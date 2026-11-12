#include "config.h"
#include <QtTest>

#include "xtb/xtbapi.h"
#include "xtb/xtbengine.h"
#include "xtb/xtbparams.h"

#include <QSignalSpy>
#include <QTimer>

#include <cmath>
#include <vector>

using Avogadro::XtbApi;
using Avogadro::XtbEngine;

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
  void interactiveTogglesAccuracy();
  void threadsEnvAutoRespectsClamp();
  void gfnffWaterSetupWhenParamsAvailable();
  void gfnffWithoutParamsFailsDeterministically();
  void immediateStopIsSafe();
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

  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsDistortedAng(),
                       /*method=*/1, /*level=*/4, /*threads=*/0));

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
  // forcing the user to restart the optimization).
  const double pullZAng = 2.5;
  const std::vector<double> pullBohr = { 0.0, 0.0, pullZAng / bohr2ang };
  engine.setDraggedAtom(1);
  engine.notifyDragPosition(1, pullBohr);

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
}

void XtbEngineTest::draggedAtomPinnedDuringOptimization()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  const double bohr2ang = 0.52917721092;
  const double dragAngZ = 2.0;
  const double dragBohrZ = dragAngZ / bohr2ang; // 2.0 A in bohr
  std::vector<double> dragPos = { 0.0, 0.0, dragBohrZ };

  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                       /*level=*/4, /*threads=*/0));

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
      engine.notifyDragPosition(1, dragPos);
    }
  });
  tickTimer.start(30);

  // Wait for at least one evaluation before grabbing.
  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= 1, 5000);
  std::vector<double> preDrag = lastCoords;

  dragActive = true;
  engine.setDraggedAtom(1);
  engine.notifyDragPosition(1, dragPos);

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
}

void XtbEngineTest::dragReleaseResumesCoverage()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }
  const double bohr2ang = 0.52917721092;
  const double dragBohrZ = 2.0 / bohr2ang;
  std::vector<double> dragPos = { 0.0, 0.0, dragBohrZ };

  XtbEngine engine;
  QVERIFY(engine.setup(waterNumbers(), waterPositionsAng(), /*method=*/1,
                       /*level=*/4, /*threads=*/0));

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
      engine.notifyDragPosition(1, dragPos);
    }
  });
  tickTimer.start(30);

  QTRY_VERIFY_WITH_TIMEOUT(coordsCount >= 1, 5000);

  // Drag atom 1 to (0,0,2.0) A and hold it for >= 3 evaluations.
  dragActive = true;
  engine.setDraggedAtom(1);
  engine.notifyDragPosition(1, dragPos);
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

QTEST_MAIN(XtbEngineTest)
#include "xtbenginetest.moc"
