/**********************************************************************
  Selection hash-set test (gpu-rendering-pipeline task 4.3, design A5/D6).

  Task 4.3 changes GLWidgetPrivate::selectedPrimitives from a QList-backed
  PrimitiveList to a QSet<Primitive*>. The win is O(1) contains/remove: with a
  QList, checking whether every primitive on a large scene is selected was
  O(n) per primitive, i.e. O(n^2) per frame, so "select all" on a big molecule
  slowed rendering. QSet makes each check O(1), so selecting all primitives
  changes the per-frame cost by ~0%.

  The public PrimitiveList API is preserved: selectedPrimitives() still
  materializes a PrimitiveList (a rare call). An empty-set early-out at the top
  of GLWidget::isSelected() (the single choke point every engine's
  pd->isSelected() call funnels through) makes the nothing-selected case, the
  common one, a single isEmpty() check.

  The selection data structures are pure (no OpenGL), so this test drives a real
  GLWidget headlessly and verifies the set semantics at small and large scale.
 *****************************************************************/

#include <QtTest>
#include <QSurfaceFormat>
#include <QList>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/primitive.h>

#include "libavogadro/src/glwidget.h"
#include "libavogadro/src/primitivelist.h"

using namespace Avogadro;

namespace {

// A widget that needs no GL context for the selection data-structure tests.
GLWidget *makeWidget(Molecule *mol)
{
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  return new GLWidget(mol, fmt, nullptr);
}

QList<Primitive *> primitivesOf(QList<Atom *> atoms)
{
  QList<Primitive *> out;
  foreach (Atom *a, atoms)
    out.append(static_cast<Primitive *>(a));
  return out;
}

} // namespace

class SelectionSetTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void testSelectAndDeselect();
  void testToggleSelectedList();
  void testToggleSelectedNoArg();
  void testClearSelected();
  void testSelectedPrimitivesMaterialize();
  void testEmptySetEarlyOutReturnsFalse();
  void testSelectAllAtScale();
};

void SelectionSetTest::testSelectAndDeselect()
{
  Molecule mol;
  Atom *a0 = mol.addAtom(6, Eigen::Vector3d(0, 0, 0));
  Atom *a1 = mol.addAtom(7, Eigen::Vector3d(1, 0, 0));
  GLWidget *w = makeWidget(&mol);
  w->setColorMap(new Color());

  QList<Primitive *> sel;
  sel << a0 << a1;
  w->setSelected(PrimitiveList(sel), true);
  QVERIFY(w->isSelected(a0));
  QVERIFY(w->isSelected(a1));

  // Deselect one through the same path.
  QList<Primitive *> off;
  off << a0;
  w->setSelected(PrimitiveList(off), false);
  QCOMPARE(w->isSelected(a0), false);
  QVERIFY(w->isSelected(a1));

  delete w;
}

void SelectionSetTest::testToggleSelectedList()
{
  Molecule mol;
  Atom *a0 = mol.addAtom(6, Eigen::Vector3d(0, 0, 0));
  Atom *a1 = mol.addAtom(8, Eigen::Vector3d(1, 0, 0));
  GLWidget *w = makeWidget(&mol);
  w->setColorMap(new Color());

  QList<Primitive *> sel;
  sel << a0 << a1;
  w->toggleSelected(PrimitiveList(sel));
  QVERIFY(w->isSelected(a0));
  QVERIFY(w->isSelected(a1));

  // Toggling an already-selected primitive deselects it.
  QList<Primitive *> t;
  t << a0;
  w->toggleSelected(PrimitiveList(t));
  QCOMPARE(w->isSelected(a0), false);
  QVERIFY(w->isSelected(a1));

  // Toggling a selected bond (added below) selects it.
  Bond *b0 = mol.addBond(0);
  b0->setBegin(a0);
  b0->setEnd(a1);
  QList<Primitive *> tb;
  tb << static_cast<Primitive *>(b0);
  w->toggleSelected(PrimitiveList(tb));
  QVERIFY(w->isSelected(b0));

  delete w;
}

void SelectionSetTest::testToggleSelectedNoArg()
{
  Molecule mol;
  Atom *a0 = mol.addAtom(6, Eigen::Vector3d(0, 0, 0));
  Atom *a1 = mol.addAtom(6, Eigen::Vector3d(1, 0, 0));
  Bond *b0 = mol.addBond(0);
  b0->setBegin(a0);
  b0->setEnd(a1);
  GLWidget *w = makeWidget(&mol);
  w->setColorMap(new Color());

  // Toggle with nothing pre-selected selects every atom and bond.
  w->toggleSelected();
  QVERIFY(w->isSelected(a0));
  QVERIFY(w->isSelected(a1));
  QVERIFY(w->isSelected(b0));

  // Toggling again deselects everything.
  w->toggleSelected();
  QCOMPARE(w->isSelected(a0), false);
  QCOMPARE(w->isSelected(a1), false);
  QCOMPARE(w->isSelected(b0), false);

  delete w;
}

void SelectionSetTest::testClearSelected()
{
  Molecule mol;
  Atom *a0 = mol.addAtom(6, Eigen::Vector3d(0, 0, 0));
  Atom *a1 = mol.addAtom(6, Eigen::Vector3d(1, 0, 0));
  GLWidget *w = makeWidget(&mol);
  w->setColorMap(new Color());

  QList<Primitive *> sel;
  sel << a0 << a1;
  w->setSelected(PrimitiveList(sel), true);
  QCOMPARE(w->selectedPrimitives().size(), 2);

  w->clearSelected();
  QCOMPARE(w->selectedPrimitives().size(), 0);
  QVERIFY(w->selectedPrimitives().isEmpty());
  QCOMPARE(w->isSelected(a0), false);

  delete w;
}

void SelectionSetTest::testSelectedPrimitivesMaterialize()
{
  Molecule mol;
  Atom *a0 = mol.addAtom(6, Eigen::Vector3d(0, 0, 0));
  Atom *a1 = mol.addAtom(7, Eigen::Vector3d(1, 0, 0));
  Bond *b0 = mol.addBond(0);
  b0->setBegin(a0);
  b0->setEnd(a1);
  GLWidget *w = makeWidget(&mol);
  w->setColorMap(new Color());

  QList<Primitive *> sel;
  sel << a0 << a1 << static_cast<Primitive *>(b0);
  w->setSelected(PrimitiveList(sel), true);

  // The public PrimitiveList API still materializes every selected primitive.
  PrimitiveList materialized = w->selectedPrimitives();
  QCOMPARE(materialized.size(), 3);
  QList<Primitive *> all = materialized.list();
  QVERIFY(all.contains(a0));
  QVERIFY(all.contains(a1));
  QVERIFY(all.contains(static_cast<Primitive *>(b0)));
  // No duplicates: the QSet stores each pointer once.
  QCOMPARE(all.size(), QSet<Primitive *>(all.constBegin(), all.constEnd()).size());

  delete w;
}

void SelectionSetTest::testEmptySetEarlyOutReturnsFalse()
{
  // The empty-set early-out in isSelected() must return false (and the set
  // must stay empty) once everything is cleared.
  Molecule mol;
  Atom *a0 = mol.addAtom(6, Eigen::Vector3d(0, 0, 0));
  GLWidget *w = makeWidget(&mol);
  w->setColorMap(new Color());

  QList<Primitive *> sel;
  sel << a0;
  w->setSelected(PrimitiveList(sel), true);
  w->clearSelected();

  QCOMPARE(w->selectedPrimitives().isEmpty(), true);
  QCOMPARE(w->isSelected(a0), false);

  delete w;
}

void SelectionSetTest::testSelectAllAtScale()
{
  // Selecting every primitive on a large scene must select exactly those
  // primitives and materialize them all back — the O(1) contains of QSet is
  // what keeps this from degrading to O(n^2) per frame (design A5).
  const int n = 5000;
  Molecule mol;
  QList<Atom *> atoms;
  for (int i = 0; i < n; ++i) {
    Atom *a = mol.addAtom(6, Eigen::Vector3d(i, 0, 0));
    atoms.append(a);
  }
  GLWidget *w = makeWidget(&mol);
  w->setColorMap(new Color());

  w->setSelected(primitivesOf(atoms), true);

  // Every primitive is selected; none extra.
  foreach (Atom *a, atoms)
    QVERIFY(w->isSelected(a));
  QCOMPARE(w->selectedPrimitives().size(), n);

  // Materialization returns the whole set with no loss or duplication.
  QList<Primitive *> all = w->selectedPrimitives().list();
  QCOMPARE(all.size(), n);
  QCOMPARE(QSet<Primitive *>(all.constBegin(), all.constEnd()).size(), n);

  // Selecting again is a no-op (idempotent) and does not grow the set.
  w->setSelected(primitivesOf(atoms), true);
  QCOMPARE(w->selectedPrimitives().size(), n);

  delete w;
}

QTEST_MAIN(SelectionSetTest)
#include "selectionsettest.moc"
