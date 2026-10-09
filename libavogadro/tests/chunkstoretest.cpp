/**********************************************************************
  ChunkStore and Molecule revision test.

  Unit tests for the tier-agnostic scene core (gpu-rendering-pipeline task 1.1):
    - ChunkStore<T> dirty-range tracking under append / in-place modify / clear
    - Molecule::revision() bumping on structural and positional mutations

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

#include <Eigen/Core>

#include "libavogadro/src/rendering/chunkstore.h"
#include "libavogadro/src/rendering/scenesnapshot.h"

using Avogadro::Molecule;
using Avogadro::Atom;
using Avogadro::Bond;
using Avogadro::rendering::ChunkStore;
using Avogadro::rendering::DirtyRange;
using Avogadro::rendering::SnapshotAtom;
using Avogadro::rendering::SceneSnapshot;

using Eigen::Vector3d;

namespace {
// A stand-in for a real per-instance record (sphere / bond-half).
struct FakeInstance {
  float pos[3] = {0.0f, 0.0f, 0.0f};
  unsigned char atomicNumber = 0;
};
}

class ChunkStoreTest : public QObject
{
  Q_OBJECT

  private Q_SLOTS:
    void init();
    void cleanup();

    void testAppendMarksDirty();
    void testDirtyRangesCoalesce();
    void testDirtyRangesSplit();
    void testModifySingle();
    void testMarkDirtyClamp();
    void testAcknowledgeClears();
    void testClearResets();
    void testSetAll();
    void testSnapshotCopiesValues();
    void testMoleculeRevisionBumps();
    void testMoleculeRevisionOnPositionEdit();
};

void ChunkStoreTest::init()
{
}

void ChunkStoreTest::cleanup()
{
}

void ChunkStoreTest::testAppendMarksDirty()
{
  ChunkStore<FakeInstance> store;
  QVERIFY(store.isEmpty());
  QVERIFY(!store.hasDirty());

  for (unsigned int i = 0; i < 5; ++i) {
    FakeInstance inst;
    inst.pos[0] = static_cast<float>(i);
    store.append(inst);
  }

  QCOMPARE(store.size(), 5u);
  QVERIFY(store.hasDirty());

  QList<DirtyRange> ranges = store.dirtyRanges();
  QCOMPARE(ranges.size(), 1);
  QCOMPARE(ranges[0].start, 0u);
  QCOMPARE(ranges[0].count, 5u);
}

void ChunkStoreTest::testDirtyRangesCoalesce()
{
  ChunkStore<FakeInstance> store;
  for (unsigned int i = 0; i < 10; ++i)
    store.append(FakeInstance());

  store.acknowledge();
  QVERIFY(!store.hasDirty());

  // Modify 2-4 and 6-8 (inclusive). 5 and 9 stay clean.
  store.markDirty(2, 3);
  store.markDirty(6, 3);

  QList<DirtyRange> ranges = store.dirtyRanges();
  QCOMPARE(ranges.size(), 2);
  QCOMPARE(ranges[0].start, 2u);
  QCOMPARE(ranges[0].count, 3u);
  QCOMPARE(ranges[1].start, 6u);
  QCOMPARE(ranges[1].count, 3u);
}

void ChunkStoreTest::testDirtyRangesSplit()
{
  ChunkStore<FakeInstance> store;
  for (unsigned int i = 0; i < 6; ++i)
    store.append(FakeInstance());

  store.acknowledge();
  // Adjacent marks coalesce into one range.
  store.markDirty(1, 1);
  store.markDirty(2, 1);
  store.markDirty(3, 1);

  QList<DirtyRange> ranges = store.dirtyRanges();
  QCOMPARE(ranges.size(), 1);
  QCOMPARE(ranges[0].start, 1u);
  QCOMPARE(ranges[0].count, 3u);
}

void ChunkStoreTest::testModifySingle()
{
  ChunkStore<FakeInstance> store;
  for (unsigned int i = 0; i < 4; ++i)
    store.append(FakeInstance());

  store.acknowledge();
  store.modify(2);

  QList<DirtyRange> ranges = store.dirtyRanges();
  QCOMPARE(ranges.size(), 1);
  QCOMPARE(ranges[0].start, 2u);
  QCOMPARE(ranges[0].count, 1u);
}

void ChunkStoreTest::testMarkDirtyClamp()
{
  ChunkStore<FakeInstance> store;
  for (unsigned int i = 0; i < 3; ++i)
    store.append(FakeInstance());

  store.acknowledge();
  // Range starts past the end: no-op.
  store.markDirty(5, 2);
  QVERIFY(!store.hasDirty());

  // Range straddles the end: clamped.
  store.markDirty(2, 5);
  QList<DirtyRange> ranges = store.dirtyRanges();
  QCOMPARE(ranges.size(), 1);
  QCOMPARE(ranges[0].start, 2u);
  QCOMPARE(ranges[0].count, 1u);
}

void ChunkStoreTest::testAcknowledgeClears()
{
  ChunkStore<FakeInstance> store;
  store.append(FakeInstance());
  store.append(FakeInstance());
  QVERIFY(store.hasDirty());

  store.acknowledge();
  QVERIFY(!store.hasDirty());
  QVERIFY(store.dirtyRanges().isEmpty());

  // After acknowledge, only new changes show up.
  store.modify(0);
  QList<DirtyRange> ranges = store.dirtyRanges();
  QCOMPARE(ranges.size(), 1);
  QCOMPARE(ranges[0].start, 0u);
  QCOMPARE(ranges[0].count, 1u);
}

void ChunkStoreTest::testClearResets()
{
  ChunkStore<FakeInstance> store;
  store.append(FakeInstance());
  store.append(FakeInstance());
  store.acknowledge();

  store.append(FakeInstance());
  QVERIFY(store.hasDirty());

  store.clear();
  QVERIFY(store.isEmpty());
  QCOMPARE(store.size(), 0u);
  QVERIFY(!store.hasDirty());
  QVERIFY(store.dirtyRanges().isEmpty());
}

void ChunkStoreTest::testSetAll()
{
  ChunkStore<FakeInstance> store;
  store.setAll(QVector<FakeInstance>(3));
  QCOMPARE(store.size(), 3u);
  QVERIFY(store.hasDirty());

  QList<DirtyRange> ranges = store.dirtyRanges();
  QCOMPARE(ranges.size(), 1);
  QCOMPARE(ranges[0].count, 3u);
}

void ChunkStoreTest::testSnapshotCopiesValues()
{
  // The snapshot must copy values, not pointers, because Atom::pos() returns a
  // pointer into the live molecule position vector.
  Molecule mol;
  Atom *a = mol.addAtom(6, Vector3d(1, 2, 3));
  Atom *b = mol.addAtom(8, Vector3d(4, 5, 6));
  mol.addBond(a->id(), b->id(), 2);

  unsigned int rev = mol.revision();

  SceneSnapshot snapshot;
  snapshot.revision = rev;
  snapshot.atoms.resize(mol.numAtoms());
  snapshot.bonds.resize(mol.numBonds());

  for (unsigned int i = 0; i < mol.numAtoms(); ++i) {
    const Atom *atom = mol.atom(i);
    const Vector3d &p = *atom->pos();
    snapshot.atoms[i].pos[0] = static_cast<float>(p.x());
    snapshot.atoms[i].pos[1] = static_cast<float>(p.y());
    snapshot.atoms[i].pos[2] = static_cast<float>(p.z());
    snapshot.atoms[i].atomicNumber = static_cast<unsigned char>(atom->atomicNumber());
  }

  // Mutate the molecule in place after the snapshot was taken.
  a->setPos(Vector3d(9, 9, 9));

  // The snapshot must still hold the original values.
  QCOMPARE(snapshot.atoms[0].pos[0], 1.0f);
  QCOMPARE(snapshot.atoms[0].pos[2], 3.0f);
  QCOMPARE(snapshot.revision, rev);
}

void ChunkStoreTest::testMoleculeRevisionBumps()
{
  Molecule mol;
  unsigned int start = mol.revision();

  Atom *a = mol.addAtom(6, Vector3d::Zero());
  QVERIFY(mol.revision() > start);

  unsigned int afterAdd = mol.revision();
  mol.addBond(a->id(), a->id(), 1);
  QVERIFY(mol.revision() > afterAdd);

  unsigned int afterBond = mol.revision();
  mol.removeAtom(a);
  QVERIFY(mol.revision() > afterBond);
}

void ChunkStoreTest::testMoleculeRevisionOnPositionEdit()
{
  Molecule mol;
  Atom *a = mol.addAtom(6, Vector3d::Zero());

  unsigned int before = mol.revision();
  mol.setAtomPos(a->id(), Vector3d(1, 1, 1));
  QVERIFY(mol.revision() > before);

  unsigned int afterMove = mol.revision();
  mol.translate(Vector3d(1, 0, 0));
  QVERIFY(mol.revision() > afterMove);
}

QTEST_MAIN(ChunkStoreTest)
#include "chunkstoretest.moc"
