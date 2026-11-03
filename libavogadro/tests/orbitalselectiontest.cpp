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
#include <QThread>

#include "orbitalextension.h"
#include "orbitalwidget.h"
#include "orbitaltablemodel.h"

#include <avogadro/moleculefile.h>
#include <avogadro/molecule.h>

#include <openqube/basisset.h>

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
  // fix-orbital-watcher-affinity D3: drive the real async basis-load path.
  void asyncLoadBasisIsGuiAffine();
  void asyncLoadOrcaCompletesQueue();
  void asyncLoadFchkCompletesQueue();
  void asyncLoadMoldenCompletesQueue();
  // Progress-supervisor/oracle: drive the real production selection chain
  // (OrbitalWidget::selectOrbital -> selectionChanged -> tableClicked ->
  // orbitalSelected -> renderOrbital) in-process, no coordinates needed.
  void tableSelectionFiresRenderPath();

private:
  // Build a widget whose table holds `n` orbitals (indices 0..n-1) so the
  // completion path's finishProgress/selectOrbital index safely.
  OrbitalWidget *makePopulatedWidget(int n);
  // Load a real molecule from `filePath` and drive the real async basis-load
  // path (constructBasis on the GUI thread, fillBasis on a worker) by calling
  // setMolecule. Returns the MoleculeFile that owns the loaded molecule (the
  // caller keeps it alive), or null if the file cannot be read. On success
  // `outWidget` receives the populated widget, which the caller must delete.
  MoleculeFile *loadMoleculeAndDrive(const QString &filePath,
                                     OrbitalExtension &ext, int tableRows,
                                     OrbitalWidget **outWidget = nullptr);
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

// Load a real molecule from `filePath` and drive the real async basis-load
// path (constructBasis on the GUI thread, fillBasis on a QtConcurrent worker)
// by calling setMolecule. The caller keeps the returned MoleculeFile alive so
// the loaded Molecule (owned by it) stays valid.
MoleculeFile *OrbitalSelectionTest::loadMoleculeAndDrive(
    const QString &filePath, OrbitalExtension &ext, int tableRows,
    OrbitalWidget **outWidget)
{
  MoleculeFile *mf = MoleculeFile::readFile(filePath.toUtf8().constData());
  if (!mf)
    return nullptr;
  Molecule *mol = mf->molecule(0);
  if (!mol)
    return nullptr;
  mol->setFileName(filePath);
  OrbitalWidget *w = makePopulatedWidget(tableRows);
  ext.setWidgetForTest(w);
  // dockWidget() wires orbitalSelected -> renderOrbital, but the test sets the
  // widget directly (bypassing dockWidget), so replicate that connection here
  // so the real selection->render chain is exercised.
  QObject::connect(w, SIGNAL(orbitalSelected(unsigned int)),
                   &ext, SLOT(renderOrbital(unsigned int)));
  if (outWidget)
    *outWidget = w;
  ext.setMolecule(mol);
  return mf;
}

// fix-orbital-watcher-affinity D3.1: after the async load continuation runs
// for koffein_orca.out, the basis object (and its internal QFutureWatcher) is
// GUI-affine. This alone would have caught the regression where the basis was
// constructed on a QtConcurrent pool thread and its finished() callout was
// swallowed by the pool thread's lack of an event loop.
void OrbitalSelectionTest::asyncLoadBasisIsGuiAffine()
{
  const QString filePath = QString::fromUtf8(TESTDATADIR) + "koffein_orca.out";
  OrbitalExtension ext;
  OrbitalWidget *widget = nullptr;
  MoleculeFile *mf = loadMoleculeAndDrive(filePath, ext, 256, &widget);
  QVERIFY(mf);
  QThread *guiThread = QThread::currentThread();

  // The async continuation is queued to the GUI thread; QTRY spins the event
  // loop until it has run and set m_basis.
  QTRY_VERIFY_WITH_TIMEOUT(ext.basisForTest() != nullptr, 30000);

  // D3.1: the basis must be constructed on the GUI thread, so its thread()
  // equals the dispatching (GUI) thread.
  QCOMPARE(ext.basisForTest()->thread(), guiThread);

  // Quiesce the in-flight precalculation so the extension destructs cleanly.
  ext.setMolecule(nullptr);
  delete widget;
  delete mf;
}

// fix-orbital-watcher-affinity D3.2: the real async load path drains the
// calculation queue to a completed entry without any document switch — the
// no-wedge observable. With the fix the watcher is GUI-affine and delivers
// finished(), so the pipeline advances; with the fix reverted the first
// calculation never completes and this times out.
void OrbitalSelectionTest::asyncLoadOrcaCompletesQueue()
{
  const QString filePath = QString::fromUtf8(TESTDATADIR) + "koffein_orca.out";
  OrbitalExtension ext;
  OrbitalWidget *widget = nullptr;
  MoleculeFile *mf = loadMoleculeAndDrive(filePath, ext, 256, &widget);
  QVERIFY(mf);

  QTRY_VERIFY_WITH_TIMEOUT(ext.anyCompletedForTest(), 60000);
  ext.setMolecule(nullptr);
  delete widget;
  delete mf;
}

// fix-orbital-watcher-affinity D3.2: Gaussian fchk format goes through the
// same split (constructBasis -> GaussianSet, fillBasis -> GaussianFchk).
void OrbitalSelectionTest::asyncLoadFchkCompletesQueue()
{
  const QString filePath = QString::fromUtf8(TESTDATADIR) + "benzene.fchk";
  OrbitalExtension ext;
  OrbitalWidget *widget = nullptr;
  MoleculeFile *mf = loadMoleculeAndDrive(filePath, ext, 64, &widget);
  QVERIFY(mf);

  QTRY_VERIFY_WITH_TIMEOUT(ext.anyCompletedForTest(), 60000);
  ext.setMolecule(nullptr);
  delete widget;
  delete mf;
}

// fix-orbital-watcher-affinity D3.3: molden format goes through the same split
// (constructBasis -> GaussianSet, fillBasis -> MoldenFile).
void OrbitalSelectionTest::asyncLoadMoldenCompletesQueue()
{
  const QString filePath =
      QString::fromUtf8(TESTDATADIR) + "koffein_orca.molden";
  OrbitalExtension ext;
  OrbitalWidget *widget = nullptr;
  MoleculeFile *mf = loadMoleculeAndDrive(filePath, ext, 256, &widget);
  QVERIFY(mf);

  QTRY_VERIFY_WITH_TIMEOUT(ext.anyCompletedForTest(), 60000);
  ext.setMolecule(nullptr);
  delete widget;
  delete mf;
}

// Progress-supervisor/oracle discriminating test: the real production
// selection chain in-process. OrbitalWidget::selectOrbital() is public and
// drives the identical selectionModel()->select() -> selectionChanged ->
// tableClicked -> orbitalSelected -> renderOrbital chain that a mouse click
// fires, so this verifies the click-convergent render path with real meshes and
// no X11 hit-testing. Orbital 1 is NOT in the precalc set (koffein_orca.out
// precalcs 47-56), so renderOrbital(1) takes the EnqueueThenRender branch
// (priority-0 user calc) and marks it pending; when that calc completes,
// calculationComplete() clears pending and calls selectOrbital(1) again, so
// orbitalSelected fires a second time. renderCompletedOrbital bails gracefully
// headless (no current GLWidget).
void OrbitalSelectionTest::tableSelectionFiresRenderPath()
{
  const QString filePath = QString::fromUtf8(TESTDATADIR) + "koffein_orca.out";
  OrbitalExtension ext;
  OrbitalWidget *widget = nullptr;
  MoleculeFile *mf = loadMoleculeAndDrive(filePath, ext, 256, &widget);
  QVERIFY(mf);
  QTRY_VERIFY_WITH_TIMEOUT(ext.anyCompletedForTest(), 60000); // real meshes done

  QSignalSpy spy(widget, SIGNAL(orbitalSelected(unsigned int)));
  widget->selectOrbital(1); // orbital 1 is not in the precalc set (47-56)

  QCOMPARE(spy.count(), 1); // selectionChanged -> tableClicked fired
  QCOMPARE(ext.pendingOrbitalForTest(), 1u); // renderOrbital ran (EnqueueThenRender)

  // The priority-0 user-requested calc completes -> calculationComplete clears
  // pending and calls selectOrbital(1) again (spy -> 2); renderCompletedOrbital
  // bails gracefully headless (no GLWidget).
  QTRY_VERIFY_WITH_TIMEOUT(ext.pendingOrbitalForTest() == 0, 30000);
  QCOMPARE(spy.count(), 2);
  ext.setMolecule(nullptr);
  delete widget;
  delete mf;
}

QTEST_MAIN(OrbitalSelectionTest)
#include "orbitalselectiontest.moc"
