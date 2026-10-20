/**********************************************************************
 * autooptreprotest.cpp
 *
 * End-to-end regression for the "fix-autoopt-frozen-atoms" change.
 *
 * The fix made OpenBabel force-field constraint state (fix/ignore markers +
 * the OBFFConstraints bit-vectors) PER-INSTANCE instead of static, so a draw
 * stroke or dummy-atom ignore in one tool no longer leaks into AutoOpt (and
 * vice versa). It also wired AutoOpt's enable()/disable()/abort() and the
 * per-tick worker to clear/reset those markers, and made DrawTool release an
 * in-flight stroke's markers when the tool is switched.
 *
 * These cases reproduce the design.md section D repro matrix (steps 1, 2, 5,
 * 7 and the per-instance independence invariant) by driving the real tool
 * state machines.
 *
 * NOTE (concurrency-blindness): every existing case here drives the worker
 * synchronously via `thread.update()` (or, for the tool lifecycle cases,
 * `disable()` calls `m_thread->wait()` before the assertions run). That
 * pattern (see the comment at autooptreprotest.cpp:271) stops the worker so
 * no background update() races the assertions, which makes the suite
 * structurally blind to the cross-thread force-field race fixed by
 * fix-autoopt-concurrency. autoOptConcurrencyRaces() is the counterpoint: it
 * runs a REAL started QThread and exercises update() concurrently with
 * snapshot() reads, setPendingFixAtom() writes and molecule mutations.
 ***********************************************************************/

#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtGui/QSurfaceFormat>
#include <QCoreApplication>
#include <QMetaObject>
#include <QDeadlineTimer>

#include <avogadro/glwidget.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/tool.h>
#include <avogadro/toolgroup.h>
#include <avogadro/navigate.h>

#include <openbabel/forcefield.h>

#include "tools/autoopttool.h"
#include "tools/drawtool.h"

using namespace Avogadro;

namespace {

// Build a small carbon chain molecule (C-C-C-C) as an Avogadro::Molecule.
Molecule *makeCarbonChain()
{
  Molecule *mol = new Molecule;
  const double bond = 1.5;
  for (int i = 0; i < 4; ++i) {
    Atom *a = mol->addAtom();
    a->setAtomicNumber(6); // carbon
    a->setPos(Eigen::Vector3d(i * bond, 0.0, 0.0));
  }
  if (mol->numAtoms() >= 2) {
    for (int i = 0; i + 1 < mol->numAtoms(); ++i)
      mol->addBond(i, i + 1, 1);
  }
  return mol;
}

// UFF singleton (AutoOpt optimizes this instance).
OpenBabel::OBForceField *uff()
{
  return OpenBabel::OBForceField::FindForceField("UFF");
}

// MMFF94 singleton (DrawTool owns this instance post-fork).
OpenBabel::OBForceField *mmff94()
{
  return OpenBabel::OBForceField::FindForceField("MMFF94");
}

// Exposes AutoOptTool's protected members for the lifecycle-hygiene checks.
class AutoOptToolExposed : public AutoOptTool
{
public:
  GLWidget *glw() const { return m_glwidget; }
  void setGlw(GLWidget *w) { m_glwidget = w; }
  OpenBabel::OBForceField *ff() const { return m_forceField; }
  bool running() const { return m_running; }
  QComboBox *ffCombo() const { return m_comboFF; }
  QComboBox *algCombo() const { return m_comboAlgorithm; }
  QSpinBox *stepsSpin() const { return m_stepsSpinBox; }
  QPushButton *startBtn() const { return m_buttonStartStop; }
};

} // namespace

// Exposes DrawTool's in-flight stroke state so the "tool switched away
// mid-stroke" scenario (spec: Tool switched away mid-stroke) can be driven
// directly, without a real GLWidget picking pipeline. Defined at global scope
// Defined inside namespace Avogadro so it exactly matches the `friend class
// DrawToolExposed;` hook inside Avogadro::DrawTool (drawtool.h), granting access
// to DrawTool's private stroke state for the "tool switched away mid-stroke"
// scenario, without depending on GL picking under xvfb.
namespace Avogadro {
class DrawToolExposed : public DrawTool
{
public:
  OpenBabel::OBForceField *ff() const { return m_forceField; }
  void setFF(OpenBabel::OBForceField *ff) { m_forceField = ff; }
  Atom *beginAtom() const { return m_beginAtom; }
  void setBeginAtom(Atom *a) { m_beginAtom = a; }
  Atom *endAtom() const { return m_endAtom; }
  void setEndAtom(Atom *a) { m_endAtom = a; }
  Bond *bond() const { return m_bond; }
  void setBond(Bond *b) { m_bond = b; }
  int prevAtomElement() const { return m_prevAtomElement; }
  void setPrevAtomElement(int e) { m_prevAtomElement = e; }
};
} // namespace Avogadro

class AutoOptReproTest : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Step 5 + 7: per-tick Clear()/rescan self-heals dummy-atom ignores and the
  // count never grows.
  void rescanDummyAtomAndStableCounts();

  // Core invariant: the AutoOpt (UFF) and DrawTool (MMFF94) instances are
  // independent — a marker on one does not appear on the other.
  void perInstanceIndependence();

  // Step 1 + 2 + 7: enable()/disable() clear stale markers; HUD stays 0 0.
  void enableDisableClearsMarkers();

  // Step 6.4: switching tools mid-stroke releases the in-flight markers and
  // restores the alchemy-edited begin atom (spec: Tool switched away
  // mid-stroke).
  void drawToolSwitchedMidStrokeReleasesMarkers();

  // Step 3: a stroke-scoped anchor on DrawTool's instance does not freeze the
  // atom under AutoOpt's live optimization, and the marker is released once
  // the stroke ends (spec: Draw a bond while optimization runs).
  void drawWhileRunAnchorScopedReleased();

  // Step 4: deleting an atom while optimization runs leaves no survivor
  // frozen as a result of the deletion (spec: Atom deleted while
  // optimization runs).
  void deleteAtomWhileRunningNoSurvivorFreeze();

  // Step 7: 20x start/stop keeps the HUD at 0 0 with no count growth.
  void startStop20xStable();

  // Concurrency regression net for fix-autoopt-concurrency: a real started
  // worker thread driven via queued update() while the test thread hammers
  // snapshot(), setPendingFixAtom() and molecule mutations, then a quiesced
  // disable()/enable() teardown. Asserts snapshot invariants and clean,
  // deadlock-free teardown (spec: AutoOpt force-field access is thread-confined).
  void autoOptConcurrencyRaces();
};

void AutoOptReproTest::initTestCase()
{
  // QApplication is created by QTEST_MAIN; the OpenBabel force-field plugin
  // registry is available without any extra initialization.
  QVERIFY(uff() != nullptr);
  QVERIFY(mmff94() != nullptr);
}

void AutoOptReproTest::cleanupTestCase()
{
  // QApplication managed by QTEST_MAIN.
}

void AutoOptReproTest::rescanDummyAtomAndStableCounts()
{
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  AutoOptThread thread;
  thread.setup(mol, uff(), AutoOptConjugateGradients, 2);

  // Insert a dummy atom (atomic number 0 == no element) -> should be ignored.
  Atom *dummy = mol->addAtom();
  dummy->setAtomicNumber(0);
  dummy->setPos(Eigen::Vector3d(6.0, 0.0, 0.0));

  // First tick: dummy is ignored (blue halo / counted in Ignored). The rescan
  // runs on the UFF singleton that setup() was given.
  thread.update();
  QCOMPARE(uff()->GetConstraints().IsIgnored(dummy->index() + 1), true);

  // Counts stay stable over many ticks while the dummy is present (no runaway
  // "Num Constraints" growth): only the dummy atom is ever ignored; the 4
  // carbons never are. The task requires stability over 1000+ ticks.
  for (int i = 0; i < 1000; ++i)
    thread.update();
  QCOMPARE(uff()->GetConstraints().GetIgnoredBitVec().CountBits(), 1u);
  QCOMPARE(uff()->GetConstraints().GetFixedBitVec().CountBits(), 0u);

  // Give it a real element -> within one tick it loses the ignore marker and
  // the Ignored count returns to 0.
  dummy->setAtomicNumber(6);
  thread.update();
  QCOMPARE(uff()->GetConstraints().IsIgnored(dummy->index() + 1), false);
  QCOMPARE(uff()->GetConstraints().GetIgnoredBitVec().CountBits(), 0u);

  delete mol;
}

void AutoOptReproTest::perInstanceIndependence()
{
  // DrawTool's MMFF94 instance and AutoOpt's UFF instance must not share state.
  OpenBabel::OBForceField *drawFF = mmff94();
  OpenBabel::OBForceField *autoFF = uff();
  QVERIFY(drawFF != autoFF);

  // Marker on the DrawTool instance must not leak into AutoOpt's instance.
  drawFF->SetFixAtom(2);
  drawFF->SetIgnoreAtom(3);
  QVERIFY(drawFF->GetFixAtom() == 2);
  QVERIFY(drawFF->GetIgnoreAtom() == 3);
  QVERIFY(autoFF->GetFixAtom() == 0);
  QVERIFY(autoFF->GetIgnoreAtom() == 0);
  QVERIFY(!autoFF->GetConstraints().IsFixed(2));
  QVERIFY(!autoFF->GetConstraints().IsIgnored(3));

  // Reset DrawTool's instance before testing the reverse direction so the
  // two directions don't interfere.
  drawFF->UnsetFixAtom();
  drawFF->UnsetIgnoreAtom();
  drawFF->GetConstraints().Clear();

  // And the reverse: a marker on AutoOpt's instance must not leak into
  // DrawTool's instance.
  autoFF->SetFixAtom(1);
  autoFF->SetIgnoreAtom(4);
  QVERIFY(autoFF->GetFixAtom() == 1);
  QVERIFY(autoFF->GetIgnoreAtom() == 4);
  QVERIFY(drawFF->GetFixAtom() == 0);
  QVERIFY(drawFF->GetIgnoreAtom() == 0);
  QVERIFY(!drawFF->GetConstraints().IsFixed(1));
  QVERIFY(!drawFF->GetConstraints().IsIgnored(4));

  // Clean up so we don't disturb other tests sharing the singletons.
  drawFF->UnsetFixAtom();
  drawFF->UnsetIgnoreAtom();
  drawFF->GetConstraints().Clear();
  autoFF->UnsetFixAtom();
  autoFF->UnsetIgnoreAtom();
  autoFF->GetConstraints().Clear();
}

void AutoOptReproTest::enableDisableClearsMarkers()
{
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  // Build the settings widgets (combos / spinbox / start-stop button) that
  // enable() reads. m_forceFieldList is populated by the constructor.
  tool.settingsWidget();
  QVERIFY(tool.ffCombo() != nullptr);
  QVERIFY(tool.algCombo() != nullptr);
  QVERIFY(tool.stepsSpin() != nullptr);
  QVERIFY(tool.startBtn() != nullptr);
  tool.setGlw(&widget);

  // Contaminate: set markers as if a previous session left them.
  tool.ff()->SetFixAtom(1);
  tool.ff()->SetIgnoreAtom(2);
  tool.ff()->GetConstraints().AddIgnore(3);
  QVERIFY(tool.ff()->GetFixAtom() != 0);

  // enable() must clear them before starting the worker.
  tool.enable();
  QVERIFY(tool.running());
  QCOMPARE(tool.ff()->GetFixAtom(), 0u);
  QCOMPARE(tool.ff()->GetIgnoreAtom(), 0u);
  QCOMPARE(tool.ff()->GetConstraints().GetFixedBitVec().CountBits(), 0u);
  QCOMPARE(tool.ff()->GetConstraints().GetIgnoredBitVec().CountBits(), 0u);

  // disable() clears markers again (stop/start recovery: step 2 + 7) and
  // stops the worker so no background update() races the assertions.
  tool.disable();
  QVERIFY(!tool.running());
  QCOMPARE(tool.ff()->GetFixAtom(), 0u);
  QCOMPARE(tool.ff()->GetIgnoreAtom(), 0u);
  QCOMPARE(tool.ff()->GetConstraints().GetFixedBitVec().CountBits(), 0u);
  QCOMPARE(tool.ff()->GetConstraints().GetIgnoredBitVec().CountBits(), 0u);

  delete mol;
}

void AutoOptReproTest::drawToolSwitchedMidStrokeReleasesMarkers()
{
  // Spec: "Tool switched away mid-stroke" — any temporary anchor/exclusion
  // from an in-flight draw stroke SHALL be released when the tool is
  // deactivated, and the alchemy-edited begin atom SHALL be restored.
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  // Use a non-zero index: OpenBabel treats fix/ignore index 0 as "none" (see
  // OBForceField::UnsetFixAtom), so atom 0 cannot be a fix target.
  Atom *begin = mol->atom(1);
  QVERIFY(begin != nullptr);

  DrawToolExposed draw;
  OpenBabel::OBForceField *drawFF = draw.ff();
  QVERIFY(drawFF != nullptr);

  // Simulate an in-flight stroke: begin atom anchored with both an ignore and
  // a fix marker. The atom was alchemically changed to carbon (6); its
  // original element (nitrogen, 7) is remembered in m_prevAtomElement.
  const int originalElement = 7;
  begin->setAtomicNumber(6);
  draw.setBeginAtom(begin);
  draw.setPrevAtomElement(originalElement);
  draw.setBond(nullptr);
  drawFF->SetIgnoreAtom(begin->index());
  drawFF->SetFixAtom(begin->index());
  QVERIFY(drawFF->GetFixAtom() != 0);
  QVERIFY(draw.beginAtom() == begin);

  // A different tool is activated mid-stroke.
  DrawToolExposed other;
  draw.toolActivated(&other);

  // Markers released, stroke state reset, begin atom's element restored.
  QCOMPARE(drawFF->GetFixAtom(), 0u);
  QCOMPARE(drawFF->GetIgnoreAtom(), 0u);
  QCOMPARE(draw.beginAtom(), nullptr);
  QCOMPARE(draw.endAtom(), nullptr);
  QCOMPARE(draw.bond(), nullptr);
  QCOMPARE(draw.prevAtomElement(), 0);
  QCOMPARE(begin->atomicNumber(), originalElement);

  delete mol;
}

void AutoOptReproTest::drawWhileRunAnchorScopedReleased()
{
  // Spec: "Draw a bond while optimization runs" — the stroke-scoped anchor is
  // scoped to the interaction and must not leave AutoOpt frozen; once the
  // stroke ends the marker is released.
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  AutoOptThread thread;
  thread.setup(mol, uff(), AutoOptConjugateGradients, 2);

  // Non-zero index (atom 0 is OpenBabel's "no fix" sentinel).
  Atom *anchor = mol->atom(1);
  QVERIFY(anchor != nullptr);

  // DrawTool begins a stroke from the anchor: anchors it on DrawTool's own
  // MMFF94 instance.
  DrawToolExposed draw;
  OpenBabel::OBForceField *drawFF = draw.ff();
  QVERIFY(drawFF != nullptr);
  draw.setBeginAtom(anchor);
  drawFF->SetFixAtom(anchor->index());
  QVERIFY(drawFF->GetFixAtom() != 0);

  // AutoOpt's UFF optimizer must not see DrawTool's anchor marker (per-instance
  // independence during concurrent editing): its fixed count stays 0 across
  // several optimization ticks.
  QCOMPARE(uff()->GetConstraints().GetFixedBitVec().CountBits(), 0u);
  for (int i = 0; i < 5; ++i)
    thread.update();
  QCOMPARE(uff()->GetConstraints().GetFixedBitVec().CountBits(), 0u);
  QCOMPARE(uff()->GetFixAtom(), 0u);

  // Stroke ends (tool switched away) -> DrawTool's anchor marker is released.
  DrawToolExposed other;
  draw.toolActivated(&other);
  QCOMPARE(drawFF->GetFixAtom(), 0u);

  delete mol;
}

void AutoOptReproTest::deleteAtomWhileRunningNoSurvivorFreeze()
{
  // Spec: "Atom deleted while optimization runs" — no surviving atom becomes
  // frozen as a result of the deletion, and no stale ignore survives.
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  AutoOptThread thread;
  thread.setup(mol, uff(), AutoOptConjugateGradients, 2);

  // Add a dummy atom (atomic number 0 == ignored) and run a few ticks so the
  // ignore is active.
  Atom *dummy = mol->addAtom();
  dummy->setAtomicNumber(0);
  dummy->setPos(Eigen::Vector3d(6.0, 0.0, 0.0));
  thread.update();
  QCOMPARE(uff()->GetConstraints().IsIgnored(dummy->index() + 1), true);

  // Delete the dummy atom mid-run and continue optimizing.
  mol->removeAtom(dummy);
  for (int i = 0; i < 5; ++i)
    thread.update();

  // No survivor is frozen and the removed dummy leaves no stale ignore.
  QCOMPARE(uff()->GetConstraints().GetFixedBitVec().CountBits(), 0u);
  QCOMPARE(uff()->GetConstraints().GetIgnoredBitVec().CountBits(), 0u);

  delete mol;
}

void AutoOptReproTest::startStop20xStable()
{
  // Step 7: 20x start/stop keeps the HUD at 0 0 with no count growth (this
  // replaces the old runaway "Num Constraints").
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  QVERIFY(tool.ffCombo() != nullptr);
  QVERIFY(tool.algCombo() != nullptr);
  QVERIFY(tool.stepsSpin() != nullptr);
  QVERIFY(tool.startBtn() != nullptr);
  tool.setGlw(&widget);

  for (int i = 0; i < 20; ++i) {
    tool.enable();
    QVERIFY(tool.running());
    QCOMPARE(tool.ff()->GetFixAtom(), 0u);
    QCOMPARE(tool.ff()->GetIgnoreAtom(), 0u);
    QCOMPARE(tool.ff()->GetConstraints().GetFixedBitVec().CountBits(), 0u);
    QCOMPARE(tool.ff()->GetConstraints().GetIgnoredBitVec().CountBits(), 0u);
    tool.disable();
    QVERIFY(!tool.running());
  }

  delete mol;
}

void AutoOptReproTest::autoOptConcurrencyRaces()
{
  // Drives a REAL started AutoOptThread (the worker) directly, so the
  // finished() signal has no GUI handler and cannot mutate the molecule from
  // the test thread. This isolates exactly what fix-autoopt-concurrency
  // fixes: the worker's live force-field access, the published snapshot, the
  // drag pending-marker path, the worker-side molecule read lock, and
  // quiesced teardown. (The tool-level enable()/disable() lifecycle is
  // covered by startStop20xStable and by the teardown below.)
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  AutoOptThread thread;
  thread.setup(mol, uff(), AutoOptConjugateGradients, 1);
  thread.start();
  QVERIFY(thread.isRunning());

  // Let one tick run and publish a fresh snapshot (the worker publishes
  // asynchronously, so poll rather than read a stale default). The fresh
  // snapshot is valid and constraint-free.
  // QDeadlineTimer guards the whole test against deadlock/hang: if the worker
  // never publishes (e.g. a persistent Setup failure) or quiesced teardown
  // hangs, this fires and the assertion fails instead of hanging the suite.
  QDeadlineTimer deadline(20000);

  QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);
  while (!thread.snapshot().valid && !deadline.hasExpired())
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  {
    const AutoOptThread::ConstraintSnapshot snap = thread.snapshot();
    QVERIFY(snap.valid);
    QCOMPARE(snap.fixAtom, 0u);
    QCOMPARE(snap.ignoreAtom, 0u);
    QVERIFY(snap.ignored.CountBits() <= (unsigned int)mol->numAtoms());
    QVERIFY(snap.fixed.CountBits() <= (unsigned int)mol->numAtoms());
  }

  const int iterations = 400;
  for (int i = 0; i < iterations; ++i) {
    // Queue a tick on the worker thread (the race surface).
    QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);

    // Hammer the snapshot read path (the GUI-thread paint path). No snapshot
    // read may ever see torn data or crash while the worker rebuilds the FF.
    for (int j = 0; j < 16; ++j) {
      const AutoOptThread::ConstraintSnapshot snap = thread.snapshot();
      // Invariant: published constraint bits never exceed the atom count at
      // publish time; fix/ignore markers are 0 or a valid 1-based index.
      QVERIFY(snap.ignored.CountBits() <= (unsigned int)mol->numAtoms());
      QVERIFY(snap.fixed.CountBits() <= (unsigned int)mol->numAtoms());
      if (snap.fixAtom != 0u)
        QVERIFY(snap.fixAtom <= (unsigned int)mol->numAtoms());
      if (snap.ignoreAtom != 0u)
        QVERIFY(snap.ignoreAtom <= (unsigned int)mol->numAtoms());
    }

    // Interleave a drag fix-marker write (GUI -> worker pending value).
    if (i % 4 == 0)
      thread.setPendingFixAtom((unsigned)((i % mol->numAtoms()) + 1));
    else
      thread.setPendingFixAtom(0u);

    // Mutate the topology under the write lock, mirroring DrawTool, so the
    // worker's OBMol() copy + dummy rescan cannot race live molecule storage.
    // Real carbons keep UFF Setup() succeeding (a dummy, atomicNumber 0, would
    // make Setup fail and publish an invalid snapshot -- that path is covered
    // by the synchronous cases). Bond each new atom so the molecule stays
    // connected and topologically valid for the force field.
    if (i % 30 == 0) {
      if (mol->lock()->tryLockForWrite()) {
        Atom *tmp = mol->addAtom();
        tmp->setAtomicNumber(6); // carbon
        tmp->setPos(Eigen::Vector3d(6.0, 0.0, 0.0));
        mol->addBond(0, tmp->id(), 1);
        mol->lock()->unlock();
      }
    }

    QVERIFY(!deadline.hasExpired());
  }

  // Quiesced teardown: stop requesting ticks, then wait for the in-flight tick
  // to finish (spec: teardown quiesces the worker before touching shared
  // state). The m_stop early-out makes the drained ticks cheap, so wait()
  // is bounded. A timeout here fails the test instead of hanging the suite.
  thread.stop();
  thread.quit();
  // wait() returns false on timeout; terminate first so we never destroy a
  // running QThread (Qt's qFatal would abort the whole binary and mask the
  // remaining assertions).
  bool quiesced = thread.wait(10000);
  if (!quiesced) {
    thread.terminate();
    thread.wait();
  }
  QVERIFY(quiesced);
  QVERIFY(!deadline.hasExpired());

  // 20x teardown-mid-tick cycles (spec: tool switch / document close while
  // running): start, immediately quiesce mid-tick, then restart. Each cycle
  // must complete without deadlock or crash and re-Start cleanly. The m_stop
  // early-out keeps each wait() bounded. This is the worker-level analogue of
  // AutoOptTool::disable()/abort() quiesced shutdown under a running tick.
  bool allCyclesOk = true;
  for (int cycle = 0; cycle < 20 && !deadline.hasExpired(); ++cycle) {
    thread.setup(mol, uff(), AutoOptConjugateGradients, 1);
    thread.start();
    QVERIFY(thread.isRunning());
    QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);
    // Let one tick publish a valid snapshot (proves the re-Start ran).
    bool validSeen = false;
    while (!deadline.hasExpired()) {
      if (thread.snapshot().valid) {
        validSeen = true;
        break;
      }
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (!validSeen) {
      allCyclesOk = false;
      break;
    }
    // Immediate quiesce mid-tick.
    thread.stop();
    thread.quit();
    if (!thread.wait(10000)) {
      allCyclesOk = false;
      break;
    }
  }
  QVERIFY(allCyclesOk);
  QVERIFY(!deadline.hasExpired());

  // Singleton hygiene: the drained ticks may have left a residual transient
  // fix marker on the shared UFF instance used by other tests.
  uff()->UnsetFixAtom();

  delete mol;
}

QTEST_MAIN(AutoOptReproTest)
#include "autooptreprotest.moc"
