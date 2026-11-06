/**********************************************************************
  EraseToolTest - Regression tests for the Erase tool lasso interaction

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 **********************************************************************/

#include "config.h"

#include "tools/erasetool.h"
#include "tools/drawcommand.h"

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/camera.h>
#include <avogadro/glwidget.h>
#include <avogadro/molecule.h>
#include <avogadro/undosequence.h>
#include <avogadro/undostack.h>

#include <Eigen/Core>

#include <QtTest>
#include <QMouseEvent>

#include <cmath>

using Eigen::Vector3d;

using namespace Avogadro;

namespace {

//! Portable pi constant. MSVC does not define M_PI unless _USE_MATH_DEFINES
//! is set; the repo's test convention (placementtest) uses an explicit constant
//! instead of relying on the platform macro.
const double kPi = 3.14159265358979323846;

//! Build a GLWidget bound to `mol` with a deterministic camera so atom
//! projection is reproducible. The widget is NOT shown (QOpenGLWidget cannot
//! present on the offscreen platform); the erase tool never uses GL picking, so
//! driving its mouse handlers directly against the camera projection is safe.
GLWidget *makeWidget(Molecule *mol)
{
  QSurfaceFormat fmt;
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);
  GLWidget *widget = new GLWidget(mol, fmt, nullptr);
  widget->resize(800, 600);

  // Frame the molecule deterministically: center it and pull back 8 world
  // units, the same convention as the autoopt repro test.
  Vector3d center = Vector3d::Zero();
  const QList<Atom *> atoms = mol->atoms();
  if (!atoms.isEmpty()) {
    for (Atom *a : atoms)
      center += *a->pos();
    center /= static_cast<double>(atoms.size());
  }
  Eigen::Projective3d mv = Eigen::Projective3d::Identity();
  mv.translate(-center);
  mv.translate(Vector3d(0.0, 0.0, -8.0));
  widget->camera()->setModelview(mv);
  widget->camera()->applyProjection();
  return widget;
}

//! Projected device-pixel position of an atom.
QPointF projPoint(GLWidget *widget, Atom *a)
{
  const Vector3d sp = widget->camera()->project(*a->pos());
  return QPointF(sp.x(), sp.y());
}

//! Build a circular lasso loop around the projected positions of `targets`.
//! The radius is the max projected distance from the centroid plus `pad`, so
//! the loop encloses every target; atoms projecting further away stay outside.
QVector<QPointF> loopAround(GLWidget *widget, const QList<Atom *> &targets,
                            double pad = 10.0)
{
  QList<QPointF> pts;
  QPointF c(0, 0);
  for (Atom *a : targets) {
    const QPointF p = projPoint(widget, a);
    pts << p;
    c += p;
  }
  c /= pts.size();

  double r = 0.0;
  for (const QPointF &p : pts) {
    const double dx = p.x() - c.x();
    const double dy = p.y() - c.y();
    r = std::max(r, std::sqrt(dx * dx + dy * dy));
  }
  r += pad;

  QVector<QPointF> loop;
  const int n = 24;
  for (int i = 0; i < n; ++i) {
    const double ang = 2.0 * kPi * i / n;
    loop << QPointF(c.x() + r * std::cos(ang), c.y() + r * std::sin(ang));
  }
  return loop;
}

//! Drive the erase tool through a press/move/release lasso over `loop` and
//! return the command (an UndoSequence) or nullptr for a no-op.
QUndoCommand *driveLasso(EraseTool &tool, GLWidget *widget,
                         const QVector<QPointF> &loop)
{
  const QPointF start = loop.first();
  QMouseEvent press(QEvent::MouseButtonPress, start, start,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  tool.mousePressEvent(widget, &press);

  for (int i = 1; i < loop.size(); ++i) {
    const QPointF p = loop[i];
    QMouseEvent move(QEvent::MouseMove, p, p,
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    tool.mouseMoveEvent(widget, &move);
  }

  // Release at the last path vertex. When the loop's last vertex does not
  // coincide with its first, the tool auto-closes it with a straight segment
  // (exercised by the unclosed-loop test).
  const QPointF end = loop.last();
  QMouseEvent release(QEvent::MouseButtonRelease, end, end,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  return tool.mouseReleaseEvent(widget, &release);
}

//! Cluster of four carbon atoms tightly grouped near the origin, plus a distant
//! carbon bonded to the cluster so the "one endpoint outside" bond is covered.
Molecule *makeClusterMolecule()
{
  Molecule *mol = new Molecule;
  Atom *a0 = mol->addAtom(6, Vector3d(0.0, 0.0, 0.0));
  Atom *a1 = mol->addAtom(6, Vector3d(0.8, 0.6, 0.0));
  Atom *a2 = mol->addAtom(6, Vector3d(-0.8, 0.6, 0.0));
  Atom *a3 = mol->addAtom(6, Vector3d(0.0, -0.9, 0.0));
  Atom *a4 = mol->addAtom(6, Vector3d(5.0, 0.0, 0.0)); // outside the lasso
  mol->addBond(a0, a1, 1);
  mol->addBond(a1, a2, 1);
  mol->addBond(a2, a3, 1);
  mol->addBond(a3, a0, 1);
  mol->addBond(a0, a4, 1); // bond crossing the lasso boundary
  return mol;
}

//! Methane-like carbon with three explicit hydrogens (managed when
//! adjust-hydrogens is on).
Molecule *makeMethane()
{
  Molecule *mol = new Molecule;
  Atom *c = mol->addAtom(6, Vector3d(0.0, 0.0, 0.0));
  Atom *h1 = mol->addAtom(1, Vector3d(1.0, 0.0, 0.0));
  Atom *h2 = mol->addAtom(1, Vector3d(-0.5, 0.87, 0.0));
  Atom *h3 = mol->addAtom(1, Vector3d(-0.5, -0.87, 0.0));
  mol->addBond(c, h1, 1);
  mol->addBond(c, h2, 1);
  mol->addBond(c, h3, 1);
  return mol;
}

} // namespace

class EraseToolTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void testPointInPolygon();
  void testContainmentDeletion();
  void testTapNoop();
  void testSingleUndo();
  void testUnclosedLoopAutoCloses();
  void testManagedHydrogenParity();
  void testInterruptedLasso();
  void testLockContentionAbort();
  void testOverlayPaint();
};

void EraseToolTest::testPointInPolygon()
{
  // Even-odd containment on a closed square.
  QVector<QPointF> square;
  square << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10) << QPointF(0, 10);
  QVERIFY(EraseTool::pointInPolygon(square, QPointF(5, 5)));
  QVERIFY(!EraseTool::pointInPolygon(square, QPointF(15, 5)));
  QVERIFY(!EraseTool::pointInPolygon(square, QPointF(5, 15)));
  QVERIFY(!EraseTool::pointInPolygon(square, QPointF(-5, 5)));

  // Degenerate: fewer than three vertices is never inside.
  QVector<QPointF> line;
  line << QPointF(0, 0) << QPointF(10, 0);
  QVERIFY(!EraseTool::pointInPolygon(line, QPointF(5, 5)));

  // Concave polygon (an L-shape) exercises the even-odd rule, not a naive
  // bounding-box test: the notch point is outside.
  QVector<QPointF> lshape;
  lshape << QPointF(0, 0) << QPointF(20, 0) << QPointF(20, 5) << QPointF(5, 5)
         << QPointF(5, 20) << QPointF(0, 20);
  QVERIFY(EraseTool::pointInPolygon(lshape, QPointF(3, 3)));
  // (10,10) lies in the notch (x>5, y>5), outside the L.
  QVERIFY(!EraseTool::pointInPolygon(lshape, QPointF(10, 10)));
}

void EraseToolTest::testContainmentDeletion()
{
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked); // delete every enclosed atom

  const QList<Atom *> atoms = mol->atoms();
  QList<Atom *> cluster;
  cluster << atoms[0] << atoms[1] << atoms[2] << atoms[3];

  const QVector<QPointF> loop = loopAround(widget, cluster, 10.0);
  // Sanity: the distant atom must project outside the loop.
  QVERIFY(!EraseTool::pointInPolygon(loop, projPoint(widget, atoms[4])));

  const int before = mol->numAtoms();
  UndoStack stack(mol);
  QUndoCommand *cmd = driveLasso(tool, widget, loop);
  QVERIFY(cmd != nullptr);
  stack.push(cmd);

  // The four cluster atoms are gone; the distant atom survives.
  QCOMPARE(mol->numAtoms(), before - 4);
  QCOMPARE(mol->numBonds(), 0);
  QCOMPARE(mol->atomById(atoms[4]->id()), atoms[4]);

  delete widget;
  delete mol;
}

void EraseToolTest::testTapNoop()
{
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);

  const int before = mol->numAtoms();
  UndoStack stack(mol);

  // Press and release at the same point: no significant travel, no delete.
  const QPointF pt(400, 300);
  QMouseEvent press(QEvent::MouseButtonPress, pt, pt,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  tool.mousePressEvent(widget, &press);
  QMouseEvent release(QEvent::MouseButtonRelease, pt, pt,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QUndoCommand *cmd = tool.mouseReleaseEvent(widget, &release);
  QVERIFY(cmd == nullptr);
  QCOMPARE(mol->numAtoms(), before);
  QCOMPARE(stack.count(), 0);

  // A tiny twitch (below the minimum lasso length) is also a no-op.
  QPointF a(400, 300), b(404, 300);
  QMouseEvent press2(QEvent::MouseButtonPress, a, a,
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  tool.mousePressEvent(widget, &press2);
  QMouseEvent move2(QEvent::MouseMove, b, b,
                    Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  tool.mouseMoveEvent(widget, &move2);
  QMouseEvent release2(QEvent::MouseButtonRelease, b, b,
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QUndoCommand *cmd2 = tool.mouseReleaseEvent(widget, &release2);
  QVERIFY(cmd2 == nullptr);
  QCOMPARE(mol->numAtoms(), before);
  QCOMPARE(stack.count(), 0);

  delete widget;
  delete mol;
}

void EraseToolTest::testSingleUndo()
{
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);

  const QList<Atom *> atoms = mol->atoms();
  QList<Atom *> cluster;
  cluster << atoms[0] << atoms[1] << atoms[2] << atoms[3];
  const QVector<QPointF> loop = loopAround(widget, cluster, 10.0);

  UndoStack stack(mol);
  QUndoCommand *cmd = driveLasso(tool, widget, loop);
  QVERIFY(cmd != nullptr);
  stack.push(cmd);

  QCOMPARE(mol->numAtoms(), 1); // only the distant atom survives

  // One undo restores the whole lasso (all four atoms and their bonds).
  stack.undo();
  QCOMPARE(mol->numAtoms(), 5);
  QCOMPARE(mol->numBonds(), 5);
  // The restored cluster atoms are present again.
  QVERIFY(mol->atomById(atoms[0]->id()) != nullptr);
  QVERIFY(mol->atomById(atoms[1]->id()) != nullptr);
  QVERIFY(mol->atomById(atoms[2]->id()) != nullptr);
  QVERIFY(mol->atomById(atoms[3]->id()) != nullptr);

  // One redo deletes them again.
  stack.redo();
  QCOMPARE(mol->numAtoms(), 1);
  QCOMPARE(mol->numBonds(), 0);

  delete widget;
  delete mol;
}

void EraseToolTest::testUnclosedLoopAutoCloses()
{
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);

  const QList<Atom *> atoms = mol->atoms();
  QList<Atom *> cluster;
  cluster << atoms[0] << atoms[1] << atoms[2] << atoms[3];
  QVector<QPointF> loop = loopAround(widget, cluster, 10.0);

  // Drop the last vertex so the path does not return to its start: the tool
  // must auto-close it with a straight segment from end to start. The gap is
  // kept small so the auto-closed shape still encloses the whole cluster.
  const QVector<QPointF> unclosed = loop.mid(0, loop.size() - 1);

  const int before = mol->numAtoms();
  UndoStack stack(mol);
  QUndoCommand *cmd = driveLasso(tool, widget, unclosed);
  QVERIFY(cmd != nullptr);
  stack.push(cmd);

  QCOMPARE(mol->numAtoms(), before - 4);
  QCOMPARE(mol->numBonds(), 0);

  delete widget;
  delete mol;
}

void EraseToolTest::testManagedHydrogenParity()
{
  // With adjust-hydrogens enabled: looping a carbon whose hydrogens are
  // managed deletes the carbon and removes its hydrogens by hydrogen
  // adjustment, not as independent lasso victims.
  {
    Molecule *mol = makeMethane();
    GLWidget *widget = makeWidget(mol);
    EraseTool tool;
    tool.setAddHydrogens(Qt::Checked); // default policy
    const QList<Atom *> atoms = mol->atoms();
    const QVector<QPointF> loop = loopAround(widget, atoms, 10.0);

    UndoStack stack(mol);
    QUndoCommand *cmd = driveLasso(tool, widget, loop);
    QVERIFY(cmd != nullptr);
    stack.push(cmd);

    // The carbon and all three hydrogens are gone in one step.
    QCOMPARE(mol->numAtoms(), 0);
    QCOMPARE(mol->numBonds(), 0);

    delete widget;
    delete mol;
  }

  // With adjust-hydrogens disabled: every enclosed atom, hydrogen or not, is a
  // deletion victim.
  {
    Molecule *mol = makeMethane();
    GLWidget *widget = makeWidget(mol);
    EraseTool tool;
    tool.setAddHydrogens(Qt::Unchecked);
    const QList<Atom *> atoms = mol->atoms();
    const QVector<QPointF> loop = loopAround(widget, atoms, 10.0);

    UndoStack stack(mol);
    QUndoCommand *cmd = driveLasso(tool, widget, loop);
    QVERIFY(cmd != nullptr);
    stack.push(cmd);

    QCOMPARE(mol->numAtoms(), 0);
    QCOMPARE(mol->numBonds(), 0);

    // P1 guard (adjust-OFF): one undo restores the methane exactly. A hydrogen
    // victim must be re-added only after its heavy-atom neighbor is restored,
    // otherwise its C-H bond is created against a missing atom (null-ended).
    stack.undo();
    QCOMPARE(mol->numAtoms(), 4);
    QCOMPARE(mol->numBonds(), 3);

    delete widget;
    delete mol;
  }
}

void EraseToolTest::testInterruptedLasso()
{
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);

  const int before = mol->numAtoms();
  UndoStack stack(mol);

  // Start a lasso, draw part of the loop...
  const QPointF start(300, 300);
  QMouseEvent press(QEvent::MouseButtonPress, start, start,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  tool.mousePressEvent(widget, &press);
  QMouseEvent move(QEvent::MouseMove, QPointF(400, 300), QPointF(400, 300),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  tool.mouseMoveEvent(widget, &move);

  // ...then switch tools mid-loop (the ToolGroup emits toolActivated).
  tool.toolActivated(nullptr);
  QCOMPARE(mol->numAtoms(), before);

  // A release after the abandon produces no command (not dragging).
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(400, 300),
                      QPointF(400, 300), Qt::LeftButton, Qt::NoButton,
                      Qt::NoModifier);
  QUndoCommand *abandoned = tool.mouseReleaseEvent(widget, &release);
  QVERIFY(abandoned == nullptr);
  QCOMPARE(mol->numAtoms(), before);
  QCOMPARE(stack.count(), 0);

  // The next gesture starts clean: a fresh lasso deletes only what it
  // encloses, with no points carried over from the abandoned path.
  const QList<Atom *> atoms = mol->atoms();
  QList<Atom *> cluster;
  cluster << atoms[0] << atoms[1] << atoms[2] << atoms[3];
  const QVector<QPointF> loop = loopAround(widget, cluster, 10.0);
  QUndoCommand *fresh = driveLasso(tool, widget, loop);
  QVERIFY(fresh != nullptr);
  stack.push(fresh);
  QCOMPARE(mol->numAtoms(), before - 4);

  delete widget;
  delete mol;
}

void EraseToolTest::testLockContentionAbort()
{
  // D6: if the molecule write lock cannot be acquired at release (e.g. held by
  // an optimization worker for a tick), the gesture aborts cleanly with no
  // command, no mutation, and no transient state; the next gesture is fresh.
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);

  const int before = mol->numAtoms();
  UndoStack stack(mol);
  const QList<Atom *> atoms = mol->atoms();
  QList<Atom *> cluster;
  cluster << atoms[0] << atoms[1] << atoms[2] << atoms[3];
  const QVector<QPointF> loop = loopAround(widget, cluster, 10.0);

  // Hold the write lock so the release's tryLockForWrite() fails.
  mol->lock()->lockForWrite();
  QUndoCommand *cmd = driveLasso(tool, widget, loop);
  mol->lock()->unlock();
  QVERIFY(cmd == nullptr);
  QCOMPARE(mol->numAtoms(), before);
  QCOMPARE(stack.count(), 0);

  // Once the lock is free, the next gesture is a fresh lasso.
  QUndoCommand *fresh = driveLasso(tool, widget, loop);
  QVERIFY(fresh != nullptr);
  stack.push(fresh);
  QCOMPARE(mol->numAtoms(), before - 4);

  delete widget;
  delete mol;
}

void EraseToolTest::testOverlayPaint()
{
  // The in-progress loop renders through Tool::paint() on every tier. A null
  // painter (degraded context) must be handled with an early return so the
  // overlay pass never emits a "painter not active" diagnostic or crashes.
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);

  // No active lasso: paint is a no-op.
  QVERIFY(tool.paint(widget));

  // Start a lasso with a real path, then paint. This must not crash and must
  // not touch the molecule.
  const QList<Atom *> atoms = mol->atoms();
  QList<Atom *> cluster;
  cluster << atoms[0] << atoms[1] << atoms[2] << atoms[3];
  const QVector<QPointF> loop = loopAround(widget, cluster, 10.0);
  QMouseEvent press(QEvent::MouseButtonPress, loop.first(), loop.first(),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  tool.mousePressEvent(widget, &press);
  QMouseEvent move(QEvent::MouseMove, loop[5], loop[5],
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  tool.mouseMoveEvent(widget, &move);

  QVERIFY(tool.paint(widget));
  QCOMPARE(mol->numAtoms(), 5); // molecule untouched by the overlay pass

  delete widget;
  delete mol;
}

QTEST_MAIN(EraseToolTest)
#include "erasetooltest.moc"
