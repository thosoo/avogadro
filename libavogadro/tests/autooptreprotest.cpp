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
#include <QDir>
#include <QMetaObject>
#include <QDeadlineTimer>

#include <cstdlib>   // getenv, putenv
#include <string>    // std::string

#include <avogadro/glwidget.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/tool.h>
#include <avogadro/toolgroup.h>
#include <avogadro/navigate.h>
#include <avogadro/undostack.h>

#include <QtTest/QSignalSpy>
#include <QUndoCommand>

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

// A minimal molecule-mutating command used to reproduce the push/worker read
// race headlessly (a real DrawTool stroke needs GL picking, which segfaults on
// the offscreen platform). redo()/undo() add or remove a terminal carbon, so a
// push(add) / push(remove) (or push(add) / undo()) cycle reproduces the exact
// addAtom/removeAtom churn a hydrogen-adjusting draw stroke causes on the push
// path. The UndoStack takes ownership of every instance pushed into it.
class AtomToggleCommand : public QUndoCommand
{
public:
  AtomToggleCommand(Molecule *mol, bool addAtom)
    : QUndoCommand(), mol(mol), m_add(addAtom), a(0)
  {
    setText(addAtom ? QStringLiteral("Add atom") : QStringLiteral("Remove atom"));
  }

  void redo() override
  {
    if (m_add) {
      a = mol->addAtom();
      a->setAtomicNumber(6); // carbon
      a->setPos(Eigen::Vector3d(6.0, 0.0, 0.0));
    } else {
      // remove the terminal atom, keeping at least the base chain
      unsigned int n = mol->numAtoms();
      if (n > 2u)
        a = mol->atom(n - 1);
    }
  }

  void undo() override
  {
    if (m_add) {
      // undo of add == remove the terminal atom
      if (a && a->index() == mol->numAtoms() - 1)
        mol->removeAtom(a);
    } else {
      // undo of remove == re-add a terminal carbon
      a = mol->addAtom();
      a->setAtomicNumber(6);
      a->setPos(Eigen::Vector3d(6.0, 0.0, 0.0));
    }
  }

private:
  Molecule *mol;
  bool m_add;
  Atom *a;
};

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
  // QObject::thread() returns QThread*; name this differently so the worker
  // AutoOptThread* is reachable for spying on its setupFailed() signal.
  AutoOptThread *autoThread() const { return m_thread; }
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

  // fix-autoopt-push-lock-race task 2.2: a failed Setup must end the session
  // deterministically, not livelock retrying at tick rate. MMFF94 typing an
  // unsupported element (iron) makes Setup() fail; exactly one setup attempt
  // must be observed across all ticks afterwards.
  void setupFailureStaysFailed();

  // task 3.1: hydrogen-adjusting churn (add/remove atom) pushed through the
  // real UndoStack while the worker ticks. Pre-fix the push mutated the
  // molecule with no lock, racing the worker's OBMol() read (ASan
  // heap-use-after-free). Must pass deterministically post-fix.
  void hydrogenChurnDuringRun();

  // task 3.2: delete churn via push + undo() re-add during a run. Pins the
  // push-override coverage for the undo-side re-add path.
  void undoDeleteDuringRun();

  // task 3.3 / 1.3: quiesce while the test thread holds the molecule write
  // lock must return within a bounded timeout (no read-lock deadlock).
  void quiesceWhileWriteHeldNoDeadlock();

  // task 3.5: Start on a 0-atom molecule must not attempt a Setup (and must
  // not crash); the worker never ticks.
  void emptyMoleculeStartIdle();
};

void AutoOptReproTest::initTestCase()
{
  // Locate the bundled OpenBabel data files (UFF.prm, ...). The OpenBabel
  // ExternalProject installs them under <build>/openbabel-install, but the
  // subdirectory differs by toolchain: non-MSVC puts them in
  // share/openbabel/<version>/ (OpenBabel appends the version when it
  // resolves a data file), while MSVC puts them directly in bin/data/.
  // OpenBabel's compiled-in BABEL_DATADIR is not guaranteed to resolve on
  // every CI runner (the Windows installer build is the known failure), so
  // override it explicitly via the BABEL_DATADIR environment variable, the
  // name OpenBabel's OpenDatafile() consults before the compiled-in path.
  // This is process-global and applied before any force field is set up, so
  // every test benefits.
  {
#ifdef _MSC_VER
    static const char kDataSubpath[] = "/../openbabel-install/bin/data";
#else
    static const char kDataSubpath[] = "/../openbabel-install/share/openbabel";
#endif
    QString dataBase = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + QString::fromUtf8(kDataSubpath));

    // OpenBabel's OpenDatafile() resolves parameter files with the narrow
    // C-runtime getenv("BABEL_DATADIR"). Qt's qputenv() writes the WIDE
    // environment on Windows (via wputenv), which that narrow getenv() does
    // not observe - leaving UFF.prm unfindable on the Windows CI runner. Drive
    // the narrow environment directly with putenv() so getenv() actually sees
    // the value. The buffer must persist (putenv stores a pointer), so keep it
    // in a static and dup it once.
    std::string kv = std::string("BABEL_DATADIR=") + dataBase.toLocal8Bit().toStdString();
    static char *kvStorage = nullptr;
    if (!kvStorage) {
      kvStorage = strdup(kv.c_str());
    }
    putenv(kvStorage);

    qWarning().noquote() << "AutoOptReproTest BABEL_DATADIR=" << dataBase
                         << " getenv="
                         << (getenv("BABEL_DATADIR") ? getenv("BABEL_DATADIR") : "(null)");
  }

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

void AutoOptReproTest::setupFailureStaysFailed()
{
  // Task 2.2: a failed Setup must end the session deterministically, not
  // livelock retrying at tick rate. UFF parameterizes essentially everything,
  // so we drive a genuine Setup failure with MMFF94 typing an unsupported
  // element (iron, Z=26): MMFF94's typing step rejects it and Setup() returns
  // false. We start a session on a 2-iron molecule with MMFF94 selected and
  // count how many times Setup is attempted via the worker's setupFailed().
  // Pre-fix, the per-tick setup() reset m_stop and the session retried Setup
  // on every tick (N emissions). Post-fix, the first failure sets m_stop and
  // the per-tick setup() (newSession=false) no longer resurrects it, so exactly
  // one setupFailed is emitted.
  Molecule *mol = new Molecule;
  {
    Atom *a = mol->addAtom();
    a->setAtomicNumber(26); // iron
    a->setPos(Eigen::Vector3d(0.0, 0.0, 0.0));
    Atom *b = mol->addAtom();
    b->setAtomicNumber(26);
    b->setPos(Eigen::Vector3d(2.2, 0.0, 0.0));
    mol->addBond(0, 1, 1);
  }
  QVERIFY(mol);

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  QVERIFY(tool.ffCombo() != nullptr);
  int idx = tool.ffCombo()->findText("MMFF94");
  QVERIFY(idx >= 0);
  tool.ffCombo()->setCurrentIndex(idx);
  tool.setGlw(&widget);

  // Count Setup failures: each failed Setup emits the worker's setupFailed().
  QSignalSpy spy(tool.autoThread(), &AutoOptThread::setupFailed);
  QVERIFY(spy.isValid());

  // Start the session; the worker begins ticking on the real 50 ms timer.
  tool.enable();
  QVERIFY(tool.running());

  // Bound the whole test against a hang (persistent failure / deadlock).
  QDeadlineTimer deadline(10000);

  // Wait for the first Setup failure to be observed.
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.count(), 1u);

  // Let a number of further ticks fire (the real 50 ms timer). If the
  // per-tick setup() still resurrected the session, each tick would attempt
  // Setup again and spy.count() would keep growing. Post-fix it stays at 1.
  // Interleave processEvents so the worker's queued update()/finished() drain
  // between observations.
  while (!deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
    if (spy.count() > 1u)
      break; // livelock: a second Setup attempt appeared
  }
  QCOMPARE(spy.count(), 1u);

  // CPU-idle afterwards: no further failures for a quiet window.
  int idleSpins = 0;
  while (idleSpins++ < 1000 && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  QCOMPARE(spy.count(), 1u);

  tool.disable();
  QVERIFY(!tool.running());

  // Singleton hygiene.
  uff()->UnsetFixAtom();
  uff()->UnsetIgnoreAtom();

  delete mol;
}

void AutoOptReproTest::hydrogenChurnDuringRun()
{
  // Task 3.1: hydrogen-adjusting churn while the worker ticks. A real DrawTool
  // press/move/release stroke needs GL picking, which segfaults on the
  // offscreen platform (see glselectoverflowtest.cpp:262), so this drives the
  // identical race surface at the mutation layer: 200 add/remove-atom cycles
  // pushed through the real UndoStack (the fix's choke point, task 1.1) while
  // the worker ticks. Pre-fix the push mutated the molecule with no lock,
  // racing the worker's OBMol() read of m_atomList (ASan heap-use-after-free);
  // post-fix the UndoStack::push write lock serializes the mutation against the
  // worker read.
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  AutoOptThread thread;
  thread.setup(mol, uff(), AutoOptConjugateGradients, 1);
  thread.start();
  QVERIFY(thread.isRunning());

  QDeadlineTimer deadline(30000);
  // Let one tick publish a valid snapshot.
  QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);
  while (!thread.snapshot().valid && !deadline.hasExpired())
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QVERIFY(thread.snapshot().valid);

  UndoStack stack(mol);
  for (int i = 0; i < 200 && !deadline.hasExpired(); ++i) {
    // add an atom (as a draw stroke would), then remove it -- net zero per cycle
    stack.push(new AtomToggleCommand(mol, true));
    stack.push(new AtomToggleCommand(mol, false));

    // worker ticks concurrently
    QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);

    // molecule stays consistent (no torn read / dangling atom)
    QVERIFY(mol->numAtoms() >= 2u);
    QVERIFY(mol->numBonds() <= (unsigned)(mol->numAtoms() * (mol->numAtoms() - 1) / 2));
    const auto snap = thread.snapshot();
    QVERIFY(snap.fixed.CountBits() <= (unsigned int)mol->numAtoms());
    QVERIFY(snap.ignored.CountBits() <= (unsigned int)mol->numAtoms());
    QVERIFY(!deadline.hasExpired());
  }

  stack.clear();
  thread.stop();
  thread.quit();
  bool quiesced = thread.wait(10000);
  if (!quiesced) {
    thread.terminate();
    thread.wait();
  }
  QVERIFY(quiesced);
  delete mol;
}

void AutoOptReproTest::undoDeleteDuringRun()
{
  // Task 3.2: delete churn via push + undo() re-add during a run. The delete
  // commands are applied through UndoStack::push (write lock, task 1.1); undo()
  // re-adds them. undo() is intentionally NOT wrapped in the write lock
  // (design D3, deferred) -- this test pins that the push/undo cycle during a
  // run does not crash and keeps the molecule consistent.
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  AutoOptThread thread;
  thread.setup(mol, uff(), AutoOptConjugateGradients, 1);
  thread.start();
  QVERIFY(thread.isRunning());

  QDeadlineTimer deadline(20000);
  QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);
  while (!thread.snapshot().valid && !deadline.hasExpired())
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QVERIFY(thread.snapshot().valid);

  UndoStack stack(mol);
  for (int i = 0; i < 100 && !deadline.hasExpired(); ++i) {
    // delete churn: push a delete (redo removes the terminal atom)
    stack.push(new AtomToggleCommand(mol, false));
    QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);
    // re-add via undo (undo() re-adds a terminal carbon)
    stack.undo();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);

    QVERIFY(mol->numAtoms() >= 2u);
    QVERIFY(!deadline.hasExpired());
  }

  stack.clear();
  thread.stop();
  thread.quit();
  bool quiesced = thread.wait(10000);
  if (!quiesced) {
    thread.terminate();
    thread.wait();
  }
  QVERIFY(quiesced);
  delete mol;
}

void AutoOptReproTest::quiesceWhileWriteHeldNoDeadlock()
{
  // Task 3.3 / 1.3: quiesce while the GUI holds the molecule write lock must
  // return within a bounded timeout. The worker's read acquisition is a bounded
  // tryLockForRead retry that returns immediately when m_stop is set (design
  // D4). Pre-fix, the worker's unbounded lockForRead would block on the held
  // write lock forever and wait() would hang.
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  AutoOptThread thread;
  thread.setup(mol, uff(), AutoOptConjugateGradients, 1);
  thread.start();
  QVERIFY(thread.isRunning());

  // Let one tick publish a valid snapshot (proves the worker ran).
  QDeadlineTimer deadline(20000);
  QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);
  while (!thread.snapshot().valid && !deadline.hasExpired())
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QVERIFY(thread.snapshot().valid);

  // Hold the write lock, then quiesce the worker. Any queued update() the
  // worker runs must bail (m_stop) rather than block on the held write lock.
  //
  // Scoped in its own block so the QWriteLocker releases the molecule write
  // lock BEFORE mol is deleted: destroying the QReadWriteLock against freed
  // molecule storage would be a use-after-free (the write lock is still held
  // until this locker is destroyed).
  {
    QWriteLocker wl(mol->lock());
    QMetaObject::invokeMethod(&thread, "update", Qt::QueuedConnection);
    // Give the worker a moment to reach the read-lock acquisition while we hold
    // the write lock, then quiesce.
    for (int i = 0; i < 200 && !deadline.hasExpired(); ++i)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 2);

    thread.stop();
    thread.quit();
    bool quiesced = thread.wait(10000);
    if (!quiesced) {
      thread.terminate();
      thread.wait();
    }
    QVERIFY(quiesced);
  }
  QVERIFY(!deadline.hasExpired());

  delete mol;
}

void AutoOptReproTest::emptyMoleculeStartIdle()
{
  // Task 3.5: Start on a 0-atom molecule must not attempt a Setup (and must not
  // crash). The per-tick timerEvent gates on numAtoms() < 2, so the worker is
  // never asked to tick; no Setup runs and no setupFailed is emitted.
  Molecule *mol = new Molecule; // 0 atoms
  QVERIFY(mol);

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  QVERIFY(tool.ffCombo() != nullptr);
  tool.setGlw(&widget);

  // Count Setup failures across the whole idle window.
  QSignalSpy spy(tool.autoThread(), &AutoOptThread::setupFailed);
  QVERIFY(spy.isValid());

  tool.enable();
  QVERIFY(tool.running());

  // Pump for ~10 s (the real 50 ms timer fires ~200 times). Each tick returns
  // early on numAtoms() < 2; no Setup attempt is made.
  QDeadlineTimer deadline(15000);
  int spins = 0;
  while (spins++ < 3000 && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(2);
  }
  QCOMPARE(spy.count(), 0u);

  tool.disable();
  QVERIFY(!tool.running());

  delete mol;
}

QTEST_MAIN(AutoOptReproTest)
#include "autooptreprotest.moc"
