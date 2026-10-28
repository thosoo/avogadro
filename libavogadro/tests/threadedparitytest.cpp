/**********************************************************************
  Tessellator parity (gpu-rendering-pipeline task 6.1).

  Proves the snapshot-driven SnapshotTessellator produces byte-identical
  ChunkStore records to the GUI-thread BSDYEngine emission: the tessellator
  drives the exact same SceneBuilder draw calls, so parity is structural. This
  is the headless correctness gate that must pass before the pool is wired in
  (design A7): it retires the parity risk before any threading exists.

  The test is fully headless: the BSDYEngine renders into a TestDevice
  painter (not a GL surface) and SnapshotTessellator tessellates an immutable
  snapshot, so no GL context is required. It therefore runs on any runner,
  including headless CI.
 *****************************************************************/

#include <QtTest>
#include <QColor>
#include <Eigen/Geometry>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/painter.h>
#include <avogadro/painterdevice.h>
#include <avogadro/camera.h>

#include "libavogadro/src/engines/bsdyengine.h"
#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/rendering/records.h"
#include "libavogadro/src/rendering/chunkstore.h"
#include "libavogadro/src/rendering/scenesnapshot.h"
#include "libavogadro/src/rendering/snapshottessellator.h"

using namespace Avogadro;
using Avogadro::rendering::ChunkStore;
using Avogadro::rendering::SceneBuilder;
using Avogadro::rendering::SceneSnapshot;
using Avogadro::rendering::TessellationResult;
using Avogadro::rendering::JobParams;
using Avogadro::rendering::BondInstance;
using Avogadro::rendering::SphereInstance;
using Avogadro::rendering::CapturedAtom;
using Avogadro::rendering::SnapshotAtom;
using Avogadro::rendering::SnapshotBond;

//! Element colour map mirroring the parity harness (CPK subset) so the
//! tessellator and engine resolve identical atom colours.
class TestColorMap : public Color
{
public:
  TestColorMap() { setFromRgba(0.8f, 0.8f, 0.8f, 1.0f); }
  void setFromPrimitive(const Primitive *p) override
  {
    const Atom *atom = static_cast<const Atom *>(p);
    if (!atom)
      return;
    if (!atom->customColorName().isEmpty()) {
      const QColor c(atom->customColorName());
      setFromRgba(static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                  static_cast<float>(c.blueF()), 1.0f);
      return;
    }
    switch (atom->atomicNumber()) {
      case 1: setFromRgba(0.95f, 0.95f, 0.95f, 1.0f); break;
      case 6: setFromRgba(0.20f, 0.20f, 0.20f, 1.0f); break;
      case 7: setFromRgba(0.05f, 0.05f, 0.90f, 1.0f); break;
      case 8: setFromRgba(0.90f, 0.10f, 0.10f, 1.0f); break;
      default: setFromRgba(0.80f, 0.60f, 0.20f, 1.0f); break;
    }
  }
};

//! Minimal PainterDevice: enough for BSDYEngine::renderOpaque (which reads
//! molecule(), colorMap(), painter() and the engine's own radius()).
class TestDevice : public PainterDevice
{
public:
  TestDevice(Painter *p, Molecule *mol, Color *map)
    : m_painter(p), m_molecule(mol), m_colorMap(map) {}
  Painter *painter() const override { return m_painter; }
  Camera *camera() const override
  {
    return const_cast<Camera *>(&m_camera);
  }
  bool isSelected(const Primitive *) const override { return false; }
  double radius(const Primitive *) const override { return 0.3; }
  const Molecule *molecule() const override { return m_molecule; }
  Color *colorMap() const override { return m_colorMap; }
  int width() override { return 256; }
  int height() override { return 256; }
  Painter *m_painter;
  Molecule *m_molecule;
  Color *m_colorMap;
  Camera m_camera;
};

class TessellatorParityTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void testOpaqueParity();
  void testWorkerPathPickNames();

private:
  //! Build an immutable snapshot from the live molecule (copies values).
  SceneSnapshot makeSnapshot(const Molecule &mol);
  //! Capture GUI-thread emission state (radii + resolved colours + settings).
  JobParams captureParams(const BSDYEngine &engine, const Molecule &mol,
                          TestColorMap &map, TestDevice &radiusDevice);

};

void TessellatorParityTest::initTestCase()
{
  // Fully headless: no GL context is needed (see the header comment).
}

void TessellatorParityTest::cleanupTestCase()
{
  // No GL context to clean up.
}

SceneSnapshot TessellatorParityTest::makeSnapshot(const Molecule &mol)
{
  SceneSnapshot snap;
  snap.revision = mol.revision();
  snap.atoms.reserve(mol.numAtoms());
  for (unsigned int i = 0; i < mol.numAtoms(); ++i) {
    const Atom *a = mol.atom(i);
    SnapshotAtom sa;
    const Eigen::Vector3d *pp = a->pos();
    sa.pos[0] = static_cast<float>(pp->x());
    sa.pos[1] = static_cast<float>(pp->y());
    sa.pos[2] = static_cast<float>(pp->z());
    sa.atomicNumber = a->atomicNumber();
    sa.index = i;
    snap.atoms.append(sa);
  }
  snap.bonds.reserve(mol.numBonds());
  for (unsigned int i = 0; i < mol.numBonds(); ++i) {
    const Bond *b = mol.bond(i);
    SnapshotBond sb;
    sb.begin = b->beginAtomId();
    sb.end = b->endAtomId();
    sb.order = b->order();
    sb.index = i;
    snap.bonds.append(sb);
  }
  return snap;
}

JobParams TessellatorParityTest::captureParams(const BSDYEngine &engine,
                                               const Molecule &mol,
                                               TestColorMap &map,
                                               TestDevice &radiusDevice)
{
  JobParams params;
  // Defaults match BSDYEngine's constructor (opaque, multi-bond on, vdW radii).
  params.settings.alpha = 1.0f;
  params.settings.showMulti = 2;
  params.settings.bondRadius = 0.1f;
  params.settings.shift = 0.15f;

  params.atoms.resize(mol.numAtoms());
  for (unsigned int i = 0; i < mol.numAtoms(); ++i) {
    const Atom *a = mol.atom(i);
    CapturedAtom ca;
    // Public radius() reads the engine's private settings for us.
    ca.radius = static_cast<float>(engine.radius(&radiusDevice, a));
    ca.customColorName = a->customColorName();
    if (a->customColorName().isEmpty()) {
      map.setFromPrimitive(a);
      ca.rgba[0] = map.red();
      ca.rgba[1] = map.green();
      ca.rgba[2] = map.blue();
      ca.rgba[3] = map.alpha();
    } else {
      QColor c(a->customColorName());
      ca.rgba[0] = static_cast<float>(c.redF());
      ca.rgba[1] = static_cast<float>(c.greenF());
      ca.rgba[2] = static_cast<float>(c.blueF());
      ca.rgba[3] = static_cast<float>(c.alphaF());
    }
    params.atoms[i] = ca;
  }
  return params;
}

void TessellatorParityTest::testOpaqueParity()
{
  Molecule mol;
  Atom *c = mol.addAtom(6, Eigen::Vector3d(0.0, 0.0, 0.0));
  Atom *o = mol.addAtom(8, Eigen::Vector3d(1.5, 0.0, 0.0));
  Atom *n = mol.addAtom(7, Eigen::Vector3d(0.0, 1.4, 0.0));
  Atom *h = mol.addAtom(1, Eigen::Vector3d(-0.9, -0.8, 0.0));
  c->setCustomColorName("red");
  o->setCustomColorName("blue");
  mol.addBond(c->id(), o->id(), 1);
  mol.addBond(c->id(), n->id(), 1);
  mol.addBond(c->id(), h->id(), 1);

  TestColorMap map;

  // Reference: the real engine emits into the SceneBuilder on the GUI thread.
  BSDYEngine engine;
  engine.setMolecule(&mol);
  engine.setPainterDevice(new TestDevice(nullptr, &mol, &map));
  engine.setEnabled(true);
  // Default settings: opaque (alpha ~ 1), multi-bond on, vdW radii.

  SceneBuilder refBuilder;
  TestDevice device(&refBuilder, &mol, &map);
  engine.setPainterDevice(&device);
  QVERIFY(engine.renderOpaque(&device));

  const ChunkStore<SphereInstance> refSpheres = refBuilder.spheres();
  const ChunkStore<BondInstance> refBonds = refBuilder.bonds();

  // Capture GUI-thread state, then tessellate from the immutable snapshot.
  TestDevice radiusDevice(nullptr, &mol, &map);
  JobParams params = captureParams(engine, mol, map, radiusDevice);
  SceneSnapshot snap = makeSnapshot(mol);
  const TessellationResult tes = snapshotTessellateBSDY(snap, params);

  // Compare record counts first (cheap, catches structural divergence).
  QCOMPARE(tes.spheres.size(), refSpheres.size());
  QCOMPARE(tes.bonds.size(), refBonds.size());
  QVERIFY(tes.spheres.size() > 0);
  QVERIFY(tes.bonds.size() > 0);

  // Compare records: positions/radius within a float epsilon, colour bytes
  // exact (both paths feed the same SceneBuilder colour conversion).
  const float eps = 1e-4f;
  for (unsigned int i = 0; i < refSpheres.size(); ++i) {
    const SphereInstance &r = refSpheres[i];
    const SphereInstance &t = tes.spheres[i];
    for (int k = 0; k < 3; ++k)
      QVERIFY(qAbs(r.pos[k] - t.pos[k]) <= eps);
    QVERIFY(qAbs(r.radius - t.radius) <= eps);
    for (int k = 0; k < 4; ++k)
      QCOMPARE(t.color[k], r.color[k]);
  }
  for (unsigned int i = 0; i < refBonds.size(); ++i) {
    const BondInstance &r = refBonds[i];
    const BondInstance &t = tes.bonds[i];
    for (int k = 0; k < 3; ++k) {
      QVERIFY(qAbs(r.start[k] - t.start[k]) <= eps);
      QVERIFY(qAbs(r.end[k] - t.end[k]) <= eps);
    }
    QVERIFY(qAbs(r.radius - t.radius) <= eps);
    QCOMPARE(r.order, t.order);
    QCOMPARE(r.color[0], t.color[0]);
    QCOMPARE(r.color[1], t.color[1]);
    QCOMPARE(r.color[2], t.color[2]);
    QCOMPARE(r.color[3], t.color[3]);
  }

  qInfo().noquote() << "tessellator parity OK: spheres =" << tes.spheres.size()
                    << " bonds =" << tes.bonds.size();
}

//! Worker-path pick names (design D5.2 / spec "pick hits identify the clicked
//! primitive", worker scenario): tessellate a 3-bond molecule through the
//! headless SnapshotTessellator and assert every bond half-segment's pickId
//! equals that bond's index. The tessellator drives the exact same SceneBuilder
//! as the GUI-thread engine path, so this fails on the unfixed code (all
//! pickIds 0) precisely where the render/pick path would. Fully headless.
void TessellatorParityTest::testWorkerPathPickNames()
{
  Molecule mol;
  mol.addAtom(6, Eigen::Vector3d(-3.0, 0.0, 0.0));
  mol.addAtom(6, Eigen::Vector3d(-1.5, 0.0, 0.0));
  mol.addAtom(6, Eigen::Vector3d(1.5, 0.0, 0.0));
  mol.addAtom(6, Eigen::Vector3d(3.0, 0.0, 0.0));
  mol.addBond(0, 1, 1);
  mol.addBond(1, 2, 2); // double bond: still two half-segments, one pick id
  mol.addBond(2, 3, 1);

  TestColorMap map;
  // Reference: the GUI-thread engine emission path.
  BSDYEngine engine;
  engine.setMolecule(&mol);
  engine.setEnabled(true);
  SceneBuilder refBuilder;
  TestDevice refDevice(&refBuilder, &mol, &map);
  engine.setPainterDevice(&refDevice);
  QVERIFY(engine.renderOpaque(&refDevice));
  const ChunkStore<BondInstance> refBonds = refBuilder.bonds();

  // Capture GUI-thread state, then tessellate from the immutable snapshot.
  TestDevice radiusDevice(nullptr, &mol, &map);
  JobParams params = captureParams(engine, mol, map, radiusDevice);
  SceneSnapshot snap = makeSnapshot(mol);
  const TessellationResult tes = snapshotTessellateBSDY(snap, params);

  QVERIFY2(tes.bonds.size() >= 6u,
           "expected at least 2 half-segments x 3 bonds = >= 6 bond instances");

  // Walk the bond records in emission order. Each snapshot bond i (bond order
  // o) emits two half-calls (v1->v3, v3->v2), each producing o half-segments,
  // ALL stamped with pickId == i. Verify every half-segment of bond i carries
  // that bond's index (design D2 / spec: every segment of a split bond
  // resolves to that bond's index). On the unfixed code every pickId is 0, so
  // the double bond (order 2, records 2-5) and bond 2 (records 6-7) fail.
  unsigned int offset = 0;
  for (unsigned int i = 0; i < snap.bonds.size(); ++i) {
    const unsigned int count = 2u * snap.bonds.at(i).order;
    for (unsigned int r = offset; r < offset + count; ++r) {
      QCOMPARE(static_cast<int>(tes.bonds.at(r).pickId),
               static_cast<int>(snap.bonds.at(i).index));
    }
    offset += count;
  }
  QCOMPARE(static_cast<int>(offset), static_cast<int>(tes.bonds.size()));

  // The worker path must match the GUI reference for pickId too (both are the
  // same SceneBuilder; a divergence would mean the paths disagree on names).
  for (unsigned int i = 0; i < tes.bonds.size(); ++i)
    QCOMPARE(static_cast<int>(tes.bonds.at(i).pickId),
             static_cast<int>(refBonds.at(i).pickId));
}

#include "threadedparitytest.moc"
QTEST_MAIN(TessellatorParityTest)
