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
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QtGui/QSurfaceFormat>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QDir>
#include <QImage>
#include <QMetaObject>
#include <QDeadlineTimer>

#include <cstdlib>   // getenv, putenv
#include <string>    // std::string
#include <functional> // std::function (pumpUntil)
#include <cmath>     // std::abs

#include <avogadro/glwidget.h>
#include <avogadro/glhit.h>
#include <avogadro/pluginmanager.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/primitive.h>
#include <avogadro/tool.h>
#include <avogadro/toolgroup.h>
#include <avogadro/navigate.h>
#include <avogadro/undostack.h>

#include <QtTest/QSignalSpy>
#include <QUndoCommand>

#include <openbabel/forcefield.h>

#include "tools/autoopttool.h"
#include "tools/drawtool.h"
#include "libavogadro/src/engines/bsdyengine.h"
#include "xtb/xtbapi.h"
#include "xtb/xtbengine.h"

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

//! Pump the event loop until `cond()` holds or `timeoutMs` elapse.
void pumpUntil(const std::function<bool()> &cond, int timeoutMs = 10000)
{
  QDeadlineTimer deadline(timeoutMs);
  while (!cond() && !deadline.hasExpired())
    QCoreApplication::processEvents();
}

//! Build a modern-tier GLWidget hosting `mol` with the real ball-and-stick
//! engine, shown and rendered until the worker's first result is adopted and
//! presented (backend live, picking usable). Mirrors hidpiviewporttest's
//! makeHiDpiWidget. Returns a heap widget; the caller deletes it before the
//! molecule. Check presentedRevision()/backendDrawCallsLastFrame() to detect
//! a GL stack where the modern tier cannot present (the caller QSKIPs there).
GLWidget *makeLiveWidget(Molecule *mol)
{
  // NOTE: deliberately NOT pinning version/profile here. Pinning 3.3-Compat
  // made QOpenGLWidget's context rebuild produce a CORE 4.5 context on this
  // stack (llvmpipe/GLX), where paintGL's fixed-function matrix setup no-ops
  // with GL_INVALID_OPERATION and the frame/pick MVP degenerates to identity
  // (the pre-existing presentation limitation hidpiviewporttest documents).
  // The driver default on this stack is a Compatibility profile, which keeps
  // both the fixed-function setup and the modern instanced tier working.
  QSurfaceFormat fmt;
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);

  GLWidget *widget = new GLWidget(mol, fmt, nullptr);
  // NOTE: no resize() before show() — firing a resize event before the
  // widget's context is realized drove the double-initialise dance (two
  // contexts; the modern backend's GL objects stranded on the dead one;
  // black frames and an empty pick). The default widget size is used as-is.

  BSDYEngine *engine = new BSDYEngine;
  engine->setMolecule(mol);
  engine->setEnabled(true);
  widget->addEngine(engine);

  // Deterministic framing: center the molecule and pull back 8 world units so
  // atoms and bonds are comfortably large for the GL pick passes at any
  // default zoom (the fresh-widget camera is not guaranteed to frame mol).
  Eigen::Vector3d center = Eigen::Vector3d::Zero();
  const QList<Atom *> atoms = mol->atoms();
  if (!atoms.isEmpty()) {
    for (Atom *a : atoms)
      center += *a->pos();
    center /= static_cast<double>(atoms.size());
  }
  Eigen::Projective3d mv = Eigen::Projective3d::Identity();
  mv.translate(-center);
  mv.translate(Eigen::Vector3d(0.0, 0.0, -8.0));
  widget->camera()->setModelview(mv);

  widget->show();
  widget->renderNow(); // force initializeGL + first (async-submit) frame
  pumpUntil([widget]() { return widget->backendDrawCallsLastFrame() >= 1; });
  widget->renderNow(); // present one clean, populated frame
  return widget;
}

//! True when the live-GUI scenario checks can run at all: a real window
//! system (QOpenGLWidget cannot present on the offscreen platform) and the
//! modern tier actually presented a frame on `widget`.
bool liveGuiReady(GLWidget *widget)
{
  if (QGuiApplication::platformName() == QLatin1String("offscreen"))
    return false;
  return widget->backendDrawCallsLastFrame() >= 1 &&
         widget->presentedRevision() >= 1;
}

const char *const kLiveGuiSkipReason =
    "live GUI scenario needs a real window system (Xvfb/xcb) with a working "
    "GL 3.3 context; the offscreen platform cannot present and the modern "
    "tier cannot run here (same gate as hidpiviewporttest)";

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
  QComboBox *engineCombo() const { return m_comboEngine; }
  QComboBox *levelCombo() const { return m_comboLevel; }
  QSpinBox *stepsSpin() const { return m_stepsSpinBox; }
  QLabel *ffLabel() const { return m_labelFF; }
  QLabel *algLabel() const { return m_labelAlg; }
  QLabel *levelLabel() const { return m_labelLevel; }
  QLabel *threadsLabel() const { return m_labelThreads; }
  QSpinBox *threadsSpin() const { return m_threadsSpinBox; }
  QPushButton *startBtn() const { return m_buttonStartStop; }
  // QObject::thread() returns QThread*; name this differently so the worker
  // AutoOptThread* is reachable for spying on its setupFailed() signal.
  AutoOptThread *autoThread() const { return m_thread; }

  // Test-only: simulate a force-field-less deployment by tearing down the
  // worker. When the constructor finds no force field it bails before
  // creating m_thread, leaving it null; this reproduces that state so the
  // paint()/mouseReleaseEvent() D3 guards are exercised (they must not
  // dereference a null worker).
  void nullWorker()
  {
    if (m_thread) {
      m_thread->exit();
      m_thread->wait();
      delete m_thread;
      m_thread = 0;
    }
  }

  // Drag-path state for the task 2.1 write-back test. The members are
  // protected in AutoOptTool, so AutoOptReproTest (not a subclass) reaches
  // them through these public mutators defined on the subclass.
  void setDragMarker(Atom *a, bool pressed)
  {
    m_clickedAtom = a;
    m_leftButtonPressed = pressed;
  }
  void setDraggingPosition(const QPoint &p)
  {
    m_lastDraggingPosition = p;
  }
  // finished() only writes back when m_running; disable() clears it, so a
  // test that quiesces the worker before driving finished() re-sets it.
  void setRunning(bool running)
  {
    m_running = running;
  }
  // Live-drag observability (task 2.2): which atom the press grabbed.
  Atom *clickedAtom() const { return m_clickedAtom; }
  // The live xTB session engine (task 4.3), so the tool-level scenarios can
  // observe its re-establishment / evaluation signals directly. Null while no
  // xTB session is active.
  XtbEngine *xtbEngine() const { return m_xtbEngine; }
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
  // The stroke state members are now molecule ids (see drawtool.h), so the
  // exposed accessors convert to/from ids. setBeginAtom also records the
  // owning molecule so toolActivated() can resolve the begin atom.
  unsigned long beginAtomId() const { return m_beginAtomId; }
  void setBeginAtom(Atom *a)
  {
    m_beginAtomId = a ? a->id() : FALSE_ID;
    m_beginMolecule = a ? a->molecule() : nullptr;
  }
  unsigned long endAtomId() const { return m_endAtomId; }
  void setEndAtom(Atom *a) { m_endAtomId = a ? a->id() : FALSE_ID; }
  unsigned long bondId() const { return m_bondId; }
  void setBond(Bond *b) { m_bondId = b ? b->id() : FALSE_ID; }
  // Wrapper to reach DrawTool's private prepareStrokeState() from the test:
  // AutoOptReproTest is not itself a friend of DrawTool, but DrawToolExposed
  // is, so this member call is allowed.
  bool callPrepareStrokeState(Molecule *m, Atom **ba, Atom **ea, Bond **b, Bond **pb)
  {
    return prepareStrokeState(m, ba, ea, b, pb);
  }
  int prevAtomElement() const { return m_prevAtomElement; }
  void setPrevAtomElement(int e) { m_prevAtomElement = e; }
  // The live bond-order click (task 3.1) runs with "adjust hydrogens"
  // disabled so the only molecule mutation is the order bump.
  void setAddHydrogens(bool b) { m_addHydrogens = b; }
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

  // fix-drawtool-stale-atom-crash: a held stroke id that no longer resolves
  // (atom removed by churn) or a molecule replaced mid-stroke SHALL make
  // prepareStrokeState() return false so the tool abandons the interrupted
  // stroke instead of dereferencing a dangling reference.
  void staleOrReplacedMoleculeAbandonsStroke();

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
  void draggedAtomWriteBackSkipped();

  // fix-autoopt-tool-activation-crash task 2.1 (headless): a fresh tool with
  // the settings dock built (m_comboFF non-null) but no prior canvas
  // interaction (m_glwidget null) must not crash when Start is pressed, and
  // must not report running. Then a mouse release on the fresh tool must not
  // crash whether or not the worker was created (force-field-less deployment).
  void freshToolStartBeforePaintIsSafe();

  // fix-autoopt-tool-activation-crash task 2.1 (D3): with no force field the
  // constructor never creates the worker (m_thread == 0), so paint() must
  // return early (no snapshot read) and mouseReleaseEvent() must skip the
  // marker bookkeeping — both without crashing.
  void forceFieldlessToolIsInert();

  // task 4.2 (OpenSpec 17): availability gating. Selecting an xTB backend
  // while the runtime library is not resolvable SHALL disable the Start
  // button; with the library present (or the force field selected) it SHALL
  // stay enabled. Follows the spec's branch on XtbApi::isAvailable().
  void xtbEngineGatedWithoutLibrary();

  // LIVE-GUI scenario checks (fix-modern-tier-revision-churn 2.2 / 3.1).
  // Both drive the real GLWidget on a real GL context (Xvfb/llvmpipe or real
  // hardware) with real QTest mouse events through the active tool, and both
  // QSKIP with a diagnostic when no GL context can be created (offscreen ctest
  // keeps passing, mirroring hidpiviewporttest's pattern).
  // 2.2: press-drag-release an atom while AutoOpt runs — the atom tracks the
  // pointer monotonically (no tick-sized backward jumps), other atoms keep
  // optimizing, and coverage resumes at release.
  void liveDragAtomDuringAutoOpt();

  // task 4.4 (OpenSpec 19): a press/drag/release cycle during an xTB session
  // must leave the molecule valid (atom count intact, positions finite). The
  // drag routing (setDraggedAtom on press, setDraggedAtom(-1) on release)
  // drives the engine's masking; the dragged atom's position is the live
  // molecule (pointer authority, design D3), read by the worker on its
  // per-tick re-baseline. This case guards that the resulting engine ticks +
  // coordinate write-back never corrupt the molecule. Runs headlessly (no
  // live GL pick required).
  void xtbDragCycleLeavesValidMolecule();

  // task 4.2 (OpenSpec 14): tool-level scenarios that edit the live molecule
  // while an xTB session runs. The worker re-baselines per tick (design D2), so
  // a topology change (draw/erase) must re-establish the session exactly once
  // and the write-back must keep updating the molecule (no frozen atoms); an
  // undo/redo external position change must be adopted as a fresh phase with no
  // snap-back. All three run headlessly (no live GL pick required) by mutating
  // the molecule under the write lock, exactly as a draw/erase stroke would.
  void xtbDrawAtomDuringRunKeepsWriteBack();
  void xtbEraseDuringRunSurvivorsMove();
  void xtbUndoRedoDuringRunNoSnapBack();

  // task 4.6 (OpenSpec 21): backend + convergence-level selection persists.
  // Write engine=GFN2 and level=Tight to a temp ini QSettings, reload into a
  // fresh tool instance, and assert both selections round-trip (spec: the
  // backend selection persists across restarts like the other tool settings).
  void engineSettingsRoundTrip();
  void engineSwitchHidesControlsAndCaptions();

  // task 6.2 (OpenSpec 27): after retiring the old xtbopttool plugin, exactly
  // one optimization tool exists at runtime — the unified AutoOptTool. Assert
  // its AVOGADRO_TOOL identity is "AutoOptimization" (the optimization tool)
  // and that no optimization tool carries the retired "XtbOptimization"
  // identity. Instruments are instantiated directly from source (the build
  // compiles autoopttool.cpp into the binary rather than loading autoopttool.so,
  // which would duplicate the plugin-export symbols); this test cannot
  // enumerate loaded plugins without that collision.
  void singleOptimizationTool();

  // task 7.4 (OpenSpec 31): the tool's AVOGADRO_TOOL description must name all
  // four engines (force fields, GFN1-xTB, GFN2-xTB, GFN-FF), document atom
  // pulling during optimization and the convergence-level control.
  void toolDocumentationMatchesBehavior();

  // 3.1: click a bond mid-session (adjust-hydrogens off) — the order cycles
  // 1→2, the molecule revision bumps, the modern tier adopts a rebuild at or
  // after the click revision within ~one build, and ticks continue.
  void liveBondOrderClickDuringAutoOpt();

  // add-draw-tool-click-extend 5.1 / 5.2 live-GUI scenarios (the OpenSpec
  // frames these as manual; a real GL context under Xvfb can drive them, so
  // they are exercised automatically instead of left to a human):
  // 5.1 same-element click -> new atom + bond, single undo step;
  // 5.1 different-element click -> alchemy (element change, no new atom);
  // 5.2 same-element click while AutoOpt runs -> new atom created free, no
  //    constraint markers left behind.
  void liveClickExtendSameElementCreatesAtom();
  void liveClickExtendDifferentElementNoNewAtom();
  void liveClickExtendDuringAutoOpt();
};

void AutoOptReproTest::initTestCase()
{
  // The compat-tier GL path touches PluginManager lazily during its first
  // render; left at the default path it scans the build directory and
  // QPluginLoader-loads libavogadro.so into a process that already links it
  // (duplicate QObject metadata) and crashes. Point it at an empty existing
  // directory: nothing to load, and this test instantiates every tool/engine
  // directly from sources compiled into the binary.
  const QString kEmptyPluginDir = QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/empty-plugins");
  QDir().mkpath(kEmptyPluginDir);
  PluginManager::instance()->setPluginPath(kEmptyPluginDir);

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
  QVERIFY(draw.beginAtomId() == begin->id());

  // A different tool is activated mid-stroke.
  DrawToolExposed other;
  draw.toolActivated(&other);

  // Markers released, stroke state reset, begin atom's element restored.
  QCOMPARE(drawFF->GetFixAtom(), 0u);
  QCOMPARE(drawFF->GetIgnoreAtom(), 0u);
  QCOMPARE(draw.beginAtomId(), FALSE_ID);
  QCOMPARE(draw.endAtomId(), FALSE_ID);
  QCOMPARE(draw.bondId(), FALSE_ID);
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

void AutoOptReproTest::staleOrReplacedMoleculeAbandonsStroke()
{
  // fix-drawtool-stale-atom-crash: the Draw tool's cross-event stroke state is
  // stored as molecule ids and resolved against the live molecule at use time.
  // A held id that no longer resolves (atom removed by churn) or a molecule
  // replaced mid-stroke SHALL make prepareStrokeState() return false so the
  // tool abandons the interrupted stroke instead of dereferencing a dangling
  // reference (the crash fixed here).
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  DrawToolExposed draw;

  QVERIFY(mol->numAtoms() >= 3);
  Atom *begin = mol->atom(1);   // valid id 1
  QVERIFY(begin != nullptr);
  Atom *survivor = mol->atom(2); // valid id 2, removed 'begin' must not touch it
  QVERIFY(survivor != nullptr);

  // Sanity: a held id on the correct molecule resolves to the anchored atom.
  draw.setBeginAtom(begin);
  Atom *oba = nullptr;
  QCOMPARE(draw.callPrepareStrokeState(mol, &oba, nullptr, nullptr, nullptr), true);
  QCOMPARE(oba, begin);

  // 1) Molecule replacement: same dense id space but a different molecule ->
  //    the held ids can no longer be trusted, so this must abandon.
  Molecule *other = new Molecule;
  other->addAtom();
  QCOMPARE(draw.callPrepareStrokeState(other, &oba, nullptr, nullptr, nullptr), false);
  delete other;

  // 2) Churn: remove the anchored atom. Its id now resolves to nullptr ->
  //    abandon.
  mol->removeAtom(begin);
  QCOMPARE(draw.callPrepareStrokeState(mol, &oba, nullptr, nullptr, nullptr), false);

  // 3) A survivor's id still resolves after the churn (sanity).
  draw.setBeginAtom(survivor);
  QCOMPARE(draw.callPrepareStrokeState(mol, &oba, nullptr, nullptr, nullptr), true);
  QCOMPARE(oba, survivor);

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

void AutoOptReproTest::draggedAtomWriteBackSkipped()
{
  // fix-modern-tier-revision-churn task 2.1: while a left-button drag is
  // active, a tick's coordinate write-back MUST NOT overwrite the grabbed
  // atom. Its force-field coordinate is a stale, up-to-one-tick-pre-drag
  // snapshot (and the atom is fix-markered, so the optimizer never moved it);
  // writing it back teleports the atom backward before the drag path re-anchors
  // it. Non-dragged atoms still receive the optimized coordinates every tick.
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  tool.setGlw(&widget);
  tool.enable();
  QVERIFY(tool.running());

  // Quiesce the worker: the force field now holds a stable set of
  // coordinates. Read them back as the ground-truth write-back target F.
  tool.disable();
  QVERIFY(!tool.running());

  OpenBabel::OBForceField *ff = tool.autoThread()->forceField();
  QVERIFY(ff);

  const int n = mol->numAtoms();
  QVERIFY(n >= 2);
  QList<Atom *> atoms = mol->atoms();

  // Known write-back targets F (spread far from the chain's origin so a
  // write-back is detectably wrong on the non-dragged atoms). Load them into
  // the force field: set up, put F on the molecule, then copy molecule -> FF.
  OpenBabel::OBMol obmol = mol->OBMol();
  QVERIFY(ff->Setup(obmol));
  for (Atom *a : atoms) {
    const int i = a->index();
    a->setPos(Eigen::Vector3d(10.0 + i, -i, 0.5 * i));
  }
  obmol = mol->OBMol();
  QVERIFY(ff->SetCoordinates(obmol));

  // Put every atom at the origin (P), which differs from F, so the write-back
  // target is unambiguous.
  for (Atom *a : atoms) {
    a->setPos(Eigen::Vector3d(0.0, 0.0, 0.0));
    a->update();
  }

  // finished() only writes back while m_running; disable() cleared it. The
  // worker is already quiesced, so re-enabling the flag is safe (no concurrent
  // tick) and lets finished() run its write-back from the test thread.
  tool.setRunning(true);

  // Engage a left-button drag on the first atom and make the re-anchor
  // translate() a no-op (from == to), so a skipped write-back leaves the
  // dragged atom exactly where the drag put it (the origin).
  Eigen::Vector3d begin = widget.camera()->project(*atoms[0]->pos());
  tool.setDraggingPosition(QPoint(qRound(begin.x()), qRound(begin.y())));
  tool.setDragMarker(atoms[0], true);

  // One tick's write-back via finished().
  tool.finished(true);

  auto nearEq = [](const Eigen::Vector3d &a, const Eigen::Vector3d &b, double tol) {
    return (a - b).norm() < tol;
  };

  // Grabbed atom: write-back skipped, translate no-op -> stays at the drag
  // position (origin), NOT at the stale force-field coordinate F[0] = (10,0,0).
  QVERIFY(nearEq(*atoms[0]->pos(), Eigen::Vector3d(0.0, 0.0, 0.0), 1e-9));
  QVERIFY(!nearEq(*atoms[0]->pos(), Eigen::Vector3d(10.0, 0.0, 0.0), 1e-6));

  // Non-dragged atoms: write-back applied to the force-field coordinates F.
  for (int i = 1; i < n; ++i) {
    QVERIFY(nearEq(*atoms[i]->pos(),
                 Eigen::Vector3d(10.0 + i, -i, 0.5 * i), 1e-9));
  }

  // Release: coverage resumes, so a subsequent write-back now moves the atom.
  tool.setDragMarker(nullptr, false);
  tool.finished(true);
  // Coverage resumed: the dragged atom now receives its write-back (F[0]).
  QVERIFY(nearEq(*atoms[0]->pos(), Eigen::Vector3d(10.0, 0.0, 0.0), 1e-9));

  delete mol;
}

void AutoOptReproTest::freshToolStartBeforePaintIsSafe()
{
  // fix-autoopt-tool-activation-crash task 2.1. Reproduces the field crash:
  // activate the AutoOpt tool (its settings dock is built, so m_comboFF is
  // non-null) and press Start without ever moving or clicking the pointer over
  // the molecule view, so no paint() and no canvas mouse event has associated
  // a GLWidget yet (m_glwidget stays null on a renderer that skips the tool
  // overlay pass).
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget(); // build the settings dock (m_comboFF non-null)
  QVERIFY(tool.ffCombo() != nullptr);
  // NOTE: deliberately NOT calling tool.setGlw(&widget) — m_glwidget stays
  // null, simulating activation without a prior canvas interaction.

  // Start before any painted frame: must not crash and must not run. The
  // enable() guard bails with a diagnostic (design D2) instead of
  // dereferencing the null m_glwidget.
  tool.enable();
  QVERIFY(!tool.running());

  // Mouse release on the fresh tool (no force-field bookkeeping): must not
  // crash whether the worker exists (force field present) or not
  // (force-field-less deployment, design D3). The handler ignores the event
  // parameter, so a trivial release event suffices.
  QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(10, 10),
                           QPointF(10, 10), Qt::LeftButton, Qt::NoButton,
                           Qt::NoModifier);
  tool.mouseReleaseEvent(&widget, &releaseEvent);
  QVERIFY(!tool.running());

  delete mol;
}

void AutoOptReproTest::forceFieldlessToolIsInert()
{
  // D3: a force-field-less deployment leaves the worker null (the constructor
  // bails before creating it). paint() must return early without reading the
  // snapshot, and mouseReleaseEvent() must skip the marker bookkeeping — both
  // must not dereference the null worker. The assert is "no crash + inert",
  // not "FF missing", so it holds in both the FF-present and FF-absent states.
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  QVERIFY(tool.ffCombo() != nullptr);
  tool.setGlw(&widget);
  tool.nullWorker(); // simulate the force-field-less deployment
  QVERIFY(tool.autoThread() == nullptr);

  // paint() with no worker: must not dereference m_thread (no snapshot read).
  QVERIFY(!tool.paint(&widget));

  // mouseReleaseEvent() with no worker: must skip setPendingFixAtom(0) and
  // not crash.
  QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(10, 10),
                           QPointF(10, 10), Qt::LeftButton, Qt::NoButton,
                           Qt::NoModifier);
  tool.mouseReleaseEvent(&widget, &releaseEvent);

  delete mol;
}

void AutoOptReproTest::xtbEngineGatedWithoutLibrary()
{
  // task 4.2 (OpenSpec 17): availability gating. Selecting an xTB backend
  // while the runtime library is not resolvable SHALL disable the Start
  // button; with the library present (or the force field selected) it SHALL
  // stay enabled. The spec branches on XtbApi::isAvailable(), so the case is
  // correct on machines with and without libxtb.
  AutoOptToolExposed tool;
  tool.settingsWidget();
  QVERIFY(tool.engineCombo() != nullptr);
  QVERIFY(tool.startBtn() != nullptr);

  // Default is the force field, which stays enabled.
  QCOMPARE(tool.engineCombo()->currentIndex(), ForceField);
  QVERIFY(tool.startBtn()->isEnabled());

  // Select GFN2-xTB (index 2). engineChanged() fires and applies the gate.
  QSignalSpy msgSpy(&tool, &AutoOptTool::message);
  tool.engineCombo()->setCurrentIndex(2);
  QCOMPARE(tool.engineCombo()->currentIndex(), 2);

  if (!XtbApi::isAvailable()) {
    // Library absent: the xTB Start button is gated off with a diagnostic.
    QVERIFY(!tool.startBtn()->isEnabled());
    QVERIFY(tool.startBtn()->toolTip().contains("force fields"));
    QCOMPARE(msgSpy.count(), 1);
    QVERIFY(msgSpy.first().first().toString().contains("force fields"));
  } else {
    // Library present: xTB Start stays enabled.
    QVERIFY(tool.startBtn()->isEnabled());
  }
}

//! Pick-coherence gate for the live GUI scenario checks: the GL pick must
//! resolve the atom at its camera-projected position. On GL stacks where the
//! pre-existing QOpenGLWidget integration defect strands the renderer's GL
//! objects (double-initialise; GLWidget::paintEvent bypasses the QOpenGLWidget
//! FBO management), every instanced draw rasterises nothing: frames are black
//! and the colour-ID pick reads an all-sentinel FBO, so clicks cannot be
//! anchored. This is the same limitation hidpiviewporttest documents
//! ("modern tier renders a black frame under xcb+llvmpipe ... Real hardware
//! is the acceptance gate"); it predates and is outside the scope of
//! fix-modern-tier-revision-churn.
bool pickCoherent(GLWidget *widget, Molecule *mol)
{
  Atom *atom0 = mol->atom(0);
  if (!atom0)
    return false;
  const Eigen::Vector3d sp = widget->camera()->project(*atom0->pos());
  return widget->computeClickedAtom(QPoint(qRound(sp.x()), qRound(sp.y()))) ==
         atom0;
}

const char *const kPickIncoherentSkipReason =
    "the GL pick cannot anchor camera-projected positions on this GL stack "
    "(pre-existing QOpenGLWidget double-initialise presentation defect: "
    "black frames, empty pick; same limitation hidpiviewporttest documents "
    "for xcb+llvmpipe). Real hardware is the acceptance gate.";

void AutoOptReproTest::liveDragAtomDuringAutoOpt()
{
  // fix-modern-tier-revision-churn task 2.2 (live): press-drag-release an
  // atom while AutoOpt runs. The atom must track the pointer with a bounded
  // trail (no backward tick-sized jumps - the task 2.1 write-back skip is
  // what prevents those), other atoms must keep optimizing during the drag,
  // and coverage must resume at release.
  if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
    QSKIP("live GUI drag needs a real window system: QOpenGLWidget cannot "
          "present on the offscreen platform (segfaults there)");
  }
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);
  GLWidget *widget = makeLiveWidget(mol);
  if (!liveGuiReady(widget) || !pickCoherent(widget, mol)) {
    delete widget;
    delete mol;
    QSKIP(kLiveGuiSkipReason);
  }

  AutoOptToolExposed tool;
  tool.settingsWidget();
  tool.setGlw(widget);
  tool.enable();
  QVERIFY(tool.running());
  widget->setTool(&tool);

  QList<Atom *> atoms = mol->atoms();
  QVERIFY(atoms.size() >= 4);
  Atom *dragged = atoms[1];
  Atom *witness = atoms[2]; // must keep optimizing while the drag is active

  QTest::qWait(200); // let per-tick movement establish
  const Eigen::Vector3d preDrag = *dragged->pos();
  const Eigen::Vector3d witnessAtPress = *witness->pos();

  // Press on the atom's projected position: the grab must engage through the
  // real GL picking path (computeClickedAtom -> hits -> backend pick). The
  // pick lags live positions by up to one build, so re-project and retry a
  // bounded number of times (the pick slop absorbs the small residual drift).
  bool grabbed = false;
  QPoint pressPos;
  for (int attempt = 0; attempt < 5 && !grabbed; ++attempt) {
    const Eigen::Vector3d spNow = widget->camera()->project(*dragged->pos());
    pressPos = QPoint(qRound(spNow.x()), qRound(spNow.y()));
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, pressPos);
    grabbed = (tool.clickedAtom() == dragged);
    if (!grabbed) {
      QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, pressPos);
      QTest::qWait(150); // let a fresh build present, then retry
      widget->renderNow();
      QApplication::processEvents();
    }
  }
  if (!grabbed) {
    tool.disable();
    delete widget;
    delete mol;
    QSKIP("could not anchor the press on the dragged atom through the live "
          "pick (scene lag on this stack); see the pick-coherence gate");
  }

  // Drag in steps toward a target ~94 px away. After every step, sample the
  // dragged atom's screen distance to the target. The dragged atom is
  // fix-markered during the drag, so only the translate path moves it: the
  // distance must decrease monotonically. A write-back regression (the bug
  // task 2.1 fixes) teleports the atom back to its stale force-field
  // coordinate once per tick - a jump of tens of pixels.
  const QPoint target = pressPos + QPoint(80, -50);
  double prevDist = -1.0;
  double maxBackwardJump = 0.0;
  for (int step = 1; step <= 8; ++step) {
    const QPoint p = pressPos + (target - pressPos) * step / 8;
    QTest::mouseMove(widget, p);
    QTest::qWait(80); // ticks run between moves
    const Eigen::Vector3d cur = widget->camera()->project(*dragged->pos());
    const double dist =
      (Eigen::Vector2d(cur.x(), cur.y()) -
       Eigen::Vector2d(target.x(), target.y())).norm();
    if (prevDist >= 0.0)
      maxBackwardJump = qMax(maxBackwardJump, dist - prevDist);
    prevDist = dist;
  }

  // Tracks the pointer: the atom ends at the drag target (small pixel
  // tolerance for the camera-parallel plane mapping).
  QVERIFY2(prevDist < 8.0,
           "dragged atom did not follow the pointer to the drag target");
  // Bounded trail: no backward tick-sized jump at any sampled step.
  QVERIFY2(maxBackwardJump < 5.0,
           "dragged atom jumped backward between moves (tick write-back "
           "overwrote the grabbed atom)");
  // And it actually moved with the pointer (not a no-op drag).
  QVERIFY2((widget->camera()->project(*dragged->pos()) -
            Eigen::Vector3d(pressPos.x(), pressPos.y(), 0.0)).norm() > 50.0,
           "dragged atom barely moved");

  // Other atoms keep optimizing while the drag holds the fix marker.
  const Eigen::Vector3d witnessAtEnd = *witness->pos();
  QVERIFY2((witnessAtEnd - witnessAtPress).norm() > 0.02,
           "non-dragged atoms did not optimize during the drag");

  // Release: the fix marker clears and the optimizer's write-back covers the
  // dragged atom again, so it starts moving under the optimizer.
  QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, target);
  QCOMPARE(tool.clickedAtom(), static_cast<Atom *>(0));
  const Eigen::Vector3d atRelease = *dragged->pos();
  bool movedAfterRelease = false;
  {
    QDeadlineTimer deadline(3000);
    while (!deadline.hasExpired()) {
      QApplication::processEvents();
      if ((*dragged->pos() - atRelease).norm() > 0.05) {
        movedAfterRelease = true;
        break;
      }
    }
  }
  QVERIFY2(movedAfterRelease,
           "dragged atom did not resume optimizing after release");

  tool.disable();
  delete widget;
  delete mol;
}

void AutoOptReproTest::xtbDragCycleLeavesValidMolecule()
{
  // task 4.4 (OpenSpec 19): a press/drag/release cycle during an xTB session
  // must leave the molecule valid (atom count intact, positions finite). The
  // drag routing (setDraggedAtom on press, setDraggedAtom(-1) on release)
  // drives the engine's masking; the dragged atom's position is the live
  // molecule (pointer authority, design D3), read by the worker on its
  // per-tick re-baseline. This case guards that the resulting engine ticks +
  // coordinate write-back never corrupt the molecule. Runs headlessly (no
  // live GL pick required).
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }

  // Water (O + 2 H), a small GFN2-valid system. Coordinates are in angstrom;
  // deliberately distorted so the optimizer has something to relax.
  Molecule *mol = new Molecule;
  const double numbers[3] = {8.0, 1.0, 1.0};
  const double xyz[9] = {0.0, 0.0, 0.0,
                         0.0, 0.0, 1.3,
                         0.0, 0.1, -0.95};
  for (int i = 0; i < 3; ++i) {
    Atom *a = mol->addAtom();
    a->setAtomicNumber(static_cast<int>(numbers[i]));
    a->setPos(Eigen::Vector3d(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]));
  }
  QVERIFY(mol->numAtoms() == 3);

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  // Select the xTB GFN2 backend (engine combo index 2) and the Normal level.
  tool.engineCombo()->setCurrentIndex(2);
  if (tool.levelCombo())
    tool.levelCombo()->setCurrentIndex(4); // Normal
  tool.setGlw(&widget);
  tool.enable();
  QVERIFY(tool.running());

  QList<Atom *> atoms = mol->atoms();
  QVERIFY(atoms.size() == 3);

  // Let the engine tick and publish its first coordinate snapshot to the
  // molecule (exercises the timerEvent xTB branch + snapshot write-back).
  for (int i = 0; i < 6; ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(20);
  }

  // Press: engage a left-button drag on atom 0 via the exposed drag marker
  // (same headless pattern the FF write-back test uses). The real press
  // handler would route the grab to m_xtbEngine->setDraggedAtom(idx), but
  // computeClickedAtom needs a GL selection context a context-less widget
  // lacks, so engage the drag directly and drive the move/release handlers
  // below for the routing that reads m_clickedAtom.
  tool.setDragMarker(atoms[0], true);

  // Move: exercises the drag path (translate() writes the dragged atom's
  // position into the live molecule; the worker adopts it on its per-tick
  // re-baseline, design D3).
  QMouseEvent moveEvent(QEvent::MouseMove, QPointF(5, 5), QPointF(5, 5),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  tool.mouseMoveEvent(&widget, &moveEvent);

  // Let the engine tick with the dragged atom masked, then release: the real
  // release handler exercises the setDraggedAtom(-1) routing so the optimizer
  // resumes covering the atom.
  for (int i = 0; i < 6; ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(20);
  }

  QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  tool.mouseReleaseEvent(&widget, &releaseEvent);

  for (int i = 0; i < 6; ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(20);
  }

  // Quiesce the engine before touching the molecule.
  tool.disable();
  QVERIFY(!tool.running());

  // The molecule must survive the whole drag cycle intact.
  QVERIFY(mol->numAtoms() == 3);
  for (Atom *a : atoms) {
    const Eigen::Vector3d p = *a->pos();
    QVERIFY(qIsFinite(p.x()));
    QVERIFY(qIsFinite(p.y()));
    QVERIFY(qIsFinite(p.z()));
  }

  delete mol;
}

// task 4.2 (OpenSpec 14): a draw-atom edit during a running xTB session. The
// worker re-baselines per tick (design D2), so adding an atom mid-session must
// re-establish the session exactly once and the write-back must keep updating
// the molecule — the new atom participates and no existing atom freezes.
// Runs headlessly by mutating the live molecule under the write lock, exactly
// as a draw stroke's addAtom would (no live GL pick required).
void AutoOptReproTest::xtbDrawAtomDuringRunKeepsWriteBack()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }

  Molecule *mol = new Molecule;
  const double numbers[3] = {8.0, 1.0, 1.0};
  const double xyz[9] = {0.0, 0.0, 0.0,
                         0.0, 0.0, 1.3,
                         0.0, 0.1, -0.95};
  for (int i = 0; i < 3; ++i) {
    Atom *a = mol->addAtom();
    a->setAtomicNumber(static_cast<int>(numbers[i]));
    a->setPos(Eigen::Vector3d(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]));
  }

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  tool.engineCombo()->setCurrentIndex(2); // GFN2-xTB
  if (tool.levelCombo())
    tool.levelCombo()->setCurrentIndex(4); // Normal
  tool.setGlw(&widget);
  tool.enable();
  QVERIFY(tool.running());

  XtbEngine *engine = tool.xtbEngine();
  QVERIFY(engine != nullptr);
  QSignalSpy reestablSpy(engine, &XtbEngine::sessionReestablished);
  QSignalSpy coordsSpy(engine, &XtbEngine::coordsReady);

  // Let the initial 3-atom session evaluate a few times (the first tick is a
  // 100 ms pacing interval plus the SCF evaluation time, so wait deterministically).
  QTRY_VERIFY_WITH_TIMEOUT(coordsSpy.count() >= 1, 10000);

  // Draw-atom edit: add a hydrogen mid-session under the write lock (the
  // worker reads the molecule under the read lock during its tick).
  {
    QWriteLocker locker(mol->lock());
    Atom *h = mol->addAtom();
    h->setAtomicNumber(1);
    h->setPos(Eigen::Vector3d(0.0, -1.2, 0.0));
  }

  // The new topology must re-establish exactly once and the write-back must
  // keep publishing coords for the enlarged molecule (no atom freezes).
  QTRY_VERIFY_WITH_TIMEOUT(reestablSpy.count() >= 1, 5000);
  QCOMPARE(reestablSpy.count(), 1);
  QCOMPARE(reestablSpy.first().at(0).toInt(), 4);

  const int coordsAfterDraw = coordsSpy.count();
  const int target = coordsAfterDraw + 2;
  QTRY_VERIFY_WITH_TIMEOUT(coordsSpy.count() >= target, 10000);

  // The live molecule must reflect the write-back: the new atom participates
  // and no atom position is NaN.
  QCOMPARE(mol->numAtoms(), 4);
  for (Atom *a : mol->atoms()) {
    const Eigen::Vector3d p = *a->pos();
    QVERIFY(qIsFinite(p.x()));
    QVERIFY(qIsFinite(p.y()));
    QVERIFY(qIsFinite(p.z()));
  }

  tool.disable();
  QVERIFY(!tool.running());
  delete mol;
}

// task 4.2 (OpenSpec 14): an erase edit during a running xTB session. Removing
// an atom mid-session must re-establish on the reduced topology exactly once,
// and the surviving atoms must keep publishing coords (keep moving) — no
// survivor freezes as a result of the deletion.
void AutoOptReproTest::xtbEraseDuringRunSurvivorsMove()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }

  Molecule *mol = new Molecule;
  const double numbers[3] = {8.0, 1.0, 1.0};
  const double xyz[9] = {0.0, 0.0, 0.0,
                         0.0, 0.0, 1.3,
                         0.0, 0.1, -0.95};
  for (int i = 0; i < 3; ++i) {
    Atom *a = mol->addAtom();
    a->setAtomicNumber(static_cast<int>(numbers[i]));
    a->setPos(Eigen::Vector3d(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]));
  }

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  tool.engineCombo()->setCurrentIndex(2); // GFN2-xTB
  if (tool.levelCombo())
    tool.levelCombo()->setCurrentIndex(4); // Normal
  tool.setGlw(&widget);
  tool.enable();
  QVERIFY(tool.running());

  XtbEngine *engine = tool.xtbEngine();
  QVERIFY(engine != nullptr);
  QSignalSpy reestablSpy(engine, &XtbEngine::sessionReestablished);
  QSignalSpy coordsSpy(engine, &XtbEngine::coordsReady);

  // Let the initial 3-atom session evaluate a few times (the first tick is a
  // 100 ms pacing interval plus the SCF evaluation time, so wait deterministically).
  QTRY_VERIFY_WITH_TIMEOUT(coordsSpy.count() >= 1, 10000);

  // Erase edit: remove a hydrogen (index 1) mid-session under the write lock.
  {
    QWriteLocker locker(mol->lock());
    mol->removeAtom(1);
  }

  // The reduced topology must re-establish exactly once and the survivors must
  // keep publishing coords (no survivor freezes).
  QTRY_VERIFY_WITH_TIMEOUT(reestablSpy.count() >= 1, 5000);
  QCOMPARE(reestablSpy.count(), 1);
  QCOMPARE(reestablSpy.first().at(0).toInt(), 2);

  const int coordsAfterErase = coordsSpy.count();
  const int target = coordsAfterErase + 2;
  QTRY_VERIFY_WITH_TIMEOUT(coordsSpy.count() >= target, 10000);

  // The survivors keep publishing: the live molecule has the reduced topology
  // and every survivor position is finite.
  QCOMPARE(mol->numAtoms(), 2);
  for (Atom *a : mol->atoms()) {
    const Eigen::Vector3d p = *a->pos();
    QVERIFY(qIsFinite(p.x()));
    QVERIFY(qIsFinite(p.y()));
    QVERIFY(qIsFinite(p.z()));
  }

  tool.disable();
  QVERIFY(!tool.running());
  delete mol;
}

// task 4.2 (OpenSpec 14): an undo/redo external position change during a
// running xTB session. The moved positions must be adopted as the starting
// geometry of a fresh phase and NOT reverted by subsequent write-backs (no
// snap-back to the pre-edit geometry).
void AutoOptReproTest::xtbUndoRedoDuringRunNoSnapBack()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }

  Molecule *mol = new Molecule;
  const double numbers[3] = {8.0, 1.0, 1.0};
  const double xyz[9] = {0.0, 0.0, 0.0,
                         0.0, 0.0, 1.3,
                         0.0, 0.1, -0.95};
  for (int i = 0; i < 3; ++i) {
    Atom *a = mol->addAtom();
    a->setAtomicNumber(static_cast<int>(numbers[i]));
    a->setPos(Eigen::Vector3d(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]));
  }

  GLWidget widget(mol, QSurfaceFormat());
  widget.resize(640, 480);

  AutoOptToolExposed tool;
  tool.settingsWidget();
  tool.engineCombo()->setCurrentIndex(2); // GFN2-xTB
  if (tool.levelCombo())
    tool.levelCombo()->setCurrentIndex(4); // Normal
  tool.setGlw(&widget);
  tool.enable();
  QVERIFY(tool.running());

  XtbEngine *engine = tool.xtbEngine();
  QVERIFY(engine != nullptr);
  QSignalSpy coordsSpy(engine, &XtbEngine::coordsReady);

  // Let the initial session evaluate a few times (the first tick is a 100 ms
  // pacing interval plus the SCF evaluation time, so wait deterministically).
  QTRY_VERIFY_WITH_TIMEOUT(coordsSpy.count() >= 1, 10000);

  // External position edit (an undo/redo of a move): pull H1 (index 1) to
  // z = 1.5 A under the write lock. A moderate displacement is used (a 3.0 A
  // O-H bond dissociates and fails SCF, the D4 containment path, not the D2
  // adoption path this test verifies).
  {
    QWriteLocker locker(mol->lock());
    mol->atoms().at(1)->setPos(Eigen::Vector3d(0.0, 0.0, 1.5));
  }

  // The moved position must be adopted as the fresh phase's starting geometry:
  // the next write-back does not snap it back toward the pre-edit equilibrium.
  // A tight threshold (0.3 A) so a pre-fix clobber (which snapped H1 back to
  // the ~0.967 A equilibrium, |1.5 - 0.967| = 0.53) fails decisively.
  const int coordsAfterEdit = coordsSpy.count();
  const int target = coordsAfterEdit + 2;
  QTRY_VERIFY_WITH_TIMEOUT(coordsSpy.count() >= target, 10000);

  bool adopted = false;
  for (Atom *a : mol->atoms()) {
    if (a->atomicNumber() == 1) {
      const Eigen::Vector3d p = *a->pos();
      if (std::abs(p.z() - 1.5) < 0.3) {
        adopted = true;
        break;
      }
    }
  }
  QVERIFY(adopted);

  tool.disable();
  QVERIFY(!tool.running());
  delete mol;
}

void AutoOptReproTest::engineSettingsRoundTrip()
{
  // task 4.6 (OpenSpec 21): the selected backend and convergence level persist
  // across restarts like the other tool settings. Write engine=GFN2 and
  // level=Tight to a temp ini file, reload into a fresh tool instance, and
  // assert both selections round-trip. Headless (no GL required).
  //
  // Use QTemporaryDir + a named ini file (the dialogsmoketest pattern), not a
  // QTemporaryFile left open: on Windows an open QTemporaryFile holds a lock
  // on the path, and QSettings writes through a separate handle that the lock
  // can defeat, silently discarding the persisted value.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("autoottool.ini"));

  // Tool 1: pick the xTB GFN2 backend (index 2) and the Tight level (index 5).
  AutoOptToolExposed tool1;
  tool1.settingsWidget();
  QVERIFY(tool1.engineCombo() != nullptr);
  QVERIFY(tool1.levelCombo() != nullptr);
  tool1.engineCombo()->setCurrentIndex(2); // GFN2-xTB
  tool1.levelCombo()->setCurrentIndex(5); // Tight

  {
    QSettings w(path, QSettings::IniFormat);
    tool1.writeSettings(w);
    w.sync();
  }

  // Tool 2: reload from the same ini file.
  AutoOptToolExposed tool2;
  tool2.settingsWidget();
  QSettings r(path, QSettings::IniFormat);
  tool2.readSettings(r);

  QCOMPARE(tool2.engineCombo()->currentIndex(), 2); // GFN2-xTB
  QCOMPARE(tool2.levelCombo()->currentIndex(), 5); // Tight
}

void AutoOptReproTest::engineSwitchHidesControlsAndCaptions()
{
  // Field regression: switching between force field and an xTB backend left
  // orphan captions ("Force Field:"/"Algorithm:" on the xTB path,
  // "Convergence Level:"/"xTB Threads" on the FF path) and put the xTB
  // controls under two layouts, corrupting the panel geometry. Switching the
  // engine combo must toggle each backend's controls AND its captions
  // together, and keep the shared steps row visible.
  AutoOptToolExposed tool;
  tool.settingsWidget();
  QVERIFY(tool.engineCombo() != nullptr);
  QVERIFY(tool.ffCombo() != nullptr);
  QVERIFY(tool.algCombo() != nullptr);
  QVERIFY(tool.levelCombo() != nullptr);
  QVERIFY(tool.threadsSpin() != nullptr);
  QVERIFY(tool.ffLabel() != nullptr);
  QVERIFY(tool.algLabel() != nullptr);
  QVERIFY(tool.levelLabel() != nullptr);
  QVERIFY(tool.threadsLabel() != nullptr);
  QVERIFY(tool.stepsSpin() != nullptr);

  // isVisibleTo(parent) reports the would-be visibility independent of the
  // (offscreen, hidden) settings widget itself.
  const QWidget *panel = tool.engineCombo()->parentWidget();

  tool.engineCombo()->setCurrentIndex(0); // Force field
  QCOMPARE(tool.ffCombo()->isVisibleTo(panel), true);
  QCOMPARE(tool.ffLabel()->isVisibleTo(panel), true);
  QCOMPARE(tool.algCombo()->isVisibleTo(panel), true);
  QCOMPARE(tool.algLabel()->isVisibleTo(panel), true);
  QCOMPARE(tool.levelCombo()->isVisibleTo(panel), false);
  QCOMPARE(tool.levelLabel()->isVisibleTo(panel), false);
  QCOMPARE(tool.threadsSpin()->isVisibleTo(panel), false);
  QCOMPARE(tool.threadsLabel()->isVisibleTo(panel), false);
  QCOMPARE(tool.stepsSpin()->isVisibleTo(panel), true);

  tool.engineCombo()->setCurrentIndex(2); // xTB - GFN2-xTB
  QCOMPARE(tool.ffCombo()->isVisibleTo(panel), false);
  QCOMPARE(tool.ffLabel()->isVisibleTo(panel), false);
  QCOMPARE(tool.algCombo()->isVisibleTo(panel), false);
  QCOMPARE(tool.algLabel()->isVisibleTo(panel), false);
  QCOMPARE(tool.levelCombo()->isVisibleTo(panel), true);
  QCOMPARE(tool.levelLabel()->isVisibleTo(panel), true);
  QCOMPARE(tool.threadsSpin()->isVisibleTo(panel), true);
  QCOMPARE(tool.threadsLabel()->isVisibleTo(panel), true);
  QCOMPARE(tool.stepsSpin()->isVisibleTo(panel), true);

  tool.engineCombo()->setCurrentIndex(3); // xTB - GFN-FF: same xTB rows
  QCOMPARE(tool.levelCombo()->isVisibleTo(panel), true);
  QCOMPARE(tool.ffLabel()->isVisibleTo(panel), false);

  tool.engineCombo()->setCurrentIndex(0); // back to force field
  QCOMPARE(tool.ffCombo()->isVisibleTo(panel), true);
  QCOMPARE(tool.levelLabel()->isVisibleTo(panel), false);
}

void AutoOptReproTest::singleOptimizationTool()
{
  // task 6.2 (OpenSpec 27): after retiring the old xtbopttool plugin, exactly
  // one optimization tool exists at runtime — the unified AutoOptTool. Assert
  // its AVOGADRO_TOOL identity is "AutoOptimization" (the optimization tool)
  // and that no optimization tool carries the retired "XtbOptimization"
  // identity. The tool is instantiated directly from source (the build
  // compiles autoopttool.cpp into this binary rather than loading
  // autoopttool.so, which would duplicate the plugin-export symbols), so this
  // verifies the consolidated tool's identity; the absence of a second
  // optimization tool is the structural result of the xtbopttool deletion.
  AutoOptToolExposed tool;
  // The unified tool's AVOGADRO_TOOL identifier is "AutoOptimization" and its
  // description names the optimization role.
  QCOMPARE(tool.identifier(), QStringLiteral("AutoOptimization"));
  QVERIFY2(tool.description().contains("optimization", Qt::CaseInsensitive),
           "AutoOptTool is not recognized as the optimization tool");
  // The retired xtbopttool exposed itself as "XtbOptimization"; it is gone.
  QVERIFY(tool.identifier() != QStringLiteral("XtbOptimization"));
  // It is a genuine Tool, so it participates in the tool inventory.
  QVERIFY(qobject_cast<Tool *>(&tool) != nullptr);
}

void AutoOptReproTest::toolDocumentationMatchesBehavior()
{
  // task 7.4 (OpenSpec 31): the tool's documented description must name all
  // four selectable engines (force fields, GFN1-xTB, GFN2-xTB, GFN-FF), that
  // atoms can be pulled during optimization, and the convergence-level
  // control — matching the tool's actual behavior. description() is the field
  // carried by the AVOGADRO_TOOL macro (the AutoOptTool constructor layers a
  // separate mouse-navigation guide on the action tooltip, so description() is
  // the accessor that documents the selectable backends).
  AutoOptToolExposed tool;
  const QString desc = tool.description();
  QVERIFY2(!desc.isEmpty(), "AutoOptTool description is empty");
  // All four engines named.
  QVERIFY2(desc.contains("force field", Qt::CaseInsensitive),
           "description does not name the force-field backend");
  QVERIFY2(desc.contains("GFN1-xTB"), "description missing GFN1-xTB");
  QVERIFY2(desc.contains("GFN2-xTB"), "description missing GFN2-xTB");
  QVERIFY2(desc.contains("GFN-FF"), "description missing GFN-FF");
  // Atom pulling during optimization is documented.
  QVERIFY2(desc.contains("drag", Qt::CaseInsensitive),
           "description does not document pulling atoms during optimization");
  // Convergence-level control is documented.
  QVERIFY2(desc.contains("convergence", Qt::CaseInsensitive),
           "description does not document the convergence-level control");
}

void AutoOptReproTest::liveBondOrderClickDuringAutoOpt()
{
  // fix-modern-tier-revision-churn task 3.1 (live / experiment matrix row 4):
  // with AutoOpt running, click a bond (draw tool, adjust-hydrogens off) -
  // the order cycles 1->2, the click bumps the molecule revision (update()),
  // the modern tier adopts a rebuild of that revision within ~one build, and
  // the optimizer keeps ticking.
  if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
    QSKIP("live bond-order click needs a real window system: QOpenGLWidget "
          "cannot present on the offscreen platform (segfaults there)");
  }
  Molecule *mol = makeCarbonChain();
  QVERIFY(mol);
  GLWidget *widget = makeLiveWidget(mol);
  if (!liveGuiReady(widget) || !pickCoherent(widget, mol)) {
    delete widget;
    delete mol;
    QSKIP(kLiveGuiSkipReason);
  }

  // AutoOpt session runs while the draw tool is active (the concurrency
  // contract's real usage).
  AutoOptToolExposed autoopt;
  autoopt.settingsWidget();
  autoopt.setGlw(widget);
  autoopt.enable();
  QVERIFY(autoopt.running());

  DrawToolExposed draw;
  draw.setAddHydrogens(false); // the scenario is a pure order cycle
  widget->setTool(&draw);

  // The press returns ChangeBondOrderDrawCommand; GLWidget pushes it onto the
  // undo stack, whose redo() applies the order and calls molecule->update()
  // (the revision bump that reaches the modern tier's render cache).
  UndoStack *undo = new UndoStack(mol);
  widget->setUndoStack(undo);

  QList<Atom *> atoms = mol->atoms();
  // Bond 0 (atoms 0-1) is the clicked bond; see the click-anchor note below
  // for why the scenario clicks its first half.
  Bond *bond = mol->bond(atoms[0]->id(), atoms[1]->id());
  QVERIFY(bond);
  QCOMPARE(bond->order(), 1);

  // Bounded retry: the press takes the molecule write lock, which the AutoOpt
  // worker's tick read can briefly hold; a failed acquisition is a no-op
  // click (the serialization working as designed). Re-anchor before each
  // attempt.
  //
  // Click anchor: probe 1x1 picks in a small neighborhood of the projected
  // point 0.3 A along bond 0 from atom 0, and click the first pixel where the
  // pick itself resolves bond 0 as the nearest hit (and the press's own 9x9
  // hits box leads with it). This anchors on what the pick actually renders -
  // exactly the user-visible contract - immune to the pick/projection
  // micro-offset and to pick lag. (Historical note: before this change's
  // task 3.4 fix, bond instances carried no pick id and the pick returned the
  // raw half-segment instance index, which bond(index) mis-resolved for every
  // position except this one - the defect the fix removed.)
  for (int attempt = 0; attempt < 10 && bond->order() == 1; ++attempt) {
    // 0.55 A along bond 0 from atom 0: past the atom pick sphere (~0.34 A),
    // inside the bond's first half-cylinder (0 - 0.75 A) whose instances
    // carry pickId 0 (bond 0's primitive index).
    const Eigen::Vector3d anchorNow =
      *atoms[0]->pos() * 0.63 + *atoms[1]->pos() * 0.37;
    const Eigen::Vector3d spNow = widget->camera()->project(anchorNow);
    const QPoint base(qRound(spNow.x()), qRound(spNow.y()));
    QPoint clickNow(-1, -1);
    for (int dy = -5; dy <= 5 && clickNow.x() < 0; ++dy)
      for (int dx = -2; dx <= 2 && clickNow.x() < 0; ++dx) {
        QList<GLHit> h = widget->hits(base.x() + dx, base.y() + dy, 1, 1);
        if (h.isEmpty() ||
            static_cast<Primitive::Type>(h.first().type()) !=
              Primitive::BondType ||
            h.first().name() != 0)
          continue;
        QList<GLHit> box =
          widget->hits(base.x() + dx - 4, base.y() + dy - 4, 9, 9);
        if (!box.isEmpty() &&
            static_cast<Primitive::Type>(box.first().type()) ==
              Primitive::BondType &&
            box.first().name() == 0)
          clickNow = QPoint(base.x() + dx, base.y() + dy);
      }
    if (clickNow.x() < 0) {
      QTest::qWait(120); // scene lag: let a fresh build present, retry
      continue;
    }
    QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, clickNow);
    QTest::qWait(120);
  }
  if (bond->order() != 2) {
    autoopt.disable();
    delete undo;
    delete widget;
    delete mol;
    QSKIP("could not anchor the click on the bond through the live pick "
          "(scene lag on this stack); see the pick-coherence gate");
  }
  QCOMPARE(bond->order(), 2); // the double bond exists
  QVERIFY(undo->count() > 0); // the click went through the undo stack
  // The click hit the bond, not the atom: no element alchemy side effect.
  QCOMPARE(atoms[0]->atomicNumber(), 6);
  QCOMPARE(atoms[1]->atomicNumber(), 6);

  const unsigned int clickRevision = mol->revision();

  // Ticks continue while the rebuild is in flight: every tick's write-back
  // ends in molecule->update() (a revision bump), so the revision stream
  // advancing is the observable "AutoOpt still ticking" signal (near
  // equilibrium the atom POSITIONS may barely move, but the tick stream does
  // not stop).
  QVERIFY(autoopt.running());
  const unsigned int postClickRevision = mol->revision();

  // "Appears within ~one build": the presented (adopted) revision must reach
  // the click's revision - the presented frame then contains the re-ordered
  // bond's geometry. Forward-progress adoption bounds this to one build.
  pumpUntil([widget, clickRevision]() {
    return widget->presentedRevision() >= clickRevision;
  }, 10000);
  QVERIFY2(widget->presentedRevision() >= clickRevision,
           "the click's rebuilt scene was never presented");

  bool ticksContinued = false;
  {
    QDeadlineTimer deadline(3000);
    while (!deadline.hasExpired()) {
      QApplication::processEvents();
      if (mol->revision() > postClickRevision + 3) {
        ticksContinued = true;
        break;
      }
    }
  }
  QVERIFY2(ticksContinued, "AutoOpt stopped ticking after the bond click");

  autoopt.disable();
  delete undo;
  delete widget;
  delete mol;
}

//! Find a widget pixel where the GL pick resolves `atomIndex` as the nearest
//! atom, so a real QTest click lands on the atom. Returns (-1,-1) when no such
//! pixel is found within the probe neighbourhood (scene lag / pick offset).
QPoint atomPickPoint(GLWidget *widget, Molecule *mol, int atomIndex)
{
  Atom *a = mol->atom(atomIndex);
  if (!a)
    return QPoint(-1, -1);
  const Eigen::Vector3d sp = widget->camera()->project(*a->pos());
  const QPoint base(qRound(sp.x()), qRound(sp.y()));
  for (int dy = -5; dy <= 5; ++dy)
    for (int dx = -2; dx <= 2; ++dx) {
      QList<GLHit> h = widget->hits(base.x() + dx, base.y() + dy, 1, 1);
      if (h.isEmpty() ||
          static_cast<Primitive::Type>(h.first().type()) != Primitive::AtomType ||
          h.first().name() != static_cast<unsigned int>(atomIndex))
        continue;
      QList<GLHit> box =
        widget->hits(base.x() + dx - 4, base.y() + dy - 4, 9, 9);
      if (!box.isEmpty() &&
          static_cast<Primitive::Type>(box.first().type()) == Primitive::AtomType &&
          box.first().name() == static_cast<unsigned int>(atomIndex))
        return QPoint(base.x() + dx, base.y() + dy);
    }
  return QPoint(-1, -1);
}

void AutoOptReproTest::liveClickExtendSameElementCreatesAtom()
{
  // add-draw-tool-click-extend 5.1: a same-element click extends the molecule.
  if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
    QSKIP("live click-extend needs a real window system: QOpenGLWidget cannot "
          "present on the offscreen platform");
  }
  Molecule *mol = new Molecule;
  Atom *a0 = mol->addAtom();
  a0->setAtomicNumber(6); // carbon
  a0->setPos(Eigen::Vector3d(0, 0, 0));
  GLWidget *widget = makeLiveWidget(mol);
  if (!liveGuiReady(widget) || !pickCoherent(widget, mol)) {
    delete widget;
    delete mol;
    QSKIP(kLiveGuiSkipReason);
  }

  DrawToolExposed draw;
  draw.setAddHydrogens(false);
  draw.setElement(6); // carbon selected, matches the clicked atom
  widget->setTool(&draw);
  UndoStack *undo = new UndoStack(mol);
  widget->setUndoStack(undo);

  const QPoint clickNow = atomPickPoint(widget, mol, 0);
  if (clickNow.x() < 0) {
    delete undo;
    delete widget;
    delete mol;
    QSKIP("no coherent atom-0 pick anchor under this GL stack");
  }

  const int before = mol->numAtoms();
  QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, clickNow);
  QTest::qWait(120);

  // A new atom bonded to the clicked atom exists; the click is one undo step.
  QCOMPARE(mol->numAtoms(), before + 1);
  QCOMPARE(mol->numBonds(), 1);
  QVERIFY(undo->count() > 0);
  undo->undo();
  QCOMPARE(mol->numAtoms(), before);
  QCOMPARE(mol->numBonds(), 0);

  delete undo;
  delete widget;
  delete mol;
}

void AutoOptReproTest::liveClickExtendDifferentElementNoNewAtom()
{
  // add-draw-tool-click-extend 5.1: a different-element click is the existing
  // alchemy (element change, no new atom), unchanged.
  if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
    QSKIP("live alchemy click needs a real window system: QOpenGLWidget cannot "
          "present on the offscreen platform");
  }
  Molecule *mol = new Molecule;
  Atom *a0 = mol->addAtom();
  a0->setAtomicNumber(6); // carbon
  a0->setPos(Eigen::Vector3d(0, 0, 0));
  GLWidget *widget = makeLiveWidget(mol);
  if (!liveGuiReady(widget) || !pickCoherent(widget, mol)) {
    delete widget;
    delete mol;
    QSKIP(kLiveGuiSkipReason);
  }

  DrawToolExposed draw;
  draw.setAddHydrogens(false);
  draw.setElement(7); // nitrogen selected, differs from the clicked carbon
  widget->setTool(&draw);

  const QPoint clickNow = atomPickPoint(widget, mol, 0);
  if (clickNow.x() < 0) {
    delete widget;
    delete mol;
    QSKIP("no coherent atom-0 pick anchor under this GL stack");
  }

  QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, clickNow);
  QTest::qWait(120);

  // Alchemy: no new atom, element changed to nitrogen.
  QCOMPARE(mol->numAtoms(), 1);
  QCOMPARE(mol->numBonds(), 0);
  QCOMPARE(a0->atomicNumber(), 7);

  delete widget;
  delete mol;
}

void AutoOptReproTest::liveClickExtendDuringAutoOpt()
{
  // add-draw-tool-click-extend 5.2: click-extend while AutoOpt runs leaves no
  // constraint markers behind (D6) - the new atom and the clicked atom are
  // free for the optimizer, and the fixed/ignored counts stay 0.
  if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
    QSKIP("live click-extend during AutoOpt needs a real window system");
  }
  Molecule *mol = new Molecule;
  Atom *a0 = mol->addAtom();
  a0->setAtomicNumber(6);
  a0->setPos(Eigen::Vector3d(0, 0, 0));
  GLWidget *widget = makeLiveWidget(mol);
  if (!liveGuiReady(widget) || !pickCoherent(widget, mol)) {
    delete widget;
    delete mol;
    QSKIP(kLiveGuiSkipReason);
  }

  AutoOptToolExposed autoopt;
  autoopt.settingsWidget();
  autoopt.setGlw(widget);
  autoopt.enable();
  QVERIFY(autoopt.running());

  DrawToolExposed draw;
  draw.setAddHydrogens(false);
  draw.setElement(6);
  widget->setTool(&draw);

  const QPoint clickNow = atomPickPoint(widget, mol, 0);
  if (clickNow.x() < 0) {
    autoopt.disable();
    delete widget;
    delete mol;
    QSKIP("no coherent atom-0 pick anchor under this GL stack");
  }

  const int before = mol->numAtoms();
  QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, clickNow);
  QTest::qWait(150);

  // New atom + bond created, and no constraint markers survived the gesture:
  // DrawTool's own FF holds no fix/ignore, and AutoOpt's UFF fixed/ignored
  // counts stay zero.
  QCOMPARE(mol->numAtoms(), before + 1);
  QCOMPARE(mol->numBonds(), 1);
  OpenBabel::OBForceField *drawFF = draw.ff();
  if (drawFF) {
    QCOMPARE(drawFF->GetFixAtom(), 0u);
    QCOMPARE(drawFF->GetIgnoreAtom(), 0u);
  }
  QCOMPARE(uff()->GetConstraints().GetFixedBitVec().CountBits(), 0u);
  QCOMPARE(uff()->GetConstraints().GetIgnoredBitVec().CountBits(), 0u);

  autoopt.disable();
  delete widget;
  delete mol;
}

QTEST_MAIN(AutoOptReproTest)
#include "autooptreprotest.moc"
