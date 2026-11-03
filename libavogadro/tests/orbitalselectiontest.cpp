// fix-orbital-visualization task 3.5 (design D4): orbital selection always
// responds and renders when ready.
//
// The pure decision helper planOrbitalSelection is unit-tested directly. The
// pending-orbital lifecycle (latest click wins; pending cleared after
// completion) is exercised through test hooks on OrbitalExtension that expose
// the D4 m_pendingOrbital member and let the test drive the real
// calculationComplete() completion path headlessly (the full path would need a
// populated widget table and a live GLWidget, which are impractical in a
// headless binary). OrbitalExtension is compiled directly into this test
// because it is a plugin, not part of libavogadro.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QMetaObject>

#include "orbitalextension.h"
#include "orbitalwidget.h"
#include "orbitaltablemodel.h"

using namespace Avogadro;

class OrbitalSelectionTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void planCompletedRendersNow();
  void planQueuedRendersWhenReady();
  void planNeitherEnqueues();
  void latestSelectionWins();
  void pendingClearedAfterCompletion();
  void documentSwitchClearsPending();

private:
  // Build a widget whose table holds `n` orbitals (indices 0..n-1) so the
  // completion path's finishProgress/selectOrbital index safely.
  OrbitalWidget *makePopulatedWidget(int n);
};

void OrbitalSelectionTest::planCompletedRendersNow()
{
  QCOMPARE(static_cast<int>(OrbitalExtension::planOrbitalSelection(true, false)),
           static_cast<int>(OrbitalExtension::SelectionPlan::RenderNow));
  QCOMPARE(static_cast<int>(OrbitalExtension::planOrbitalSelection(true, true)),
           static_cast<int>(OrbitalExtension::SelectionPlan::RenderNow));
}

void OrbitalSelectionTest::planQueuedRendersWhenReady()
{
  QCOMPARE(static_cast<int>(OrbitalExtension::planOrbitalSelection(false, true)),
           static_cast<int>(OrbitalExtension::SelectionPlan::RenderWhenReady));
}

void OrbitalSelectionTest::planNeitherEnqueues()
{
  QCOMPARE(static_cast<int>(OrbitalExtension::planOrbitalSelection(false, false)),
           static_cast<int>(OrbitalExtension::SelectionPlan::EnqueueThenRender));
}

// Two selections of still-running orbitals: the last click must win.
void OrbitalSelectionTest::latestSelectionWins()
{
  OrbitalExtension ext;

  QList<calcInfo> queue;
  calcInfo c3;
  c3.orbital = 3;
  c3.resolution = 0.18;
  c3.isovalue = 0.02;
  c3.priority = 1;
  c3.state = Running;
  c3.posMesh = nullptr;
  c3.negMesh = nullptr;
  c3.cube = nullptr;
  queue.append(c3);
  calcInfo c5;
  c5.orbital = 5;
  c5.resolution = 0.18;
  c5.isovalue = 0.02;
  c5.priority = 1;
  c5.state = Running;
  c5.posMesh = nullptr;
  c5.negMesh = nullptr;
  c5.cube = nullptr;
  queue.append(c5);
  ext.setQueueForTest(queue);

  // Both are Running -> RenderWhenReady, which records the pending orbital.
  // renderOrbital is a private slot, so invoke it through the meta-object.
  QVERIFY(QMetaObject::invokeMethod(&ext, "renderOrbital", Qt::DirectConnection,
                                    Q_ARG(unsigned int, 3)));
  QVERIFY(QMetaObject::invokeMethod(&ext, "renderOrbital", Qt::DirectConnection,
                                    Q_ARG(unsigned int, 5)));

  QCOMPARE(ext.pendingOrbitalForTest(), 5u);
}

// Completing the pending orbital clears the pending marker (and would render
// it, but no GLWidget is current headlessly so renderCompletedOrbital no-ops).
void OrbitalSelectionTest::pendingClearedAfterCompletion()
{
  OrbitalWidget *widget = makePopulatedWidget(6);
  OrbitalExtension ext;
  ext.setWidgetForTest(widget);

  QList<calcInfo> queue;
  calcInfo c;
  c.orbital = 5;
  c.resolution = 0.18;
  c.isovalue = 0.02;
  c.priority = 0;
  c.state = Running;
  c.posMesh = nullptr;
  c.negMesh = nullptr;
  c.cube = nullptr;
  queue.append(c);

  // Mark orbital 5 pending and set up the running calculation.
  ext.setUpCompletionForTest(queue, /*runningIndex=*/0, /*pendingOrbital=*/5);
  // calculationComplete is a private slot, so invoke it through the
  // meta-object.
  QVERIFY(QMetaObject::invokeMethod(&ext, "calculationComplete",
                                    Qt::DirectConnection));

  // The pending marker is cleared once the orbital completes.
  QCOMPARE(ext.pendingOrbitalForTest(), 0u);

  delete widget;
}

// Task 7.3 row 5: switching documents while a parse/precalc is in flight must
// not leave a stale pending orbital (design D4: setMolecule clears
// m_pendingOrbital). setMolecule(nullptr) tears down the outgoing document's
// state and returns before loadBasis(), so it cleanly exercises the pending
// clear without needing a live basis file.
void OrbitalSelectionTest::documentSwitchClearsPending()
{
  OrbitalExtension ext;

  QList<calcInfo> queue;
  calcInfo c;
  c.orbital = 5;
  c.resolution = 0.18;
  c.isovalue = 0.02;
  c.priority = 0;
  c.state = Running;
  c.posMesh = nullptr;
  c.negMesh = nullptr;
  c.cube = nullptr;
  queue.append(c);
  ext.setUpCompletionForTest(queue, /*runningIndex=*/0, /*pendingOrbital=*/5);
  QCOMPARE(ext.pendingOrbitalForTest(), 5u);

  // Simulate a document switch: setMolecule(nullptr) quiesces the outgoing
  // document's work and clears the pending marker (the incoming document is
  // null, so it returns before any basis parse).
  ext.setMolecule(nullptr);
  QCOMPARE(ext.pendingOrbitalForTest(), 0u);
}

OrbitalWidget *OrbitalSelectionTest::makePopulatedWidget(int n)
{
  OrbitalWidget *w = new OrbitalWidget();
  QList<Orbital> list;
  for (int i = 0; i < n; ++i) {
    Orbital o;
    o.energy = 0.0;
    o.index = i;
    o.description = QString("MO %1").arg(i + 1);
    o.symmetry = QString();
    o.queueEntry = nullptr;
    o.min = 0;
    o.max = 0;
    o.current = 0;
    o.stage = 0;
    o.totalStages = 0;
    list.append(o);
  }
  w->fillTable(list);
  return w;
}

QTEST_MAIN(OrbitalSelectionTest)
#include "orbitalselectiontest.moc"
