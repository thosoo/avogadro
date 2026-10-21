/**********************************************************************
  Instanced GLSL shaders for the modern rendering tier (gpu-rendering-pipeline
  task 2.1).

  The sphere and bond (cylinder) fragment shaders mirror the legacy
  fixed-function lighting so the modern tier is visually equivalent to the
  default ball-and-stick engine:

    * Two directional lights taken verbatim from the constants in global.h
      (LIGHT_AMBIENT, LIGHT0_*, LIGHT1_*).
    * The per-vertex Phong material that GLPainter::applyAsMaterials sets up
      (glpainter_p.cpp): ambient = base/3, diffuse = base,
      specular = s + t*base with s derived from the colour chroma, and
      shininess 50.

  The geometry (unit sphere / unit cylinder) is instanced: the vertex shaders
  apply the per-instance radius / centre / axis transform, and the fragment
  shaders shade the per-instance colour.

  These are source strings only. Compilation into a program needs a live GL
  context and lives in the backend; keeping them here makes the shader text
  unit-testable (presence of required features) without OpenGL.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 *****************************************************************/

#ifndef AVOGADRO_RENDERING_SHADERS_H
#define AVOGADRO_RENDERING_SHADERS_H

#include <QString>

namespace Avogadro {
namespace rendering {

//! Shared lighting preamble, inlined into each fragment shader. Encodes the
//! two lights from global.h and the Phong material model from
//! glpainter_p.cpp so the modern tier matches the legacy tier visually.
//!
//! Parameters (all in view space, camera on +Z looking down -Z so
//! V = (0,0,1)):
//!   uAmbient     = LIGHT_AMBIENT                 = {0.2, 0.2, 0.2}
//!   uLight0Diff  = LIGHT0_DIFFUSE                 = {1, 1, 1}
//!   uLight0Spec  = LIGHT0_SPECULAR                = {1, 1, 1}
//!   uLight0Dir   = normalize(LIGHT0_POSITION)     = normalize({0.8,0.7,1.0})
//!   uLight1Diff  = LIGHT1_DIFFUSE                 = {0.3, 0.3, 0.3}
//!   uLight1Spec  = LIGHT1_SPECULAR                = {0.5, 0.5, 0.5}
//!   uLight1Dir   = normalize(LIGHT1_POSITION)     = normalize({-0.8,0.7,-0.5})
//! Material: ambient = base/3, diffuse = base,
//!           specular = chroma + (1-chroma)*base, shininess = 50.
static const QString kLightingPreamble = R"(
uniform vec3 uAmbient;
uniform vec3 uLight0Diff;
uniform vec3 uLight0Spec;
uniform vec3 uLight0Dir;
uniform vec3 uLight1Diff;
uniform vec3 uLight1Spec;
uniform vec3 uLight1Dir;

// View-space light directions (w = 0 => directional). Runtime-normalized,
// so they cannot be `const` (a uniform is a runtime value).
vec3 L0 = normalize(uLight0Dir);
vec3 L1 = normalize(uLight1Dir);
const vec3 V = vec3(0.0, 0.0, 1.0); // camera on +Z looking down -Z

// Replicate GLPainter::applyAsMaterials: derive the Phong material from the
// base (element) colour.
void materialFromBase(vec3 base, out vec3 ambient, out vec3 diffuse,
                      out vec3 specular) {
  ambient = base / 3.0;
  diffuse = base;
  float chroma = (0.5 + abs(base.r - base.g)
                  + abs(base.b - base.g)
                  + abs(base.b - base.r)) / 4.0;
  float t = 1.0 - chroma;
  specular = chroma + t * base;
}

// One directional light, fixed-function Blinn-Phong style (shininess 50).
vec3 shadeOne(vec3 N, vec3 base, vec3 lDir,
              vec3 lDiff, vec3 lSpec) {
  vec3 amb; vec3 df; vec3 sp;
  materialFromBase(base, amb, df, sp);
  vec3 outc = amb * uAmbient;
  float ndotl = dot(N, lDir);
  if (ndotl > 0.0) {
    outc += df * lDiff * ndotl;
    vec3 H = normalize(lDir + V);
    float ndoth = dot(N, H);
    if (ndoth > 0.0)
      outc += sp * lSpec * pow(ndoth, 50.0);
  }
  return outc;
}
)";

//! Vertex shader for the instanced sphere VAO.
static const QString kSphereVertexShader = R"(
#version 330
layout(location = 0) in vec3 aPos;      // unit-sphere position
layout(location = 1) in vec3 aNormal;   // unit-sphere normal
layout(location = 2) in vec3 aInstPos;  // atom centre
layout(location = 3) in float aInstRadius;
layout(location = 4) in vec4 aInstColor;// rgba, 0..1

out vec3 vNormal;
out vec4 vColor;

uniform mat4 uMVP;
uniform mat4 uModelView;

void main() {
  // Scale the unit sphere by the per-instance radius and centre it.
  vec3 worldPos = aPos * aInstRadius + aInstPos;
  // Transform the object-space normal into view space with the modelview
  // rotation so lighting is evaluated in a single (view) space. The modelview
  // carries rotation + translation + uniform zoom, so mat3() is a valid normal
  // transform; the fragment stage renormalizes the (possibly scaled) result.
  vNormal = mat3(uModelView) * aNormal;
  vColor = aInstColor;
  gl_Position = uMVP * vec4(worldPos, 1.0);
}
)";

//! Fragment shader for the instanced sphere (see kLightingPreamble).
static const QString kSphereFragmentShader = R"(
#version 330
in vec3 vNormal;
in vec4 vColor;
out vec4 fragColor;

${LIGHTING}

void main() {
  vec3 N = normalize(vNormal);
  // Per-instance, per-fragment lighting. Compute the normalised light
  // directions here (from the current uniforms) rather than at top level in
  // the shared preamble, so per-frame uniform updates are honoured.
  vec3 l0 = normalize(uLight0Dir);
  vec3 l1 = normalize(uLight1Dir);
  vec3 col = shadeOne(N, vColor.rgb, l0,
                      uLight0Diff, uLight0Spec);
  col += shadeOne(N, vColor.rgb, l1,
                  uLight1Diff, uLight1Spec);
  fragColor = vec4(col, vColor.a);
}
)";

//! Vertex shader for the instanced cylinder (bond) VAO. Maps the unit cylinder
//! (radius 1, axis +Z, z in [-0.5, 0.5]) onto the per-instance start->end axis.
static const QString kCylinderVertexShader = R"(
#version 330
layout(location = 0) in vec3 aPos;      // unit-cylinder position
layout(location = 1) in vec3 aNormal;   // unit-cylinder normal
layout(location = 2) in vec3 aInstStart;
layout(location = 3) in vec3 aInstEnd;
layout(location = 4) in float aInstRadius;
layout(location = 5) in vec4 aInstColor;

out vec3 vNormal;
out vec4 vColor;

uniform mat4 uMVP;
uniform mat4 uModelView;

void main() {
  vec3 axis = normalize(aInstEnd - aInstStart);
  // Orthonormal basis (u, v) spanning the plane perpendicular to axis.
  vec3 up = abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 u = normalize(cross(up, axis));
  vec3 v = cross(axis, u);

  float len = length(aInstEnd - aInstStart);
  // Local z maps onto the axis (centred), local x/y are the radial directions.
  // (u/v basis and worldPos stay object space: aPos is object space.)
  vec3 worldPos =
    0.5 * (aInstStart + aInstEnd)
    + axis * (aPos.z * len)
    + (aPos.x * u + aPos.y * v) * aInstRadius;

  // Cylinder normal is radial, so it maps to the same u/v combination in object
  // space; rotate it into view space with the modelview so lighting is evaluated
  // in a single (view) space. No pre-normalize needed: the fragment stage
  // renormalizes, absorbing the uniform-scale magnitude of the modelview's 3x3.
  vNormal = mat3(uModelView) * (aPos.x * u + aPos.y * v);
  vColor = aInstColor;
  gl_Position = uMVP * vec4(worldPos, 1.0);
}
)";

//! Fragment shader for the instanced cylinder (bond) (see kLightingPreamble).
static const QString kCylinderFragmentShader = R"(
#version 330
in vec3 vNormal;
in vec4 vColor;
out vec4 fragColor;

${LIGHTING}

void main() {
  vec3 N = normalize(vNormal);
  vec3 l0 = normalize(uLight0Dir);
  vec3 l1 = normalize(uLight1Dir);
  vec3 col = shadeOne(N, vColor.rgb, l0,
                      uLight0Diff, uLight0Spec);
  col += shadeOne(N, vColor.rgb, l1,
                  uLight1Diff, uLight1Spec);
  fragColor = vec4(col, vColor.a);
}
)";

//! Pick vertex shaders: the same instance transforms as the lit shaders, but
//! instead of colour they carry the per-instance pick id down to the fragment
//! stage. uPickBase lets a chunk start its instance ids at an arbitrary
//! primitive. One per primitive kind, because the instance layouts differ.
static const QString kSpherePickVertexShader = R"(
#version 330
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aInstPos;
layout(location = 3) in float aInstRadius;

uniform mat4 uMVP;
uniform uint uPickBase;

flat out uint vPickId;

void main() {
  vPickId = uPickBase + uint(gl_InstanceID);
  vec3 worldPos = aPos * aInstRadius + aInstPos;
  gl_Position = uMVP * vec4(worldPos, 1.0);
}
)";

static const QString kCylinderPickVertexShader = R"(
#version 330
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aInstStart;
layout(location = 3) in vec3 aInstEnd;
layout(location = 4) in float aInstRadius;

uniform mat4 uMVP;
uniform uint uPickBase;

flat out uint vPickId;

void main() {
  vPickId = uPickBase + uint(gl_InstanceID);
  vec3 axis = normalize(aInstEnd - aInstStart);
  vec3 up = abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 u = normalize(cross(up, axis));
  vec3 v = cross(axis, u);
  float len = length(aInstEnd - aInstStart);
  vec3 worldPos =
    0.5 * (aInstStart + aInstEnd)
    + axis * (aPos.z * len)
    + (aPos.x * u + aPos.y * v) * aInstRadius;
  gl_Position = uMVP * vec4(worldPos, 1.0);
}
)";

//! Pick fragment shader: writes the packed pick target as raw bytes so the CPU
//! can read the exact (type << 28 | id) value back with glReadPixels.
static const QString kPickFragmentShader = R"(
#version 330
flat in uint vPickId;
uniform uint uPickType;
out vec4 fragColor;

void main() {
  uint id = vPickId & 0x0FFFFFFFu;
  uint type = uPickType & 0xFu;
  uint encoded = (type << 28) | id;
  fragColor = vec4(
      float(encoded & 0xFFu) / 255.0,
      float((encoded >> 8) & 0xFFu) / 255.0,
      float((encoded >> 16) & 0xFFu) / 255.0,
      float((encoded >> 24) & 0xFFu) / 255.0);
}
)";

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_SHADERS_H
