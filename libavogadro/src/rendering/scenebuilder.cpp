/**********************************************************************
  SceneBuilder - implementation (see scenebuilder.h).

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

#include "scenebuilder.h"

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>

#include <Eigen/Geometry>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Avogadro {
namespace rendering {

namespace {
inline quint32 packPickTarget(Primitive::Type type, int id)
{
  // The pick pass reads a 32-bit value back from the framebuffer, so the type
  // lives in the top nibble and the primitive id in the low 28 bits.
  return (static_cast<quint32>(type) << 28) |
         (static_cast<quint32>(id) & 0x0FFFFFFFu);
}
} // namespace

SceneBuilder::SceneBuilder()
  : Painter()
  , m_quality(2)
  , m_dynamicScaling(true)
  , m_currentColor(255, 255, 255, 255)
  , m_currentType(Primitive::AtomType)
  , m_currentId(0)
{
}

SceneBuilder::~SceneBuilder() = default;

int SceneBuilder::quality() const
{
  return m_quality;
}

void SceneBuilder::setQuality(int quality)
{
  m_quality = quality;
}

void SceneBuilder::setName(const Primitive *primitive)
{
  if (!primitive)
    return;
  m_currentType = primitive->type();
  if (auto *atom = qobject_cast<const Atom *>(primitive))
    m_currentId = atom->index();
  else if (auto *bond = qobject_cast<const Bond *>(primitive))
    m_currentId = bond->index();
}

void SceneBuilder::setName(Primitive::Type type, int id)
{
  m_currentType = type;
  m_currentId = id;
}

void SceneBuilder::setColor(const Color *color)
{
  if (color)
    m_currentColor = color->color();
}

void SceneBuilder::setColor(const QColor *color)
{
  if (color)
    m_currentColor = *color;
}

void SceneBuilder::setColor(float red, float green, float blue, float alpha)
{
  m_currentColor = QColor::fromRgbF(red, green, blue, alpha);
}

void SceneBuilder::setColor(QString name)
{
  m_currentColor = QColor(name);
}

void SceneBuilder::drawSphere(const Eigen::Vector3d &center, double radius)
{
  SphereInstance inst;
  inst.pos[0] = static_cast<float>(center.x());
  inst.pos[1] = static_cast<float>(center.y());
  inst.pos[2] = static_cast<float>(center.z());
  inst.radius = static_cast<float>(radius);
  inst.color[0] = static_cast<unsigned char>(m_currentColor.red());
  inst.color[1] = static_cast<unsigned char>(m_currentColor.green());
  inst.color[2] = static_cast<unsigned char>(m_currentColor.blue());
  inst.color[3] = static_cast<unsigned char>(m_currentColor.alpha());
  m_spheres.append(inst);
  m_spherePickIds.append(packPickTarget(m_currentType, m_currentId));
}

void SceneBuilder::drawCylinder(const Eigen::Vector3d &end1,
                                const Eigen::Vector3d &end2, double radius)
{
  appendBond(end1, end2, radius, 1, 0.0);
}

void SceneBuilder::drawMultiCylinder(const Eigen::Vector3d &end1,
                                     const Eigen::Vector3d &end2, double radius,
                                     int order, double shift)
{
  appendBond(end1, end2, radius, order, shift);
}

void SceneBuilder::drawLine(const Eigen::Vector3d &start,
                            const Eigen::Vector3d &end, double lineWidth)
{
  appendLine(start, end, lineWidth, 1, 0);
}

void SceneBuilder::drawMultiLine(const Eigen::Vector3d &start,
                                 const Eigen::Vector3d &end, double lineWidth,
                                 int order, short stipple)
{
  appendLine(start, end, lineWidth, order, stipple);
}

void SceneBuilder::drawMesh(const Mesh &mesh, int mode)
{
  if (!mesh.valid())
    return;
  MeshInstance inst;
  inst.mesh = &mesh;
  inst.mode = mode;
  inst.color[0] = static_cast<unsigned char>(m_currentColor.red());
  inst.color[1] = static_cast<unsigned char>(m_currentColor.green());
  inst.color[2] = static_cast<unsigned char>(m_currentColor.blue());
  inst.color[3] = static_cast<unsigned char>(m_currentColor.alpha());
  inst.colored = false;
  m_meshes.append(inst);
}

void SceneBuilder::drawColorMesh(const Mesh &mesh, int mode)
{
  if (!mesh.valid())
    return;
  MeshInstance inst;
  inst.mesh = &mesh;
  inst.mode = mode;
  inst.color[0] = static_cast<unsigned char>(m_currentColor.red());
  inst.color[1] = static_cast<unsigned char>(m_currentColor.green());
  inst.color[2] = static_cast<unsigned char>(m_currentColor.blue());
  inst.color[3] = static_cast<unsigned char>(m_currentColor.alpha());
  inst.colored = true;
  m_meshes.append(inst);
}

void SceneBuilder::drawCone(const Eigen::Vector3d &base, const Eigen::Vector3d &cap,
                            double, double)
{
  // Capture the cone axis as a line; the ring is a rare (arrow) primitive.
  appendLine(base, cap, 1.0);
}

void SceneBuilder::drawTriangle(const Eigen::Vector3d &p1, const Eigen::Vector3d &p2,
                                const Eigen::Vector3d &p3)
{
  appendLine(p1, p2, 1.0);
  appendLine(p2, p3, 1.0);
  appendLine(p3, p1, 1.0);
}

void SceneBuilder::drawTriangle(const Eigen::Vector3d &p1, const Eigen::Vector3d &p2,
                                const Eigen::Vector3d &p3, const Eigen::Vector3d &)
{
  drawTriangle(p1, p2, p3);
}

void SceneBuilder::drawShadedSector(const Eigen::Vector3d &origin,
                                    const Eigen::Vector3d &direction1,
                                    const Eigen::Vector3d &direction2,
                                    double, bool)
{
  // Capture the sector as radii to both edges plus a chord; the filled sector
  // is a rare primitive and is captured coarsely here.
  Eigen::Vector3d v1 = direction1.normalized();
  Eigen::Vector3d v2 = direction2.normalized();
  if (v1.dot(v2) > 0.999999)
    return; // degenerate: directions parallel, nothing to draw
  Eigen::Vector3d p1 = origin + v1 * 1.0;
  Eigen::Vector3d p2 = origin + v2 * 1.0;
  appendLine(origin, p1, 1.0);
  appendLine(origin, p2, 1.0);
  appendLine(p1, p2, 1.0);
}

void SceneBuilder::drawShadedQuadrilateral(const Eigen::Vector3d &p1,
                                           const Eigen::Vector3d &p2,
                                           const Eigen::Vector3d &p3,
                                           const Eigen::Vector3d &p4)
{
  appendLine(p1, p2, 1.0);
  appendLine(p2, p3, 1.0);
  appendLine(p3, p4, 1.0);
  appendLine(p4, p1, 1.0);
}

void SceneBuilder::drawArc(const Eigen::Vector3d &origin, const Eigen::Vector3d &d1,
                           const Eigen::Vector3d &d2, double radius, double lineWidth,
                           bool)
{
  // Approximate the arc as two radii to the two direction points; the curved
  // portion is a rare primitive and is captured coarsely here.
  Eigen::Vector3d a = d1.normalized() * radius;
  Eigen::Vector3d b = d2.normalized() * radius;
  appendLine(origin, origin + a, lineWidth);
  appendLine(origin, origin + b, lineWidth);
}

void SceneBuilder::drawSpline(const QVector<Eigen::Vector3d> &pts, double radius)
{
  if (pts.size() < 2)
    return;
  for (int i = 0; i < pts.size() - 1; ++i)
    appendLine(pts[i], pts[i + 1], radius);
}

void SceneBuilder::drawBox(const Eigen::Vector3d &corner1, const Eigen::Vector3d &corner2)
{
  const double X = corner2.x() - corner1.x();
  const double Y = corner2.y() - corner1.y();
  const double Z = corner2.z() - corner1.z();
  auto v = [](double a, double b, double c) {
    Eigen::Vector3d r;
    r << a, b, c;
    return r;
  };
  const Eigen::Vector3d c1 = corner1;
  const Eigen::Vector3d c2 = corner1 + v(X, 0, 0);
  const Eigen::Vector3d c3 = corner1 + v(X, Y, 0);
  const Eigen::Vector3d c4 = corner1 + v(0, Y, 0);
  const Eigen::Vector3d c5 = corner1 + v(0, 0, Z);
  const Eigen::Vector3d c6 = corner1 + v(X, 0, Z);
  const Eigen::Vector3d c7 = corner1 + v(X, Y, Z);
  const Eigen::Vector3d c8 = corner1 + v(0, Y, Z);
  // 1------2  (see painter.h drawBoxEdges diagram)
  appendLine(c1, c2, 1.0);
  appendLine(c2, c3, 1.0);
  appendLine(c3, c4, 1.0);
  appendLine(c4, c1, 1.0);
  appendLine(c5, c6, 1.0);
  appendLine(c6, c7, 1.0);
  appendLine(c7, c8, 1.0);
  appendLine(c8, c5, 1.0);
  appendLine(c1, c5, 1.0);
  appendLine(c2, c6, 1.0);
  appendLine(c3, c7, 1.0);
  appendLine(c4, c8, 1.0);
}

void SceneBuilder::drawTorus(const Eigen::Vector3d &, double, double)
{
  // Rare primitive; not captured by the emitter.
}

void SceneBuilder::drawEllipsoid(const Eigen::Vector3d &, const Eigen::Matrix3d &)
{
  // Rare primitive; not captured by the emitter.
}

int SceneBuilder::drawText(int, int, const QString &)
{
  return 0;
}

int SceneBuilder::drawText(const QPoint &, const QString &)
{
  return 0;
}

int SceneBuilder::drawText(const Eigen::Vector3d &, const QString &)
{
  return 0;
}

quint32 SceneBuilder::spherePickId(std::size_t index) const
{
  return index < m_spherePickIds.size()
             ? m_spherePickIds[static_cast<int>(index)]
             : 0u;
}

quint32 SceneBuilder::bondPickId(std::size_t index) const
{
  return index < m_bondPickIds.size()
             ? m_bondPickIds[static_cast<int>(index)]
             : 0u;
}

quint32 SceneBuilder::linePickId(std::size_t index) const
{
  return index < m_linePickIds.size()
             ? m_linePickIds[static_cast<int>(index)]
             : 0u;
}

void SceneBuilder::begin()
{
  m_currentColor = QColor(255, 255, 255, 255);
  m_currentType = Primitive::AtomType;
  m_currentId = 0;
}

void SceneBuilder::end()
{
}

void SceneBuilder::clearStores()
{
  m_spheres.clear();
  m_bonds.clear();
  m_lines.clear();
  m_meshes.clear();
  m_spherePickIds.clear();
  m_bondPickIds.clear();
  m_linePickIds.clear();
}

void SceneBuilder::appendBond(const Eigen::Vector3d &end1, const Eigen::Vector3d &end2,
                              double radius, int order, double shift)
{
  if (order < 1)
    order = 1;

  // A perpendicular reference direction for spreading multi-bond cylinders.
  Eigen::Vector3d axis = end2 - end1;
  double len = axis.norm();
  Eigen::Vector3d ortho;
  if (len > 1e-9)
    ortho = axis.normalized().unitOrthogonal() * qAbs(shift);
  else
    ortho = Eigen::Vector3d(0, 0, 0);

  for (int k = 0; k < order; ++k) {
    double offsetScale = 0.0;
    if (order > 1)
      offsetScale = (2.0 * k + 1 - order) / static_cast<double>(order - 1);
    Eigen::Vector3d offset = ortho * offsetScale;

    BondInstance inst;
    Eigen::Vector3d s = end1 + offset;
    Eigen::Vector3d e = end2 + offset;
    inst.start[0] = static_cast<float>(s.x());
    inst.start[1] = static_cast<float>(s.y());
    inst.start[2] = static_cast<float>(s.z());
    inst.end[0] = static_cast<float>(e.x());
    inst.end[1] = static_cast<float>(e.y());
    inst.end[2] = static_cast<float>(e.z());
    inst.radius = static_cast<float>(radius);
    inst.color[0] = static_cast<unsigned char>(m_currentColor.red());
    inst.color[1] = static_cast<unsigned char>(m_currentColor.green());
    inst.color[2] = static_cast<unsigned char>(m_currentColor.blue());
    inst.color[3] = static_cast<unsigned char>(m_currentColor.alpha());
    inst.order = order;
    inst.shift = shift;
    m_bonds.append(inst);
    m_bondPickIds.append(packPickTarget(m_currentType, m_currentId));
  }
}

void SceneBuilder::appendLine(const Eigen::Vector3d &start, const Eigen::Vector3d &end,
                              double width, int order, short stipple)
{
  if (order < 1)
    order = 1;

  Eigen::Vector3d axis = end - start;
  double len = axis.norm();
  Eigen::Vector3d ortho;
  if (len > 1e-9)
    ortho = axis.normalized().unitOrthogonal() * width;
  else
    ortho = Eigen::Vector3d(0, 0, 0);

  for (int k = 0; k < order; ++k) {
    double offsetScale = 0.0;
    if (order > 1)
      offsetScale = (2.0 * k + 1 - order) / static_cast<double>(order - 1);
    Eigen::Vector3d offset = ortho * offsetScale;

    LineInstance inst;
    Eigen::Vector3d s = start + offset;
    Eigen::Vector3d e = end + offset;
    inst.start[0] = static_cast<float>(s.x());
    inst.start[1] = static_cast<float>(s.y());
    inst.start[2] = static_cast<float>(s.z());
    inst.end[0] = static_cast<float>(e.x());
    inst.end[1] = static_cast<float>(e.y());
    inst.end[2] = static_cast<float>(e.z());
    inst.width = static_cast<float>(width);
    inst.color[0] = static_cast<unsigned char>(m_currentColor.red());
    inst.color[1] = static_cast<unsigned char>(m_currentColor.green());
    inst.color[2] = static_cast<unsigned char>(m_currentColor.blue());
    inst.color[3] = static_cast<unsigned char>(m_currentColor.alpha());
    inst.order = order;
    inst.stipple = stipple;
    m_lines.append(inst);
    m_linePickIds.append(packPickTarget(m_currentType, m_currentId));
  }
}

} // namespace rendering
} // namespace Avogadro
