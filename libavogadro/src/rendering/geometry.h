/**********************************************************************
  Primitive geometry for the modern rendering tier (gpu-rendering-pipeline
  task 2.1).

  The modern tier draws every atom from one unit-sphere VAO and every bond
  from one unit-cylinder VAO, instanced with the per-instance records defined
  in records.h. These generators produce the shared, per-vertex geometry
  (position + normal) that the vertex shader then scales / translates /
  orients per instance, so a single VAO renders the whole scene with one draw
  call per primitive type.

  The generators are pure CPU math (Eigen + Qt containers only, no OpenGL), so
  they can be built on worker threads (design D3) and unit-tested headlessly.
  All spheres are radius 1 and centred on the origin; all cylinders are radius
  1, height 1 and aligned to the +Z axis.

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

#ifndef AVOGADRO_RENDERING_GEOMETRY_H
#define AVOGADRO_RENDERING_GEOMETRY_H

#include <QVector>
#include <QtGlobal>

#include <cmath>

namespace Avogadro {
namespace rendering {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//! A single geometry vertex shared by the sphere and cylinder VAOs.
//! Interleaved position (3 x float) + normal (3 x float). The vertex-shader
//! binding layout is documented in formats.h.
struct GeometryVertex {
  float pos[3];
  float normal[3];
};

//! Layout of a GeometryVertex in the geometry VBO, in bytes. Kept in one place
//! so the geometry VAO and the tests agree on the stride / attribute offsets.
namespace geometryLayout {
constexpr int kStride = 24;      // 6 floats
constexpr int kPosOffset = 0;    // vec3 aPos
constexpr int kNormalOffset = 12; // vec3 aNormal
} // namespace geometryLayout

//! A unit sphere (radius 1, centred on the origin) tessellated into `rings`
//! latitude rings and `segments` longitude segments. Every vertex lies on the
//! unit sphere so its normal equals its position.
class SphereGeometry {
public:
  SphereGeometry() = default;

  //! Regenerate the tessellated unit sphere. Safe to call repeatedly.
  void generate();

  int rings() const { return m_rings; }
  void setRings(int rings) { m_rings = rings; }

  int segments() const { return m_segments; }
  void setSegments(int segments) { m_segments = segments; }

  const QVector<GeometryVertex> &vertices() const { return m_vertices; }
  const QVector<unsigned short> &indices() const { return m_indices; }

  int vertexCount() const { return m_vertices.count(); }
  int indexCount() const { return m_indices.count(); }

  //! Radius of the (unit) sphere.
  static constexpr double radius() { return 1.0; }

private:
  int m_rings = 12;
  int m_segments = 24;
  QVector<GeometryVertex> m_vertices;
  QVector<unsigned short> m_indices;
};

//! A unit cylinder (radius 1, height 1) whose axis is +Z and which spans
//! z in [-0.5, 0.5], tessellated into `segments` around the circumference.
//! There are no end caps: the atom spheres overlap the bond ends and hide them.
class CylinderGeometry {
public:
  CylinderGeometry() = default;

  //! Regenerate the tessellated unit cylinder. Safe to call repeatedly.
  void generate();

  int segments() const { return m_segments; }
  void setSegments(int segments) { m_segments = segments; }

  const QVector<GeometryVertex> &vertices() const { return m_vertices; }
  const QVector<unsigned short> &indices() const { return m_indices; }

  int vertexCount() const { return m_vertices.count(); }
  int indexCount() const { return m_indices.count(); }

  //! Radius / height of the unit cylinder.
  static constexpr double radius() { return 1.0; }
  static constexpr double height() { return 1.0; }

private:
  int m_segments = 12;
  QVector<GeometryVertex> m_vertices;
  QVector<unsigned short> m_indices;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_GEOMETRY_H
