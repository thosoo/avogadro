/**********************************************************************
  CompatRendererBackend implementation (see compatbackend.h).
 *****************************************************************/

#include "compatbackend.h"

#include "geometry.h"

#include <QtGlobal>
#include <cmath>
#include <QtGlobal>
// qopengl.h includes the system GL headers, exposing the fixed-function entry
// points (glEnable, glVertexPointer, ...) in the global namespace.
#include <QtGui/qopengl.h>

namespace Avogadro {
namespace rendering {

namespace {
//! Flatten GeometryVertex vectors into raw interleaved float arrays.
QVector<float> flatten(const QVector<GeometryVertex> &vertices)
{
  QVector<float> out;
  out.reserve(vertices.size() * 6);
  for (const GeometryVertex &v : vertices) {
    out.append(v.pos[0]);
    out.append(v.pos[1]);
    out.append(v.pos[2]);
    out.append(v.normal[0]);
    out.append(v.normal[1]);
    out.append(v.normal[2]);
  }
  return out;
}
} // namespace

void CompatRendererBackend::initialize()
{
  SphereGeometry sphere;
  sphere.generate();
  CylinderGeometry cylinder;
  cylinder.generate();

  m_sphereVertices = flatten(sphere.vertices());
  m_sphereIndices = sphere.indices();
  m_cylinderVertices = flatten(cylinder.vertices());
  m_cylinderIndices = cylinder.indices();

  m_initialized = !m_sphereVertices.isEmpty() && !m_sphereIndices.isEmpty() &&
                  !m_cylinderVertices.isEmpty() && !m_cylinderIndices.isEmpty();
  if (!m_initialized)
    qWarning("CompatRendererBackend: unit geometry generation failed");
}

bool CompatRendererBackend::beginFrame(const Camera *camera,
                                       const QRect &viewport, int quality)
{
  Q_UNUSED(camera);
  Q_UNUSED(quality);
  if (!m_initialized)
    return false;

  m_viewport = viewport;
  m_drawCallsThisFrame = 0;
  m_inFrame = true;

  // Frame state shared by all primitive types. Blending + per-instance alpha
  // mirror ModernRendererBackend::drawInstances (D2 "same visual class");
  // lighting is the fixed-function two-light setup initializeGL() configured.
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  // Non-uniform scaling (cylinder length vs radius) invalidates normals.
  glEnable(GL_NORMALIZE);
  // glColor drives the lit material so instance colors survive lighting.
  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  return true;
}

void CompatRendererBackend::pushSpheres(const ChunkStore<SphereInstance> &store)
{
  if (!m_inFrame || store.isEmpty())
    return;

  const int stride = 6 * static_cast<int>(sizeof(float));
  const float *verts = m_sphereVertices.constData();
  const GLushort *indices = m_sphereIndices.constData();

  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(3, GL_FLOAT, stride, verts);
  glEnableClientState(GL_NORMAL_ARRAY);
  glNormalPointer(GL_FLOAT, stride, verts + 3);

  for (unsigned int i = 0; i < store.size(); ++i) {
    const SphereInstance &s = store.at(i);
    glPushMatrix();
    glTranslated(s.pos[0], s.pos[1], s.pos[2]);
    glScaled(s.radius, s.radius, s.radius);
    glColor4ubv(s.color);
    glDrawElements(GL_TRIANGLES, m_sphereIndices.size(), GL_UNSIGNED_SHORT,
                   indices);
    glPopMatrix();
    ++m_drawCallsThisFrame;
  }

  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
}

void CompatRendererBackend::pushBonds(const ChunkStore<BondInstance> &store)
{
  if (!m_inFrame || store.isEmpty())
    return;

  const int stride = 6 * static_cast<int>(sizeof(float));
  const float *verts = m_cylinderVertices.constData();
  const GLushort *indices = m_cylinderIndices.constData();

  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(3, GL_FLOAT, stride, verts);
  glEnableClientState(GL_NORMAL_ARRAY);
  glNormalPointer(GL_FLOAT, stride, verts + 3);

  for (unsigned int i = 0; i < store.size(); ++i) {
    const BondInstance &b = store.at(i);
    const double dx = b.end[0] - b.start[0];
    const double dy = b.end[1] - b.start[1];
    const double dz = b.end[2] - b.start[2];
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length <= 0.0)
      continue;

    glPushMatrix();
    // Centre the unit cylinder (spans z in [-0.5, 0.5]) on the bond midpoint.
    glTranslated((b.start[0] + b.end[0]) * 0.5, (b.start[1] + b.end[1]) * 0.5,
                 (b.start[2] + b.end[2]) * 0.5);
    // Rotate the unit +Z axis onto the bond direction.
    const double nz = dz / length;
    if (nz <= -0.9999) {
      glRotated(180.0, 1.0, 0.0, 0.0);
    } else if (nz < 0.9999) {
      // Axis = +Z x dir (normalized); angle from Z to dir.
      double ax = -dy / length; // Z x d = (-dy, dx, 0)
      double ay = dx / length;
      const double axisLen = std::sqrt(ax * ax + ay * ay);
      if (axisLen > 1e-8) {
        ax /= axisLen;
        ay /= axisLen;
        glRotated(std::acos(nz) * 180.0 / M_PI, ax, ay, 0.0);
      }
    }
    // Non-uniform scale: unit radius in x/y, bond length along z.
    glScaled(b.radius, b.radius, length);
    glColor4ubv(b.color);
    glDrawElements(GL_TRIANGLES, m_cylinderIndices.size(), GL_UNSIGNED_SHORT,
                   indices);
    glPopMatrix();
    ++m_drawCallsThisFrame;
  }

  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
}

void CompatRendererBackend::pushLines(const ChunkStore<LineInstance> &store)
{
  if (!m_inFrame || store.isEmpty())
    return;

  // Lines are unlit in the legacy path (GLPainter::drawLine): push lighting
  // attributes so the frame state is restored afterwards.
  glPushAttrib(GL_LIGHTING_BIT);
  glDisable(GL_LIGHTING);

  // One begin/end block per line: glLineWidth is not legal inside a
  // begin/end block, and the width is per-record.
  for (unsigned int i = 0; i < store.size(); ++i) {
    const LineInstance &l = store.at(i);
    glColor4ubv(l.color);
    glLineWidth(l.width > 0.0f ? l.width : 1.0f);
    glBegin(GL_LINES);
    glVertex3f(l.start[0], l.start[1], l.start[2]);
    glVertex3f(l.end[0], l.end[1], l.end[2]);
    glEnd();
    ++m_drawCallsThisFrame;
  }

  glPopAttrib();
}

void CompatRendererBackend::endFrame()
{
  if (!m_inFrame)
    return;

  glDisable(GL_COLOR_MATERIAL);
  glDisable(GL_NORMALIZE);
  glDisable(GL_BLEND);
  m_inFrame = false;
  ++m_framesSubmitted;
}

} // namespace rendering
} // namespace Avogadro
