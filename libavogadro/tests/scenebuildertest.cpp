/**********************************************************************
  SceneBuilder + RendererBackend test (gpu-rendering-pipeline task 1.2).

  Verifies the tier-agnostic emitter seam:
    - SceneBuilder (a Painter) turns draw calls into chunk stores,
    - a RendererBackend receives those stores via pushChunks,
    - an engine (BSDY ball-and-stick) renders a test molecule headlessly
      through the emitter with no engine signature changes.

  Copyright (C) 2026 Avogadro contributors

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 *****************************************************************/

#include "config.h"

#include <QtTest>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/painter.h>
#include <avogadro/painterdevice.h>
#include <avogadro/camera.h>

#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/rendering/rendererbackend.h"
#include "libavogadro/src/rendering/records.h"
#include "libavogadro/src/engines/bsdyengine.h"

using Avogadro::Molecule;
using Avogadro::Atom;
using Avogadro::Bond;
using Avogadro::Color;
using Avogadro::Camera;
using Avogadro::BSDYEngine;
using Avogadro::rendering::SceneBuilder;
using Avogadro::rendering::RendererBackend;
using Avogadro::rendering::SphereInstance;
using Avogadro::rendering::BondInstance;

using Eigen::Vector3d;

namespace {

//! A no-op RendererBackend that records how it was driven. Used to verify the
//! pushChunk contract without any OpenGL context (headless).
class NullBackend : public RendererBackend {
public:
  bool beginFrame(const Camera *, const QRect &, int quality) override
  {
    ++frameCount;
    lastQuality = quality;
    return true;
  }

  void pushSpheres(const Avogadro::rendering::ChunkStore<Avogadro::rendering::SphereInstance> &s) override
  {
    ++spheresPushed;
    lastSpheres = s.size();
  }
  void pushBonds(const Avogadro::rendering::ChunkStore<Avogadro::rendering::BondInstance> &b) override
  {
    ++bondsPushed;
    lastBonds = b.size();
  }
  void pushLines(const Avogadro::rendering::ChunkStore<Avogadro::rendering::LineInstance> &l) override
  {
    ++linesPushed;
    lastLines = l.size();
  }
  void pushMeshes(const Avogadro::rendering::ChunkStore<Avogadro::rendering::MeshInstance> &m) override
  {
    ++meshesPushed;
    lastMeshes = m.size();
  }
  void endFrame() override { ++frameEnded; }

  QList<Avogadro::GLHit> pick(int, int, int, int) override
  {
    ++pickCalls;
    return QList<Avogadro::GLHit>();
  }

  int frameCount = 0;
  int frameEnded = 0;
  int spheresPushed = 0;
  int bondsPushed = 0;
  int linesPushed = 0;
  int meshesPushed = 0;
  int pickCalls = 0;
  int lastQuality = -1;
  size_t lastSpheres = 0;
  size_t lastBonds = 0;
  size_t lastLines = 0;
  size_t lastMeshes = 0;
};

//! A minimal PainterDevice that routes painter() to a SceneBuilder so an engine
//! can paint through the emitter exactly as GLWidget would.
class DeviceTestPainterDevice : public Avogadro::PainterDevice {
public:
  DeviceTestPainterDevice(SceneBuilder *builder, Molecule *mol, Color *map)
    : m_builder(builder), m_molecule(mol), m_colorMap(map) {}

  Avogadro::Painter *painter() const override { return m_builder; }
  Camera *camera() const override { return const_cast<Camera*>(&m_camera); }
  bool isSelected(const Avogadro::Primitive *) const override { return false; }
  double radius(const Avogadro::Primitive *) const override { return 0.3; }
  const Molecule *molecule() const override { return m_molecule; }
  Color *colorMap() const override { return m_colorMap; }
  int width() override { return 800; }
  int height() override { return 600; }

  SceneBuilder *m_builder;
  Molecule *m_molecule;
  Color *m_colorMap;
  Camera m_camera;
};

} // anonymous namespace

class SceneBuilderTest : public QObject
{
  Q_OBJECT

  private Q_SLOTS:
    void testBeginResetsState();
    void testDrawSphereAppendsRecord();
    void testDrawCylinderAndMultiCylinder();
    void testColorIsBakedIntoRecords();
    void testDirtyRangesAfterEmits();
    void testEngineThroughEmitter();
    void testNullBackendContract();
};

void SceneBuilderTest::testBeginResetsState()
{
  SceneBuilder builder;
  builder.begin();
  builder.setColor(1.0f, 0.0f, 0.0f, 1.0f);
  builder.drawSphere(Vector3d(0, 0, 0), 0.5);
  QCOMPARE(builder.spheres().size(), 1u);

  // begin() resets the current color so a new pass starts from white.
  builder.begin();
  builder.drawSphere(Vector3d(0, 0, 0), 0.5);
  const SphereInstance &inst = builder.spheres().at(1);
  QCOMPARE(inst.color[0], 255);
  QCOMPARE(inst.color[1], 255);
  QCOMPARE(inst.color[2], 255);
}

void SceneBuilderTest::testDrawSphereAppendsRecord()
{
  SceneBuilder builder;
  builder.clearStores();
  builder.setColor(0.2f, 0.4f, 0.6f, 1.0f);
  builder.drawSphere(Vector3d(1, 2, 3), 0.75);

  QCOMPARE(builder.spheres().size(), 1u);
  const SphereInstance &inst = builder.spheres().at(0);
  QCOMPARE(inst.pos[0], 1.0f);
  QCOMPARE(inst.pos[1], 2.0f);
  QCOMPARE(inst.pos[2], 3.0f);
  QCOMPARE(inst.radius, 0.75f);
}

void SceneBuilderTest::testDrawCylinderAndMultiCylinder()
{
  SceneBuilder builder;
  builder.clearStores();

  // A single bond half.
  builder.setColor(0.5f, 0.5f, 0.5f, 1.0f);
  builder.drawCylinder(Vector3d(0, 0, 0), Vector3d(2, 0, 0), 0.1);
  QCOMPARE(builder.bonds().size(), 1u);

  // A double bond (order 2) splits into two parallel halves.
  builder.drawMultiCylinder(Vector3d(0, 0, 0), Vector3d(2, 0, 0), 0.1, 2, 0.2);
  QCOMPARE(builder.bonds().size(), 3u);

  const BondInstance &double1 = builder.bonds().at(1);
  const BondInstance &double2 = builder.bonds().at(2);
  QCOMPARE(double1.order, 2);
  QCOMPARE(double2.order, 2);
  // The two halves of a double bond are offset in opposite directions, so the
  // vector between their starts must be non-zero.
  const double dx = double1.start[0] - double2.start[0];
  const double dy = double1.start[1] - double2.start[1];
  const double dz = double1.start[2] - double2.start[2];
  QVERIFY(dx * dx + dy * dy + dz * dz > 0.0);
}

void SceneBuilderTest::testColorIsBakedIntoRecords()
{
  SceneBuilder builder;
  builder.clearStores();

  // QColor path.
  QColor qc(10, 20, 30, 40);
  builder.setColor(&qc);
  builder.drawSphere(Vector3d(0, 0, 0), 0.5);
  const SphereInstance &c = builder.spheres().at(0);
  QCOMPARE(c.color[0], 10);
  QCOMPARE(c.color[1], 20);
  QCOMPARE(c.color[2], 30);
  QCOMPARE(c.color[3], 40);

  // Color* path (resolved through Color::color()). Compare against a QColor
  // built the same way so the 0-255 rounding matches exactly.
  Color col;
  col.setFromRgba(0.5f, 0.25f, 0.0f, 1.0f);
  builder.setColor(&col);
  builder.drawSphere(Vector3d(0, 0, 0), 0.5);
  const SphereInstance &c2 = builder.spheres().at(1);
  QColor expected = QColor::fromRgbF(0.5, 0.25, 0.0, 1.0);
  QCOMPARE(c2.color[0], static_cast<unsigned char>(expected.red()));
  QCOMPARE(c2.color[1], static_cast<unsigned char>(expected.green()));
  QCOMPARE(c2.color[2], static_cast<unsigned char>(expected.blue()));
}

void SceneBuilderTest::testDirtyRangesAfterEmits()
{
  SceneBuilder builder;
  builder.clearStores();
  builder.setColor(0.5f, 0.5f, 0.5f, 1.0f);
  builder.drawSphere(Vector3d(0, 0, 0), 0.5);
  builder.drawSphere(Vector3d(0, 0, 0), 0.5);

  // Both instances were appended, so the store is fully dirty.
  QCOMPARE(builder.spheres().dirtyRanges().size(), 1);
  QCOMPARE(builder.spheres().dirtyRanges().at(0).count, 2u);

  // After acknowledge, no dirty ranges until a new emit.
  builder.spheres().acknowledge();
  QVERIFY(builder.spheres().dirtyRanges().isEmpty());
  builder.drawSphere(Vector3d(0, 0, 0), 0.5);
  QCOMPARE(builder.spheres().dirtyRanges().at(0).count, 1u);
}

void SceneBuilderTest::testEngineThroughEmitter()
{
  // A small H2O-like molecule: O at origin, two H off axis, two bonds.
  Molecule mol;
  Atom *o = mol.addAtom(8, Vector3d(0, 0, 0));
  Atom *h1 = mol.addAtom(1, Vector3d(0.94, 0, 0));
  Atom *h2 = mol.addAtom(1, Vector3d(-0.34, 0.86, 0));
  mol.addBond(o->id(), h1->id(), 1);
  mol.addBond(o->id(), h2->id(), 1);

  SceneBuilder builder;
  Color colorMap;
  DeviceTestPainterDevice device(&builder, &mol, &colorMap);

  BSDYEngine engine;
  engine.setMolecule(&mol);
  engine.setPainterDevice(&device);
  engine.setEnabled(true);

  builder.begin();
  builder.clearStores();
  engine.renderOpaque(&device);

  // Three atoms drawn as spheres; each of the two single bonds is drawn as two
  // halves (atom->mid, mid->atom), so four bond-half instances total.
  QCOMPARE(builder.spheres().size(), 3u);
  QCOMPARE(builder.bonds().size(), 4u);

  // The baked colors are element colors (non-white for O and H).
  QVERIFY(builder.spheres().at(0).color[0] != 255 ||
          builder.spheres().at(0).color[1] != 255 ||
          builder.spheres().at(0).color[2] != 255);
}

void SceneBuilderTest::testNullBackendContract()
{
  SceneBuilder builder;
  builder.begin();
  builder.clearStores();
  builder.setColor(0.5f, 0.5f, 0.5f, 1.0f);
  builder.drawSphere(Vector3d(0, 0, 0), 0.5);
  builder.drawCylinder(Vector3d(0, 0, 0), Vector3d(1, 0, 0), 0.1);

  NullBackend backend;
  QRect viewport(0, 0, 800, 600);
  QVERIFY(backend.beginFrame(nullptr, viewport, 2));
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.pushLines(builder.lines());
  backend.pushMeshes(builder.meshes());
  backend.endFrame();

  QCOMPARE(backend.frameCount, 1);
  QCOMPARE(backend.frameEnded, 1);
  QCOMPARE(backend.lastQuality, 2);
  QCOMPARE(backend.spheresPushed, 1);
  QCOMPARE(backend.bondsPushed, 1);
  QCOMPARE(backend.lastSpheres, 1u);
  QCOMPARE(backend.lastBonds, 1u);
  QVERIFY(backend.pick(10, 10, 20, 20).isEmpty());
  QCOMPARE(backend.pickCalls, 1);
}

QTEST_MAIN(SceneBuilderTest)
#include "scenebuildertest.moc"
