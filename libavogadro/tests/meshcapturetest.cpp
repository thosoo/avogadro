/**********************************************************************
  Mesh capture regression test (fix-orbital-mesh-render, tasks 2.1 / 3.1,
  design D4).

  This is the first test in the repo whose fixture matches what orbital,
  VdW and density surfaces actually look like: an UNCOLORED MeshGenerator
  product (vertices + normals, zero per-vertex colors). It pins the two
  behaviors the colored-fixture tests could not reach:

    - An uncolored mesh (colors.size() == 0, agreeing vertex/normal counts)
      is VALID (Mesh::valid() widened by D1) and is captured by both
      SceneBuilder::drawMesh and drawColorMesh, carrying the instance color
      the engine assigned.
    - A genuinely invalid mesh (mismatched vertex/normal counts) is NOT
      captured and its diagnostic fires exactly once per process (D3,
      "no silent primitive loss"), then repeats silently.

  The corrupt-mesh assertion has discriminating power: if Mesh::valid() is
  reverted to reject colors.size() == 0, the uncolored-mesh assertions below
  fail.

  No GL context is needed — SceneBuilder is a CPU-side emitter (a Painter),
  so this runs headless/offscreen.

  Copyright (C) 2026 Avogadro contributors

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 *****************************************************************/

#include "config.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QStringList>

#include <avogadro/cube.h>
#include <avogadro/color3f.h>
#include <avogadro/mesh.h>
#include <avogadro/meshgenerator.h>

#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/rendering/records.h"

using namespace Avogadro;
using Avogadro::rendering::SceneBuilder;
using Avogadro::rendering::MeshInstance;
using Eigen::Vector3f;

namespace {

// A synthetic cube holding a sphere distance field (see meshgenparalleltest):
// negative inside, positive outside, so the iso=0 surface is a closed sphere
// well inside the grid. Produces a Mesh with vertices + normals and no colors.
Cube *makeSphereCube(int dim, double radius)
{
  Cube *cube = new Cube;
  Eigen::Vector3d min(-3.0, -3.0, -3.0);
  Eigen::Vector3d max(3.0, 3.0, 3.0);
  if (!cube->setLimits(min, max, Eigen::Vector3i(dim, dim, dim))) {
    delete cube;
    return nullptr;
  }
  const Eigen::Vector3d center = (min + max) * 0.5;
  std::vector<float> *data = cube->data();
  data->clear();
  data->reserve(static_cast<std::size_t>(dim) * dim * dim);
  for (int i = 0; i < dim; ++i) {
    for (int j = 0; j < dim; ++j) {
      for (int k = 0; k < dim; ++k) {
        Eigen::Vector3d pos = min + Eigen::Vector3d(i, j, k).cwiseProduct(
                                        (max - min) / (dim - 1));
        double d = (pos - center).norm() - radius;
        data->push_back(static_cast<float>(d));
      }
    }
  }
  return cube;
}

//! Tally the emitter's mesh "not captured" diagnostic. The one-time warning
//! is a process-global static, so the first call warns and later calls are
//! silent (design D3).
struct MeshTally {
  int notCaptured = 0;
  void handle(const QString &msg)
  {
    if (msg.contains("not captured"))
      ++notCaptured;
  }
};
static MeshTally *g_tally = nullptr;
void tallyHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
  if (g_tally)
    g_tally->handle(msg);
}

} // namespace

class MeshCaptureTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void uncoloredMeshIsValidAndCaptured();
  void corruptMeshNotCapturedAndReportsOnce();
};

// Task 3.1 / D4: an uncolored MeshGenerator product is valid and both emitter
// entry points capture it with the instance color.
void MeshCaptureTest::uncoloredMeshIsValidAndCaptured()
{
  Cube *cube = makeSphereCube(16, 1.5);
  QVERIFY(cube != nullptr);

  Mesh mesh;
  MeshGenerator generator;
  QVERIFY(generator.initialize(cube, &mesh, 0.0f));
  generator.run(); // run directly on the calling thread (deterministic order)

  // The production shape: geometry present, zero per-vertex colors.
  QVERIFY(mesh.vertices().size() > 0);
  QCOMPARE(mesh.normals().size(), mesh.vertices().size());
  QCOMPARE(mesh.colors().size(), std::size_t(0));

  // D1: an uncolored mesh with agreeing vertex/normal counts is valid.
  QVERIFY(mesh.valid());

  SceneBuilder builder;
  builder.begin();
  builder.clearStores();
  // The engine assigns a solid instance color before drawMesh.
  builder.setColor(1.0f, 0.0f, 0.0f, 1.0f);

  builder.drawMesh(mesh, 0);
  builder.drawColorMesh(mesh, 0);

  QCOMPARE(builder.meshes().size(), 2u);

  const MeshInstance &m0 = builder.meshes().at(0);
  QCOMPARE(m0.mesh, &mesh);
  QCOMPARE(m0.mode, 0);
  QCOMPARE(m0.colored, false);
  QCOMPARE(int(m0.color[0]), 255);
  QCOMPARE(int(m0.color[1]), 0);
  QCOMPARE(int(m0.color[2]), 0);
  QCOMPARE(int(m0.color[3]), 255);

  const MeshInstance &m1 = builder.meshes().at(1);
  QCOMPARE(m1.mesh, &mesh);
  QCOMPARE(m1.mode, 0);
  QCOMPARE(m1.colored, true);
  QCOMPARE(int(m1.color[0]), 255);
  QCOMPARE(int(m1.color[1]), 0);
  QCOMPARE(int(m1.color[2]), 0);
  QCOMPARE(int(m1.color[3]), 255);

  delete cube;
}

// Task 2.1 / D3: a genuinely invalid mesh (mismatched vertex/normal counts) is
// not captured, and the diagnostic fires exactly once then repeats silently.
void MeshCaptureTest::corruptMeshNotCapturedAndReportsOnce()
{
  Mesh corrupt;
  std::vector<Vector3f> verts(3, Vector3f(0, 0, 0));
  std::vector<Vector3f> normals(2, Vector3f(0, 0, 1)); // count mismatch
  QVERIFY(corrupt.setVertices(verts));
  QVERIFY(corrupt.setNormals(normals));
  QVERIFY(!corrupt.valid()); // mismatched counts stay invalid

  MeshTally tally;
  g_tally = &tally;
  QtMessageHandler old = qInstallMessageHandler(tallyHandler);
  {
    SceneBuilder builder;
    builder.begin();
    builder.clearStores();
    builder.drawMesh(corrupt, 0);     // first call reports
    builder.drawMesh(corrupt, 0);     // second call is silent
    builder.drawColorMesh(corrupt, 0); // shared flag -> silent
    QCOMPARE(builder.meshes().size(), 0u); // never captured
  }
  qInstallMessageHandler(old);
  g_tally = nullptr;

  QCOMPARE(tally.notCaptured, 1); // exactly once, then silent
}

QTEST_MAIN(MeshCaptureTest)
#include "meshcapturetest.moc"
