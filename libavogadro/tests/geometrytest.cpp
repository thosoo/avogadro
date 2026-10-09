/**********************************************************************
  Geometry + instance-format test (gpu-rendering-pipeline task 2.1).

  Headless (no OpenGL) verification of the modern-tier primitive geometry:
    - unit-sphere tessellation (vertex/index counts, vertices on the unit
      sphere, outward-facing winding),
    - unit-cylinder tessellation (axis, radius, radial normals),
    - per-instance vertex-format layout matching records.h.

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

#include <QVector>
#include <QtTest>

#include <Eigen/Geometry>

#include "libavogadro/src/rendering/geometry.h"
#include "libavogadro/src/rendering/instanceformat.h"
#include "libavogadro/src/rendering/records.h"

using namespace Avogadro::rendering;
using Avogadro::rendering::CylinderGeometry;
using Avogadro::rendering::GeometryVertex;
using Avogadro::rendering::SphereGeometry;

namespace {

//! True when a and b differ by at most tol.
bool approx(double a, double b, double tol = 1e-4)
{
  return qAbs(a - b) <= tol;
}

//! Cross product of two edges of a triangle, i.e. an (unnormalised) outward
//! face normal for a CCW-oriented triangle.
Eigen::Vector3d faceNormal(const GeometryVertex &a, const GeometryVertex &b,
                           const GeometryVertex &c)
{
  Eigen::Vector3d e1(b.pos[0] - a.pos[0], b.pos[1] - a.pos[1],
                     b.pos[2] - a.pos[2]);
  Eigen::Vector3d e2(c.pos[0] - a.pos[0], c.pos[1] - a.pos[1],
                     c.pos[2] - a.pos[2]);
  return e1.cross(e2);
}

} // anonymous namespace

class GeometryTest : public QObject
{
  Q_OBJECT

  private Q_SLOTS:
    void testSphereVertexCounts();
    void testSphereVerticesOnUnitSphere();
    void testSphereWindingOutward();
    void testCylinderVertexCounts();
    void testCylinderAxisAndRadius();
    void testInstanceFormatLayout();
};

void GeometryTest::testSphereVertexCounts()
{
  SphereGeometry s;
  s.setRings(10);
  s.setSegments(20);
  s.generate();

  // (rings + 1) quads around, `segments` quads per ring, 2 triangles each.
  QCOMPARE(s.vertexCount(), 11 * 20);
  QCOMPARE(s.indexCount(), 10 * 20 * 6);

  // Every index references a real vertex.
  for (unsigned short idx : s.indices())
    QVERIFY(idx < static_cast<unsigned int>(s.vertexCount()));
}

void GeometryTest::testSphereVerticesOnUnitSphere()
{
  SphereGeometry s;
  s.setRings(16);
  s.setSegments(32);
  s.generate();

  const double eps = 1e-5;
  for (const GeometryVertex &v : s.vertices()) {
    const Eigen::Vector3d pos(v.pos[0], v.pos[1], v.pos[2]);
    const Eigen::Vector3d n(v.normal[0], v.normal[1], v.normal[2]);
    QVERIFY(approx(pos.norm(), 1.0));
    QVERIFY(approx(n.norm(), 1.0));
    // Normal must match position on a unit sphere.
    QVERIFY((pos - n).norm() < eps);
  }
}

void GeometryTest::testSphereWindingOutward()
{
  SphereGeometry s;
  s.setRings(6);
  s.setSegments(8);
  s.generate();

  const auto &v = s.vertices();
  const auto &i = s.indices();
  // Every triangle's face normal must point outward (non-negative dot with the
  // vertex position, i.e. the angle to the outward radial direction is acute).
  for (int t = 0; t + 2 < i.size(); t += 3) {
    const Eigen::Vector3d fn =
      faceNormal(v.at(i[t]), v.at(i[t + 1]), v.at(i[t + 2]));
    const Eigen::Vector3d centroid(
      (v.at(i[t]).pos[0] + v.at(i[t + 1]).pos[0] + v.at(i[t + 2]).pos[0]) / 3.0,
      (v.at(i[t]).pos[1] + v.at(i[t + 1]).pos[1] + v.at(i[t + 2]).pos[1]) / 3.0,
      (v.at(i[t]).pos[2] + v.at(i[t + 1]).pos[2] + v.at(i[t + 2]).pos[2]) / 3.0);
    QVERIFY(fn.dot(centroid) >= -1e-9);
  }
}

void GeometryTest::testCylinderVertexCounts()
{
  CylinderGeometry c;
  c.setSegments(16);
  c.generate();

  QCOMPARE(c.vertexCount(), 16 * 2);
  QCOMPARE(c.indexCount(), 16 * 6);

  for (unsigned short idx : c.indices())
    QVERIFY(idx < static_cast<unsigned int>(c.vertexCount()));
}

void GeometryTest::testCylinderAxisAndRadius()
{
  CylinderGeometry c;
  c.setSegments(20);
  c.generate();

  double minZ = 1e9, maxZ = -1e9;
  for (const GeometryVertex &v : c.vertices()) {
    // The radius in the x/y plane must be 1 (unit cylinder).
    const double r = std::sqrt(static_cast<double>(v.pos[0]) * v.pos[0]
                               + static_cast<double>(v.pos[1]) * v.pos[1]);
    QVERIFY(approx(r, 1.0));
    // Normals are radial: no z component.
    QCOMPARE(v.normal[2], 0.0f);
    minZ = qMin(minZ, static_cast<double>(v.pos[2]));
    maxZ = qMax(maxZ, static_cast<double>(v.pos[2]));
  }
  // The cylinder spans z in [-0.5, 0.5].
  QVERIFY(approx(minZ, -0.5));
  QVERIFY(approx(maxZ, 0.5));
}

void GeometryTest::testInstanceFormatLayout()
{
  // The compile-time InstanceFormatAssertions already pin the layout; here we
  // pin the runtime stride constants and the record sizes.
  QCOMPARE(static_cast<int>(sizeof(SphereInstance)),
           sphereInstanceLayout::kStride);
  QCOMPARE(static_cast<int>(sizeof(BondInstance)),
           bondInstanceLayout::kStride);

  QCOMPARE(sphereInstanceLayout::kPosOffset, 0);
  QCOMPARE(sphereInstanceLayout::kRadiusOffset, 12);
  QCOMPARE(sphereInstanceLayout::kColorOffset, 16);

  QCOMPARE(bondInstanceLayout::kStartOffset, 0);
  QCOMPARE(bondInstanceLayout::kEndOffset, 12);
  QCOMPARE(bondInstanceLayout::kRadiusOffset, 24);
  QCOMPARE(bondInstanceLayout::kColorOffset, 28);
}

QTEST_MAIN(GeometryTest)
#include "geometrytest.moc"
