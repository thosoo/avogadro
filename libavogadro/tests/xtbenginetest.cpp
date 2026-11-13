#include "config.h"
#include <QtTest>

#include "xtb/xtbapi.h"
#include "xtb/xtbengine.h"
#include "xtb/xtbparams.h"

#include "molecule.h"
#include "atom.h"

#include <QSignalSpy>
#include <QTimer>

#include <cmath>
#include <vector>

using Avogadro::XtbApi;
using Avogadro::XtbEngine;
using Avogadro::Molecule;
using Avogadro::Atom;

// Forward declarations: these helpers are defined below the tests that use
// them (they build a live molecule the engine reads on its per-tick re-baseline).
static Molecule* makeMoleculeFrom(const std::vector<int>& numbers,
                                  const std::vector<double>& positionsAng);
static Molecule* makeWaterMolecule();
static Molecule* makeDistortedWaterMolecule();

class XtbEngineTest : public QObject
{
  Q_OBJECT
private slots:
  void setupUnavailableIsHarmless();
  void waterSetupSucceeds();
  void unsupportedElementFailsSetupOnce();
  void singlepointWaterEnergyNegative();
  void coordsReadyRoundTrips();
  void waterConvergesToReferenceGeometry();
  void convergedStopsEvaluations();
  void dragAfterConvergenceResumesEvaluation();
  void draggedAtomPinnedDuringOptimization();
  void dragReleaseResumesCoverage();
  void pressWithoutPositionDoesNotCrash();
  void interactiveTogglesAccuracy();
  void threadsEnvAutoRespectsClamp();
  void gfnffWaterSetupWhenParamsAvailable();
  void gfnffWithoutParamsFailsDeterministically();
  void immediateStopIsSafe();
  void moleculeSourceAddAtomReestablishesOnce();
  void moleculeSourceRemoveAtomSurvivorsPublish();
  void moleculeSourceElementChangeEvaluated();
  void moleculeSourceExternalMoveAdoptsFreshPhase();
  void moleculeSourceUnsupportedElementLatchOnce();
  void failedEvaluationPublishesNothingAndLatches();
};

// Water geometry in angstrom: O at origin, H1 at (0,0,1.0), H2 at (0,0.1,-1.0).
static std::vector<int> waterNumbers()
{
  return { 8, 1, 1 };
}

static std::vector<double> waterPositionsAng()
{
  return { 0.0, 0.0, 0.0,
           0.0, 0.0, 1.0,
           0.0, 0.1, -1.0 };
}

// Distorted water for the convergence test: O at origin, H1 at (0,0,1.3) Å,
// H2 at (0, 0.1, -0.95) Å.
static std::vector<double> waterPositionsDistortedAng()
{
  return { 0.0, 0.0, 0.0,
           0.0, 0.0, 1.3,
           0.0, 0.1, -0.95 };
}

// Cleanly quiesce the engine so run() (if it ran) deletes the xTB objects.
static void stopAndWait(XtbEngine& engine)
{
  if (!engine.isRunning()) {
    engine.start(QThread::LowPriority);
  }
  engine.stop();
  engine.wait(10000);
}

void XtbEngineTest::setupUnavailableIsHarmless()
{
  if (XtbApi::isAvailable()) {
    QSKIP("xTB runtime library present; absent-path test skipped");
  }
  XtbEngine engine;
  QSignalSpy failSpy(&engine, &XtbEngine::setupFailed);

  bool ok = engine.setup(waterNumbers(), waterPositionsAng(),
                         /*method=*/1 /* GFN2 */, /*level=*/4 /* normal */,
                         /*threads=*/0);
  QVERIFY(!ok);
  QCOMPARE(failSpy.count(), 0);
}

void XtbEngineTest::waterSetupSucceeds()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  XtbEngine engine;
  bool ok = engine.setup(waterNumbers(), waterPositionsAng(),
                         /*method=*/1 /* GFN2 */, /*level=*/4 /* normal */,
                         /*threads=*/0);
  QVERIFY(ok);
  stopAndWait(engine);
}

void XtbEngineTest::unsupportedElementFailsSetupOnce()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  // Es (99) is not supported by GFN2-xTB.
  std::vector<int> numbers = { 8, 99, 1 };
  std::vector<double> positions = { 0.0, 0.0, 0.0,
                                    0.0, 0.0, 1.0,
                                    0.0, 0.1, -1.0 };

  XtbEngine engine;
  QSignalSpy failSpy(&engine, &XtbEngine::setupFailed);

  bool ok = engine.setup(numbers, positions, /*method=*/1, /*level=*/4,
                         /*threads=*/0);
  QVERIFY(!ok);
  QCOMPARE(failSpy.count(), 1);

  // A second attempt after cleanup also fails exactly once (no retry leaks).
  bool ok2 = engine.setup(numbers, positions, /*method=*/1, /*level=*/4,
                          /*threads=*/0);
  QVERIFY(!ok2);
  QCOMPARE(failSpy.count(), 2);
}

void XtbEngineTest::singlepointWaterEnergyNegative()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                       /*level=*/4, /*threads=*/0));
  QSignalSpy progressSpy(&engine, &XtbEngine::progress);
  engine.start(QThread::LowPriority);
  engine.requestTick();

  QTRY_VERIFY_WITH_TIMEOUT(progressSpy.count() >= 1, 5000);
  QList<QVariant> args = progressSpy.first();
  int evaluation = args.at(0).toInt();
  double energy = args.at(2).toDouble();
  QVERIFY(evaluation >= 1);
  QVERIFY(energy < 0.0);

  engine.stop();
  engine.wait(10000);
}

void XtbEngineTest::coordsReadyRoundTrips()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                       /*level=*/4, /*threads=*/0));

  std::vector<double> lastCoords;
  bool gotCoords = false;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) {
                     lastCoords = c;
                     gotCoords = true;
                   });
  engine.start(QThread::LowPriority);
  engine.requestTick();

  QTRY_VERIFY_WITH_TIMEOUT(gotCoords, 5000);
  QCOMPARE(static_cast<int>(lastCoords.size()), 9);

  engine.stop();
  engine.wait(10000);
}

void XtbEngineTest::waterConvergesToReferenceGeometry()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  const double bohr2ang = 0.52917721092;

  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsDistortedAng(),
                       /*method=*/1, /*level=*/4, /*threads=*/0));

  std::vector<double> lastCoords;
  bool converged = false;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) { lastCoords = c; });
  QObject::connect(&engine, &XtbEngine::converged,
                   [&](double) { converged = true; });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout,
                   [&]() { engine.requestTick(); });
  tickTimer.start(30);

  QTRY_VERIFY_WITH_TIMEOUT(converged, 60000);
  tickTimer.stop();

  QCOMPARE(static_cast<int>(lastCoords.size()), 9);
  double o[3] = { lastCoords[0] * bohr2ang, lastCoords[1] * bohr2ang,
                  lastCoords[2] * bohr2ang };
  double h1[3] = { lastCoords[3] * bohr2ang, lastCoords[4] * bohr2ang,
                   lastCoords[5] * bohr2ang };
  double h2[3] = { lastCoords[6] * bohr2ang, lastCoords[7] * bohr2ang,
                   lastCoords[8] * bohr2ang };

  double d1 = 0.0, d2 = 0.0;
  double v1[3] = { h1[0] - o[0], h1[1] - o[1], h1[2] - o[2] };
  double v2[3] = { h2[0] - o[0], h2[1] - o[1], h2[2] - o[2] };
  for (int i = 0; i < 3; ++i) {
    d1 += v1[i] * v1[i];
    d2 += v2[i] * v2[i];
  }
  d1 = std::sqrt(d1);
  d2 = std::sqrt(d2);
  double n1 = 0.0, n2 = 0.0, dot = 0.0;
  for (int i = 0; i < 3; ++i) {
    n1 += v1[i] * v1[i];
    n2 += v2[i] * v2[i];
    dot += v1[i] * v2[i];
  }
  double angle =
    std::acos(dot / (std::sqrt(n1) * std::sqrt(n2))) * 180.0 / M_PI;

  QVERIFY(d1 > 0.86 && d1 < 1.06);
  QVERIFY(d2 > 0.86 && d2 < 1.06);
  QVERIFY(angle > 94.5 && angle < 114.5);

  engine.stop();
  engine.wait(10000);
}

void XtbEngineTest::convergedStopsEvaluations()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsDistortedAng(),
                       /*method=*/1, /*level=*/4, /*threads=*/0));

  QSignalSpy progressSpy(&engine, &XtbEngine::progress);
  bool converged = false;
  QObject::connect(&engine, &XtbEngine::converged,
                   [&](double) { converged = true; });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout,
                   [&]() { engine.requestTick(); });
  tickTimer.start(30);

  QTRY_VERIFY_WITH_TIMEOUT(converged, 60000);
  tickTimer.stop();

  int lastEval = 0;
  if (progressSpy.count() > 0) {
    lastEval = progressSpy.last().at(0).toInt();
  }
  QVERIFY(lastEval >= 1);

  // Pump extra ticks: the engine has stopped evaluating, so the counter must
  // not increase.
  for (int i = 0; i < 20; ++i) {
    engine.requestTick();
    QTest::qWait(10);
  }
  QCOMPARE(progressSpy.last().at(0).toInt(), lastEval);

  engine.stop();
  engine.wait(10000);
}

void XtbEngineTest::dragAfterConvergenceResumesEvaluation()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  const double bohr2ang = 0.52917721092;

  Molecule* mol = makeDistortedWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  std::vector<double> lastCoords;
  bool converged = false;
  int progressCount = 0;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) { lastCoords = c; });
  QObject::connect(&engine, &XtbEngine::progress,
                   [&](int, int, double) { ++progressCount; });
  QObject::connect(&engine, &XtbEngine::converged,
                   [&](double) { converged = true; });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout,
                   [&]() { engine.requestTick(); });
  tickTimer.start(30);

  QTRY_VERIFY_WITH_TIMEOUT(converged, 60000);
  const int progressAtConvergence = progressCount;
  QVERIFY(progressAtConvergence >= 1);

  // Idle ticks must not evaluate (the session stays open, CPU idle).
  QTest::qWait(200);
  QCOMPARE(progressCount, progressAtConvergence);

  // Pull a hydrogen after convergence: the session must resume evaluating
  // (pre-fix behavior: the worker had exited and the pull was unregistered,
  // forcing the user to restart the optimization). Write the pointer position
  // into the live molecule (drag authority, design D3) and mask the atom.
  const double pullZAng = 2.5;
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(1)->setPos(Eigen::Vector3d(0.0, 0.0, pullZAng));
  }
  engine.setDraggedAtom(1);

  QTRY_VERIFY_WITH_TIMEOUT(progressCount > progressAtConvergence, 15000);

  // The published snapshot must register the pull: H1 pinned at the pointer
  // position (drag authority / coordinate write-back).
  QTRY_VERIFY_WITH_TIMEOUT(lastCoords.size() == 9, 5000);
  const double h1z = lastCoords[5] * bohr2ang;
  QVERIFY(std::abs(h1z - pullZAng) < 1e-4);

  // Release: the optimizer resumes covering the atom and evaluations keep
  // advancing; the published H1 position leaves the pinned drag position as
  // relaxation begins.
  engine.setDraggedAtom(-1);
  const int progressAtRelease = progressCount;
  QTRY_VERIFY_WITH_TIMEOUT(progressCount > progressAtRelease, 15000);
  QTRY_VERIFY_WITH_TIMEOUT(std::abs(lastCoords[5] * bohr2ang - pullZAng) > 1e-3,
                           15000);

  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  delete mol;
}

void XtbEngineTest::draggedAtomPinnedDuringOptimization()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  const double bohr2ang = 0.52917721092;
  const double dragAngZ = 2.0;

  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  std::vector<double> lastCoords;
  int coordsCount = 0;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) {
                     lastCoords = c;
                     ++coordsCount;
                   });

  bool dragActive = false;
  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() {
    engine.requestTick();
    if (dragActive) {
      // Keep the dragged atom's position in the live molecule at the pointer
      // position (drag authority, design D3) so the worker reads it on its
      // per-tick re-baseline.
      QWriteLocker locker(mol->lock());
      mol->atoms().at(1)->setPos(Eigen::Vector3d(0.0, 0.0, dragAngZ));
    }
  });
  tickTimer.start(30);

  // Wait for at least one evaluation before grabbing.
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= 1, 5000);
  std::vector<double> preDrag = lastCoords;

  dragActive = true;
  engine.setDraggedAtom(1);
  // Write the pointer position into the molecule so the next tick adopts it.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(1)->setPos(Eigen::Vector3d(0.0, 0.0, dragAngZ));
  }

  // Wait for >= 3 further evaluations under the drag.
  int target = coordsCount + 3;
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= target, 30000);
  tickTimer.stop();

  // The dragged atom (index 1) is pinned at (0,0,2.0) A.
  double h1[3] = { lastCoords[3] * bohr2ang, lastCoords[4] * bohr2ang,
                   lastCoords[5] * bohr2ang };
  QVERIFY(std::abs(h1[0] - 0.0) < 1e-6);
  QVERIFY(std::abs(h1[1] - 0.0) < 1e-6);
  QVERIFY(std::abs(h1[2] - 2.0) < 1e-6);

  // The rest of the molecule relaxed: atoms 0 and/or 2 changed since the drag
  // began (they are not pinned).
  bool atom0Changed = std::abs(lastCoords[0] - preDrag[0]) > 1e-6 ||
                      std::abs(lastCoords[1] - preDrag[1]) > 1e-6 ||
                      std::abs(lastCoords[2] - preDrag[2]) > 1e-6;
  bool atom2Changed = std::abs(lastCoords[6] - preDrag[6]) > 1e-6 ||
                      std::abs(lastCoords[7] - preDrag[7]) > 1e-6 ||
                      std::abs(lastCoords[8] - preDrag[8]) > 1e-6;
  QVERIFY(atom0Changed || atom2Changed);

  engine.stop();
  engine.wait(10000);
  delete mol;
}

void XtbEngineTest::dragReleaseResumesCoverage()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  const double bohr2ang = 0.52917721092;
  const double dragAngZ = 2.0;

  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  std::vector<double> lastCoords;
  int coordsCount = 0;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) {
                     lastCoords = c;
                     ++coordsCount;
                   });

  bool dragActive = false;
  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() {
    engine.requestTick();
    if (dragActive) {
      // Keep the dragged atom's position in the live molecule at the pointer
      // position (drag authority, design D3) so the worker reads it on its
      // per-tick re-baseline.
      QWriteLocker locker(mol->lock());
      mol->atoms().at(1)->setPos(Eigen::Vector3d(0.0, 0.0, dragAngZ));
    }
  });
  tickTimer.start(30);

  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= 1, 5000);

  // Drag atom 1 to (0,0,2.0) A and hold it for >= 3 evaluations.
  dragActive = true;
  engine.setDraggedAtom(1);
  // Write the pointer position into the molecule so the next tick adopts it.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(1)->setPos(Eigen::Vector3d(0.0, 0.0, dragAngZ));
  }
  int pinnedTarget = coordsCount + 3;
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= pinnedTarget, 30000);
  double pinnedH1z = lastCoords[5] * bohr2ang;
  QVERIFY(std::abs(pinnedH1z - 2.0) < 1e-6);

  // Release the drag and let the optimizer cover atom 1 again.
  dragActive = false;
  engine.setDraggedAtom(-1);
  int releaseTarget = coordsCount + 3;
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= releaseTarget, 30000);
  tickTimer.stop();

  double releasedH1z = lastCoords[5] * bohr2ang;
  // The optimizer resumed covering atom 1, so it is no longer exactly pinned.
  QVERIFY(std::abs(releasedH1z - 2.0) > 1e-4);

  engine.stop();
  engine.wait(10000);
  delete mol;
}

// Design D1 regression: the cdb crash (evaluateOnce+0x18b, mov rax,[rbx] with
// rbx=0) is a read through an empty std::vector's null data pointer in the pin
// loop. It happens when a tick fires in the press->first-move window: the
// dragged index is set but no pointer position has been authored. This test
// reproduces that exact state (setDraggedAtom with no pointer position
// authored) and asserts the process survives and coordinates still publish.
void XtbEngineTest::pressWithoutPositionDoesNotCrash()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }

  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  std::vector<double> lastCoords;
  bool gotCoords = false;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) {
                     lastCoords = c;
                     gotCoords = true;
                   });

  engine.start(QThread::LowPriority);

  // Drive ticks with a timer (the worker only evaluates on requestTick).
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() { engine.requestTick(); });
  tickTimer.start(30);

  // Wait for at least one evaluation so the worker is actively ticking, then
  // reproduce the crash precondition: an index published with NO authored
  // position (the press->first-move window). Pre-fix, the next tick's pin loop
  // reads element 0 of the empty m_dragPositionBohr and faults. Now the
  // dragged atom's position comes from the live molecule, so a press before
  // the first pointer move reads the atom's pre-drag position (the safe value)
  // and never dereferences an empty vector.
  QTRY_VERIFY_WITH_TIMEOUT(gotCoords, 5000);
  engine.setDraggedAtom(0);

  // Pump several ticks across the empty-position window. The process must not
  // crash and coordinates must keep publishing (the session is not killed).
  QSignalSpy coordsSpy(&engine, &XtbEngine::coordsReady);
  for (int i = 0; i < 6; ++i) {
    QTest::qWait(30);
  }

  // No crash occurred (we reached here); coords still published after the
  // empty-position ticks.
  QTRY_VERIFY_WITH_TIMEOUT(coordsSpy.count() >= 1, 5000);
  QCOMPARE(static_cast<int>(lastCoords.size()), 9);

  // Once a pointer position IS authored, the drag promotes and the atom pins.
  // Use a moderate displacement (0.5 A): the extreme 2.0 A pull used by other
  // drag tests dissociates the geometry and now legitimately triggers the D4
  // SCF-failure containment (that behavior is covered by the containment test).
  const double bohr2ang = 0.52917721092;
  const double dragAngZ = 0.5;
  // Write the pointer position into the live molecule (drag authority, design
  // D3); the worker reads it on the next tick's re-baseline.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(0)->setPos(Eigen::Vector3d(0.0, 0.0, dragAngZ));
  }
  // After the position is authored the drag promotes and the atom pins. Water
  // converges shortly after the pin, so a modest advance (not an unbounded
  // count) is the deterministic check.
  int pinnedTarget = coordsSpy.count() + 2;
  QTRY_VERIFY_WITH_TIMEOUT(coordsSpy.count() >= pinnedTarget, 30000);
  // Atom 0 (O) pinned at z = 0.5 A.
  QVERIFY(std::abs(lastCoords[2] * bohr2ang - dragAngZ) < 1e-6);

  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  delete mol;
}

void XtbEngineTest::interactiveTogglesAccuracy()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                       /*level=*/4, /*threads=*/0));
  QCOMPARE(engine.currentAccuracy(), 1.0);
  engine.setInteractive(true);
  QCOMPARE(engine.currentAccuracy(), 0.2);
  engine.setInteractive(false);
  QCOMPARE(engine.currentAccuracy(), 1.0);
  stopAndWait(engine);
}

void XtbEngineTest::threadsEnvAutoRespectsClamp()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  qunsetenv("OMP_NUM_THREADS");
  {
    XtbEngine engine;
    QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                         /*level=*/4, /*threads=*/0));
    int autoThreads = qgetenv("OMP_NUM_THREADS").toInt();
    QVERIFY(autoThreads >= 1 && autoThreads <= 8);
    stopAndWait(engine);
  }
  {
    XtbEngine engine;
    QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                         /*level=*/4, /*threads=*/3));
    QCOMPARE(qgetenv("OMP_NUM_THREADS").toInt(), 3);
    stopAndWait(engine);
  }
  qunsetenv("OMP_NUM_THREADS");
}

void XtbEngineTest::gfnffWaterSetupWhenParamsAvailable()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  QString diag;
  if (!Avogadro::XtbParams::ensureGfnffParameters(&diag)) {
    QSKIP("GFN-FF parameter data not available on this machine");
  }
  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/2,
                       /*level=*/4, /*threads=*/0));
  stopAndWait(engine);
}

void XtbEngineTest::gfnffWithoutParamsFailsDeterministically()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  QString diag;
  if (Avogadro::XtbParams::ensureGfnffParameters(&diag)) {
    QSKIP("GFN-FF parameter data available; absent-path test skipped");
  }
  XtbEngine engine;
  QSignalSpy failedSpy(&engine, &XtbEngine::setupFailed);
  QVERIFY(!engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/2,
                        /*level=*/4, /*threads=*/0));
  QCOMPARE(failedSpy.count(), 1);
  QString msg = failedSpy.first().at(0).toString();
  QVERIFY(msg.contains("XTBHOME") || msg.contains("param"));
}

void XtbEngineTest::immediateStopIsSafe()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  XtbEngine engine;
  // Repeat the start/stop cycle several times. Each teardown must quiesce the
  // worker and rebuild the xTB objects cleanly, so a subsequent setup on the
  // same engine object must succeed again.
  for (int cycle = 0; cycle < 5; ++cycle) {
    QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                         /*level=*/4, /*threads=*/0));
    engine.start(QThread::LowPriority);
    // Stop almost immediately, without ever requesting a tick.
    engine.stop();
    QVERIFY(engine.wait(10000));
    // Objects were rebuilt cleanly: a fresh setup must succeed again.
    QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                         /*level=*/4, /*threads=*/0));
  }
}

// Build a live water molecule (O at origin, H at +z, H at -z/0.1).
// Build a live molecule from flat atomic numbers and Å positions. The engine's
// per-tick re-baseline reads this molecule under its read lock, so the tests
// mutate it (under the write lock) to simulate drags and external edits.
static Molecule* makeMoleculeFrom(const std::vector<int>& numbers,
                                  const std::vector<double>& positionsAng)
{
  Molecule* mol = new Molecule;
  for (int i = 0; i < static_cast<int>(numbers.size()); ++i) {
    Atom* a = mol->addAtom();
    a->setAtomicNumber(numbers[i]);
    a->setPos(Eigen::Vector3d(positionsAng[i * 3], positionsAng[i * 3 + 1],
                              positionsAng[i * 3 + 2]));
  }
  return mol;
}

static Molecule* makeWaterMolecule()
{
  Molecule* mol = makeMoleculeFrom(waterNumbers(), waterPositionsAng());
  mol->addBond(0, 1, 1);
  mol->addBond(0, 2, 1);
  return mol;
}

static Molecule* makeDistortedWaterMolecule()
{
  return makeMoleculeFrom(waterNumbers(), waterPositionsDistortedAng());
}

// Design D2 re-baseline: adding an atom mid-session must trigger exactly one
// worker-side re-establishment and the new atom must join the evaluation
// (published coords grow to include it).
void XtbEngineTest::moleculeSourceAddAtomReestablishesOnce()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  QSignalSpy reestablSpy(&engine, &XtbEngine::sessionReestablished);
  std::vector<double> lastCoords;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) { lastCoords = c; });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() { engine.requestTick(); });
  tickTimer.start(30);

  // Let the initial 3-atom session evaluate a few times.
  QTRY_VERIFY_WITH_TIMEOUT(lastCoords.size() == 9, 5000);

  // Add a hydrogen mid-session, under the write lock (the worker reads the
  // molecule under the read lock during its tick).
  {
    QWriteLocker locker(mol->lock());
    Atom* h = mol->addAtom();
    h->setAtomicNumber(1);
    h->setPos(Eigen::Vector3d(0.0, -1.0, 0.0));
  }

  // The new topology must be re-established exactly once and evaluated.
  QTRY_VERIFY_WITH_TIMEOUT(reestablSpy.count() >= 1, 5000);
  QCOMPARE(reestablSpy.count(), 1);
  QCOMPARE(reestablSpy.first().at(0).toInt(), 4);
  // Published coords now carry 4 atoms (12 doubles).
  QTRY_VERIFY_WITH_TIMEOUT(lastCoords.size() == 12, 5000);

  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  delete mol;
}

// Design D2 re-baseline: removing an atom mid-session re-establishes on the
// reduced topology exactly once, and the surviving atoms keep publishing.
void XtbEngineTest::moleculeSourceRemoveAtomSurvivorsPublish()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  QSignalSpy reestablSpy(&engine, &XtbEngine::sessionReestablished);
  std::vector<double> lastCoords;
  int coordsCount = 0;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) {
                     lastCoords = c;
                     ++coordsCount;
                   });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() { engine.requestTick(); });
  tickTimer.start(30);

  QTRY_VERIFY_WITH_TIMEOUT(lastCoords.size() == 9, 5000);

  // Remove a hydrogen (index 1).
  {
    QWriteLocker locker(mol->lock());
    mol->removeAtom(1);
  }

  QTRY_VERIFY_WITH_TIMEOUT(reestablSpy.count() >= 1, 5000);
  QCOMPARE(reestablSpy.count(), 1);
  // Reduced topology: 2 atoms -> 6 doubles, and survivors keep publishing.
  QTRY_VERIFY_WITH_TIMEOUT(lastCoords.size() == 6, 5000);
  int target = coordsCount + 2;
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= target, 10000);

  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  delete mol;
}

// Design D2 re-baseline: changing an atom's element mid-session re-establishes
// and the new element is evaluated (the session does not keep optimizing the
// old element's Hamiltonian invisibly).
void XtbEngineTest::moleculeSourceElementChangeEvaluated()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  QSignalSpy reestablSpy(&engine, &XtbEngine::sessionReestablished);
  std::vector<double> lastCoords;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) { lastCoords = c; });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() { engine.requestTick(); });
  tickTimer.start(30);

  QTRY_VERIFY_WITH_TIMEOUT(lastCoords.size() == 9, 5000);

  // Change the oxygen (index 0) to carbon.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(0)->setAtomicNumber(6);
  }

  QTRY_VERIFY_WITH_TIMEOUT(reestablSpy.count() >= 1, 5000);
  QCOMPARE(reestablSpy.count(), 1);
  // Same topology (3 atoms) but new element: re-establishment happens and the
  // coords keep publishing (the session rebuilt for the new element).
  int coordsCount = 0;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>&) { ++coordsCount; });
  int target = coordsCount + 2;
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= target, 10000);

  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  delete mol;
}

// Design D2 re-baseline: an external position edit (another tool moving atoms,
// an undo, a drag by another surface) is adopted as the starting geometry of a
// fresh phase and is NOT reverted by subsequent write-backs — including after
// the session had converged.
void XtbEngineTest::moleculeSourceExternalMoveAdoptsFreshPhase()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  const double bohr2ang = 0.52917721092;
  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  std::vector<double> lastCoords;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) { lastCoords = c; });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() { engine.requestTick(); });
  tickTimer.start(30);

  QTRY_VERIFY_WITH_TIMEOUT(lastCoords.size() == 9, 5000);

  // Pull H1 (index 1) to z = 1.5 A externally, under the write lock. A moderate
  // displacement is used: a 3.0 A O-H bond dissociates the geometry and the
  // baseline singlepoint fails SCF, which is the D4 containment path (covered
  // by failedEvaluationPublishesNothingAndLatches), not the D2 adoption path
  // this test verifies. The key behavior is that the moved position is adopted
  // as the fresh phase's starting geometry and is NOT reverted to the pre-edit
  // equilibrium by the next write-back.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(1)->setPos(Eigen::Vector3d(0.0, 0.0, 1.5));
  }

  // The moved position must be adopted as the fresh phase's starting geometry:
  // the next write-back does not snap it back toward the pre-edit position.
  // The threshold is tight (0.3 A) so a pre-fix clobber (which snapped H1 back
  // to the ~0.967 A equilibrium, |1.5 - 0.967| = 0.53) fails decisively; the
  // post-fix adoption keeps H1 near the pulled 1.5 A.
  QTRY_VERIFY_WITH_TIMEOUT(lastCoords.size() == 9, 5000);
  QTRY_VERIFY_WITH_TIMEOUT(std::abs(lastCoords[5] * bohr2ang - 1.5) < 0.3,
                           10000);

  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  delete mol;
}

// Design D2/D4: a topology change to an element the selected xTB method does
// not support (Es, 99, outside GFN2's Z in [1,86]) must fail setup exactly
// ONCE and latch deterministically — no per-tick destroy/create churn and no
// repeated setupFailed emissions. The canonical 'Failed setup ends the session
// deterministically' requirement forbids unbounded per-tick retries.
void XtbEngineTest::moleculeSourceUnsupportedElementLatchOnce()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  QSignalSpy reestablSpy(&engine, &XtbEngine::sessionReestablished);
  QSignalSpy setupFailSpy(&engine, &XtbEngine::setupFailed);
  std::vector<double> lastCoords;
  int coordsCount = 0;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) {
                     lastCoords = c;
                     ++coordsCount;
                   });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() { engine.requestTick(); });
  tickTimer.start(30);

  // Let the session evaluate normally first.
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= 1, 5000);

  // Change the oxygen (index 0) to Es (99), which GFN2-xTB does not support.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(0)->setAtomicNumber(99);
  }

  // Exactly one setup failure is emitted (validateElements() rejects the
  // unsupported element during re-establishment) and the session latches: the
  // re-establishment is NOT retried per tick, so no further setupFailed.
  QTRY_VERIFY_WITH_TIMEOUT(setupFailSpy.count() >= 1, 5000);
  QCOMPARE(setupFailSpy.count(), 1);
  // The re-establishment attempt is the one that failed; no second one fired.
  QCOMPARE(reestablSpy.count(), 0);

  // Pump several ticks: the latched phase consumes them without evaluating and
  // without re-attempting setup (deterministic failed-setup, no per-tick
  // retries). The setupFailed counter must stay at exactly 1.
  const int coordsAtFailure = coordsCount;
  for (int i = 0; i < 10; ++i) {
    QTest::qWait(40);
  }
  QCOMPARE(setupFailSpy.count(), 1);
  QCOMPARE(reestablSpy.count(), 0);
  // No coords published from the latched phase.
  QCOMPARE(coordsCount, coordsAtFailure);

  // Design D2/D4 P0 regression: after a failed re-establishment the xTB objects
  // are destroyed (m_objectsCreated == false, m_env == nullptr). A position
  // edit (another tool moving an atom) is adopted as an external change, which
  // must NOT re-arm the optimizer into evaluating with the destroyed objects
  // (that would call into a null xTB environment and crash). The phase stays
  // latched (m_setupBroken), so the next tick is consumed without evaluating
  // and no coords are published — and the process survives.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(1)->setPos(Eigen::Vector3d(0.0, 0.0, 1.5));
  }
  for (int i = 0; i < 10; ++i) {
    QTest::qWait(40);
  }
  // No crash (we reached here); no coords published; setup still failed once;
  // no re-establishment succeeded.
  QCOMPARE(coordsCount, coordsAtFailure);
  QCOMPARE(setupFailSpy.count(), 1);
  QCOMPARE(reestablSpy.count(), 0);

  // Drag press also must not re-arm the broken phase.
  engine.setDraggedAtom(1);
  for (int i = 0; i < 6; ++i) {
    QTest::qWait(40);
  }
  QCOMPARE(coordsCount, coordsAtFailure);
  QCOMPARE(setupFailSpy.count(), 1);
  QCOMPARE(reestablSpy.count(), 0);

  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  delete mol;
}

// Design D4 containment: an evaluation that fails (dissociated geometry -> SCF
// non-convergence) publishes nothing (takeLatestCoords returns false), latches
// the phase idle-with-failure, and recovers after Stop/Start.
void XtbEngineTest::failedEvaluationPublishesNothingAndLatches()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  Molecule* mol = makeWaterMolecule();
  XtbEngine engine;
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));

  QSignalSpy failSpy(&engine, &XtbEngine::evaluationFailed);
  std::vector<double> lastCoords;
  int coordsCount = 0;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) {
                     lastCoords = c;
                     ++coordsCount;
                   });

  engine.start(QThread::LowPriority);
  QTimer tickTimer;
  QObject::connect(&tickTimer, &QTimer::timeout, [&]() { engine.requestTick(); });
  tickTimer.start(30);

  // Let the session evaluate normally first.
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= 1, 5000);

  // Drive a failing evaluation: push H2 to a dissociated position (6 A from
  // O), which the worker adopts as an external position change and then fails
  // to converge on the next singlepoint.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(2)->setPos(Eigen::Vector3d(0.0, 0.0, 6.0));
  }

  // The failure must be surfaced and latched; the failed tick publishes
  // nothing, so takeLatestCoords() returns false and the coords count stops
  // advancing (the session does not keep evaluating garbage geometry).
  QTRY_VERIFY_WITH_TIMEOUT(failSpy.count() >= 1, 10000);
  QVERIFY(failSpy.first().at(0).toString().contains("did not converge") ||
          !failSpy.first().at(0).toString().isEmpty());
  int countAtFailure = coordsCount;
  // Wait out several ticks: no further publish from the latched phase.
  for (int i = 0; i < 15; ++i) {
    QTest::qWait(40);
  }
  QCOMPARE(coordsCount, countAtFailure);

  // Recovery after Stop/Start: a fresh session evaluates normally.
  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  // Restore a convergible geometry before restarting.
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(2)->setPos(Eigen::Vector3d(0.0, 0.1, -1.0));
  }
  QVERIFY(engine.setupFromMolecule(mol, /*method=*/1, /*level=*/4,
                                   /*threads=*/0));
  QSignalSpy failSpy2(&engine, &XtbEngine::evaluationFailed);
  std::vector<double> lastCoords2;
  QObject::connect(&engine, &XtbEngine::coordsReady,
                   [&](const std::vector<double>& c) { lastCoords2 = c; });
  engine.start(QThread::LowPriority);
  QObject::connect(&tickTimer, &QTimer::timeout,
                   [&]() { engine.requestTick(); });
  tickTimer.start(30);
  QTRY_VERIFY_WITH_TIMEOUT(lastCoords2.size() == 9, 5000);
  QCOMPARE(failSpy2.count(), 0);

  tickTimer.stop();
  engine.stop();
  engine.wait(10000);
  delete mol;
}

QTEST_MAIN(XtbEngineTest)
#include "xtbenginetest.moc"
