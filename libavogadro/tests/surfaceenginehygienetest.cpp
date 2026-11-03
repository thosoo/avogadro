// fix-orbital-visualization task 2.4 (design D2): SurfaceEngine bookkeeping
// hygiene.
//
// Three defects in SurfaceEngine::updateOrbitalCombo / setMolecule were fixed
// here:
//   (a) a mesh read-lock leak on the stale-cube early-out (`if (!cube)
//       continue;` skipped mesh->lock()->unlock(), wedging the next mesh
//       generator which takes the mesh write lock in Mesh::clear);
//   (b) setMolecule connected primitiveAdded/Updated/Removed without
//       disconnecting first, so repeated orbital clicks accumulated duplicate
//       per-primitive connections and multiplied per-event work;
//   (c) the combo could contain stale entries alongside valid MO entries.
//
// The test drives the real SurfaceEngine (surfaceengine.cpp is compiled
// directly into this binary because it is not exported from libavogadro) with
// AVOGADRO_TEST_COMBO_REBUILDS so the combo-rebuild counter hook is linkable.
// A small molecule with a real Mesh/Cube is used; the stale-cube case sets a
// bogus cube id so cubeById returns null.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QComboBox>

#include "engines/surfaceengine.h"

#include <avogadro/molecule.h>
#include <avogadro/mesh.h>
#include <avogadro/cube.h>

using namespace Avogadro;

class SurfaceEngineHygieneTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void staleCubeDoesNotLeakReadLock();
  void repeatedSelectionsDoNotMultiplyPerEventWork();
  void comboContainsOnlyValidMOEntries();
};

// (a) A mesh whose cube id does not resolve must not leave a read lock held
// after updateOrbitalCombo returns. Pre-fix the leaked reader made
// tryLockForWrite() fail; post-fix it succeeds immediately.
void SurfaceEngineHygieneTest::staleCubeDoesNotLeakReadLock()
{
  Molecule mol;
  Mesh *mesh = mol.addMesh();
  // Bogus cube id: cubeById(999999) resolves to null (no such cube).
  mesh->setCube(999999);

  SurfaceEngine engine;
  engine.setMolecule(&mol);
  // Creating the settings widget runs updateOrbitalCombo, which iterates the
  // meshes and hits the stale cube.
  engine.settingsWidget();

  // No read lock may remain held on the mesh, so a writer acquires it
  // immediately.
  QVERIFY(mesh->lock()->tryLockForWrite());
  mesh->lock()->unlock();
}

// (b) Repeated setMolecule (the OrbitalExtension calls it on every orbital
// click) must not accumulate duplicate per-primitive connections. After N
// setMolecule calls, adding one mesh fires primitiveAdded exactly once per
// connection; assert the combo-rebuild counter advanced by exactly 1, not N.
void SurfaceEngineHygieneTest::repeatedSelectionsDoNotMultiplyPerEventWork()
{
  Molecule mol;
  Mesh *mesh = mol.addMesh();
  Cube *cube = mol.addCube();
  cube->setCubeType(Cube::MO);
  mesh->setCube(cube->id());
  mesh->setIsoValue(0.01);
  mesh->setName("MO 1");

  SurfaceEngine engine;
  engine.setMolecule(&mol);
  engine.settingsWidget();

  surfaceEngineResetComboRebuildCount();

  // Repeated selections (5) — each calls updateOrbitalCombo via setMolecule.
  for (int i = 0; i < 5; ++i)
    engine.setMolecule(&mol);

  // Reset so we isolate the effect of a single mesh event (the 5 setMolecule
  // calls each ran updateOrbitalCombo).
  surfaceEngineResetComboRebuildCount();

  // One mesh addition fires primitiveAdded once per connection.
  mol.addMesh();

  QCOMPARE(surfaceEngineComboRebuildCount(), 1);
}

// (c) The combo must contain only valid MO entries: a mesh whose cube is stale
// is skipped, while a positive-isovalue MO mesh backed by a real MO cube is
// listed. No stale entry appears.
void SurfaceEngineHygieneTest::comboContainsOnlyValidMOEntries()
{
  Molecule mol;

  // Valid MO mesh (positive isovalue, real MO cube).
  Mesh *moMesh = mol.addMesh();
  Cube *moCube = mol.addCube();
  moCube->setCubeType(Cube::MO);
  moMesh->setCube(moCube->id());
  moMesh->setIsoValue(0.02);
  moMesh->setName("MO 7");

  // Stale-cube mesh (bogus cube id -> cubeById null -> skipped).
  Mesh *staleMesh = mol.addMesh();
  staleMesh->setCube(999999);

  SurfaceEngine engine;
  engine.setMolecule(&mol);
  QWidget *widget = engine.settingsWidget();

  QComboBox *combo = widget->findChild<QComboBox *>("orbital1Combo");
  QVERIFY(combo);
  // Only the valid MO mesh is listed.
  QCOMPARE(combo->count(), 1);
  QVERIFY(combo->itemText(0).contains("MO 7"));
}

QTEST_MAIN(SurfaceEngineHygieneTest)
#include "surfaceenginehygienetest.moc"
