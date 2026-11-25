/**********************************************************************
  XtbEngine - Headless xTB optimization session engine

  See xtbengine.h for the interface. This file owns the xTB library objects
  for a session and drives a client-side L-BFGS optimizer over library
  singlepoints, paced by requestTick() so the GUI apply-rate controls CPU.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 ***********************************************************************/

#include "xtbengine.h"

#include "xtbparams.h"
#include "molecule.h"
#include "atom.h"
#include "bond.h"
#include <openbabel/elements.h>

#include <QtGlobal>
#include <thread>
#include <QReadWriteLock>
#include <QDir>
#include <cmath>
#include <limits>
#include <unordered_set>

// The MinGW libgfortran invalid-parameter workaround lives in xtbapi.cpp: the
// process-wide handler is installed in XtbApi::isAvailable() before the library
// is loaded, so it is active for the eager load, GFN-FF setup, and worker
// ticks (the handler covers all threads).

namespace Avogadro {

namespace {

// Unit conversions.
constexpr double kAngstromToBohr = 1.8897259886;
constexpr double kBohrToAngstrom = 0.52917721092;

// Approximate xTB optimizer level thresholds (Eh/bohr), tunable. Order matches
// design D5: crude, sloppy, loose, lax, normal, tight, vtight, extreme.
constexpr double kXtbLevelThresholds[8] = { 5e-3, 1e-3, 8e-4, 5e-4,
                                            3e-4, 1e-4, 3e-5, 1e-6 };
constexpr int kXtbMaxEvaluations = 500;

// Interactive accuracy adaptation (tunable).
constexpr double kXtbInteractiveAccuracy = 0.1;
constexpr int kXtbInteractiveMaxIter = 20;
// Design D6 (task 5.7 tuning): during an active drag, a tick may run up to
// this many L-BFGS iterations (each at the interactive SCF budget) so the
// constrained structure keeps pace with fast pointer motion.
constexpr int kDragStepsPerTick = 4;
constexpr double kXtbDefaultAccuracy = 1.0;
constexpr int kXtbDefaultMaxIter = 100;
// Design D3: the fresh phase's first evaluation after a drag release is
// attempted once at this elevated SCF iteration budget (2x the steady-state
// budget) so a strained-but-recoverable released geometry converges into the
// session instead of latching an evaluation failure. Bounded, deterministic.
constexpr int kXtbReleaseMaxIter = 2 * kXtbDefaultMaxIter;
// Design D9 (deferred-latch): after this many consecutive failed drag ticks
// the drag is held fixed on a geometry the backend genuinely cannot evaluate;
// then (and only then) the D4 containment latch engages. Bounded, so a held
// unconvergeable drag still idles with a named failure instead of burning CPU
// forever, while a moving (or held-but-recoverable) drag never latches from
// the pull alone (spec "no failure latch from the pull alone").
constexpr int kXtbDragFailLatchThreshold = 4;

// Design D4: tether force constant for the drag bond-preservation restraint
// (kJ/mol/A^2 -> Hartree/bohr^2). 1 Ha = 2625.4996 kJ/mol, 1 A = 1.8897259886
// bohr. This is the single tuning knob (see design.md D4).
constexpr double kKJmolPerHartree = 2625.4996;
constexpr double kXtbTetherForceKJmolA2 = 1000.0;
constexpr double kXtbTetherForce =
  kXtbTetherForceKJmolA2 / (kKJmolPerHartree * kAngstromToBohr * kAngstromToBohr);
// Design D3: repair-cap threshold (1.3x the covalent-radii sum, the same
// convention bondStrainText uses). When the press length exceeds this, the
// tether target is capped at the covalent-radii sum so grabbing a previously
// broken bond pulls it back together rather than holding the broken distance.
constexpr double kXtbTetherRepairThreshold = 1.3;
// Degenerate-bond guard: bonds shorter than this (bohr) at press are skipped.
constexpr double kXtbTetherMinLengthBohr = 1e-6;
// Design D8: non-bonded floor engagement threshold as a factor of the
// covalent-radii sum. Distinct from the 1.3 repair/strain threshold: for H-H
// the floor is 0.93 A, fully above the H2 bond (0.74 A), so new-bond
// character is suppressed while legitimate contacts (H...H on one carbon
// ~1.78 A, hydrogen bonds 1.8-2.0 A) are untouched.
constexpr double kXtbTetherFloorFactor = 1.5;
// Design D8: hydrogen-pair floor factor. The H covalent-radius sum (0.62 A)
// badly undershoots where the H-H repulsion wall sits: the xTB SCF's own
// H...H attraction equilibrium for a collapsed H-C-H geometry is ~1.0 A, so a
// 1.5x floor (0.93 A) engages only AFTER the pair has fallen into bonding
// range. Hydrogen-hydrogen pairs therefore get a 2.0x floor (1.24 A), which
// preempts the collapse while staying well below legitimate H...H contacts
// (geminal ~1.78 A, hydrogen bonds 1.8-2.4 A).
constexpr double kXtbTetherFloorFactorHydrogenPair = 2.0;
// Design D8: drawn bonds are hard-clamped to at most this factor of the
// covalent-radii sum (the same strain convention as the repair cap) and at
// least half of it (nuclear collision guard) by the bond-graph projection.
constexpr double kXtbBondMaxLengthFactor = kXtbTetherRepairThreshold;
constexpr double kXtbBondMinLengthFactor = 0.5;
// Design D8 projection: correction sweeps per trial geometry. Under fast
// drag stress many constraints violate simultaneously and two sweeps left
// measurable residuals (H-H down to ~1.11 A against a 1.24 A floor); ten
// sweeps converge the overlaps (cost is trivial next to the SCF).
constexpr int kXtbProjectionSweeps = 10;

constexpr int kErrorBufferSize = 1024;

} // namespace

XtbEngine::XtbEngine(QObject* parent)
  : QThread(parent)
  , m_env(nullptr)
  , m_mol(nullptr)
  , m_calc(nullptr)
  , m_results(nullptr)
  , m_objectsCreated(false)
  , m_molecule(nullptr)
  , m_method(1)
  , m_level(4)
  , m_threadsSetting(0)
  , m_energy(0.0)
  , m_evals(0)
  , m_gnorm(0.0)
  , m_initialEvalDone(false)
  , m_threshold(kXtbLevelThresholds[4])
  , m_maxEvaluations(kXtbMaxEvaluations)
  , m_tickPending(false)
  , m_stop(0)
  , m_draggedIndex(-1)
  , m_prevDragIndex(-1)
  , m_interactive(false)
  , m_converged(false)
  , m_resetHistoryRequested(false)
  , m_currentAccuracy(kXtbDefaultAccuracy)
  , m_releaseElevatedIter(false)
  , m_lastEnergy(0.0)
  , m_lastEvalCount(0)
  , m_coordsValid(false)
  , m_quiesced(false)
  , m_evalFailed(false)
  , m_dragFailCount(0)
  , m_setupBroken(false)
{
}

XtbEngine::~XtbEngine()
{
  stop();
  if (isRunning()) {
    wait();
  }
}

bool XtbEngine::setup(const std::vector<int>& atomicNumbers,
                      const std::vector<double>& positionsAngstrom, int method,
                      int level, int threadsSetting,
                      const std::vector<int>& bondPairs)
{
  if (!XtbApi::isAvailable()) {
    return false;
  }

  // Bootstrap the OpenMP environment first (D7). Respects a user-set env.
  applyOmpEnv(threadsSetting);

  m_numbers = atomicNumbers;
  m_method = method;
  m_level = level;
  m_threadsSetting = threadsSetting;
  m_threshold = kXtbLevelThresholds[(level >= 0 && level < 8) ? level : 4];
  m_maxEvaluations = kXtbMaxEvaluations;

  // Validate the elements before any library call: libxtb crashes or aborts
  // on elements it has no basis for, so an unsupported element must be
  // rejected here with a diagnostic rather than crash the process.
  if (!validateElements()) {
    return false;
  }

  // Convert angstrom positions to bohr.
  m_coordsBohr.resize(positionsAngstrom.size());
  for (size_t i = 0; i < positionsAngstrom.size(); ++i) {
    m_coordsBohr[i] = positionsAngstrom[i] * kAngstromToBohr;
  }
  m_grad.assign(m_coordsBohr.size(), 0.0);
  m_energy = 0.0;
  m_evals = 0;
  m_gnorm = 0.0;
  m_initialEvalDone = false;
  m_converged = false;
  m_lastEvalCount = 0;
  m_quiesced = false;
  // A fresh session must not inherit a failed-evaluation latch from a
  // previous one (design D4), nor a setup-broken latch (D2/D4: a failed
  // re-establishment in a prior session must not poison a fresh setup).
  m_evalFailed = false;
  m_evalFailureReason.clear();
  m_setupBroken = false;
  // A fresh session must not inherit the stop flag from a previous one; reset
  // it here so a re-setup after quiescing starts cleanly (not immediately
  // exiting run() because the prior stop() left the flag set).
  m_stop.storeRelaxed(0);
  m_lbfgs.resetHistory();
  // Design D8: the bond-graph projection is installed once; it reads the
  // worker-owned session state (numbers, bonds, drag index) on the worker
  // thread inside the optimizer's line search.
  m_lbfgs.setProjector(
    [this](std::vector<double>& coordsBohr) { projectBondGraph(coordsBohr); });
  // A fresh setup must not carry a stale drag index, release-elevation flag,
  // tether state, or floor state from a previous session.
  m_draggedIndex = -1;
  m_releaseElevatedIter = false;
  m_tetherBondIndices.clear();
  m_tetherTargetBohr.clear();
  m_projectionEnabled = true;
  m_prevDragIndex = -1;
  m_bondPairs = bondPairs;

  // Neutral closed-shell, non-periodic molecule.
  m_charge = 0.0;
  m_uhf = 0;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      m_lattice[r][c] = 0.0;
    }
    m_periodic[r] = false;
  }

  // GFN-FF loads its parameters from XTBHOME/XTBPATH at runtime, so the data
  // directory must be locatable before the method is loaded (D8). GFN1/GFN2
  // have compiled-in parameters and skip this.
  if (m_method == 2) {
    QString diagnostic;
    if (!XtbParams::ensureGfnffParameters(&diagnostic)) {
      emit setupFailed(diagnostic);
      return false;
    }
  }

  // Create the four xTB objects (under the engine mutex).
  {
    QMutexLocker locker(&m_mutex);
    if (!createObjects()) {
      return false;
    }
    if (!loadMethod()) {
      destroyObjects();
      return false;
    }
  }

  return true;
}

bool XtbEngine::setupFromMolecule(Molecule* molecule, int method, int level,
                                  int threadsSetting)
{
  if (molecule == nullptr) {
    return false;
  }

  // Snapshot the live molecule's topology and positions (A) under the read
  // lock, then hand the headless setup the vectors. The worker later re-reads
  // the live molecule per tick for re-baselining (design D2).
  std::vector<int> numbers;
  std::vector<double> positionsAngstrom;
  std::vector<int> bondPairs;
  {
    QReadLocker locker(molecule->lock());
    const int n = molecule->numAtoms();
    numbers.reserve(static_cast<size_t>(n));
    positionsAngstrom.reserve(static_cast<size_t>(n) * 3);
    bondPairs.reserve(static_cast<size_t>(molecule->numBonds()) * 2);
    foreach (const Atom* atom, molecule->atoms()) {
      numbers.push_back(atom->atomicNumber());
      const Eigen::Vector3d& p = *atom->pos();
      positionsAngstrom.push_back(p.x());
      positionsAngstrom.push_back(p.y());
      positionsAngstrom.push_back(p.z());
    }
    // Capture the initial bond graph so the first tick's comparison does not
    // spuriously re-establish (design D2/D5): the session's m_bondPairs must
    // match the live molecule's bonds until a real change occurs.
    foreach (const Bond* bond, molecule->bonds()) {
      if (bond == nullptr)
        continue;
      const Atom* beginAtom = bond->beginAtom();
      const Atom* endAtom = bond->endAtom();
      if (beginAtom == nullptr || endAtom == nullptr)
        continue;
      const int bi = static_cast<int>(beginAtom->index());
      const int ei = static_cast<int>(endAtom->index());
      if (bi < 0 || bi >= n || ei < 0 || ei >= n)
        continue;
      bondPairs.push_back(bi);
      bondPairs.push_back(ei);
    }
  }

  if (!setup(numbers, positionsAngstrom, method, level, threadsSetting,
             bondPairs)) {
    return false;
  }
  m_molecule = molecule;
  m_bondPairs = bondPairs;
  return true;
}

void XtbEngine::stop()
{
  m_stop.storeRelaxed(1);
  // Wake the worker so it observes the stop and quiesces.
  QMutexLocker locker(&m_stateMutex);
  m_tickCond.wakeAll();
}

void XtbEngine::requestTick()
{
  QMutexLocker locker(&m_stateMutex);
  m_tickPending = true;
  m_tickCond.wakeAll();
}

void XtbEngine::setDraggedAtom(int index)
{
  QMutexLocker locker(&m_stateMutex);
  if (m_draggedIndex == index) {
    // No change to the masked set. A continuing drag still resumes an idle
    // (converged) session so subsequent pointer moves keep registering.
    if (index >= 0)
      resumeOptimizationLocked();
    return;
  }
  m_draggedIndex = index;
  if (index >= 0) {
    // Press: the drag path is the authority for this atom's position, which
    // the worker reads from the live molecule on its per-tick re-baseline
    // (design D3). Publish the index for gradient zeroing, write-back skip and
    // the interactive-accuracy switch. The position itself is adopted from the
    // molecule; a press before the first pointer move reads the atom's
    // pre-drag position, which is the safe value the spec scenario requires.
    m_resetHistoryRequested = true;
    m_interactive = true;
    m_currentAccuracy = kXtbInteractiveAccuracy;
    m_coordsValid = false;
    // A new press starts a fresh drag; it must not consume the release-elevated
    // SCF budget left by a prior release (design D3 is scoped to the release's
    // first evaluation only).
    m_releaseElevatedIter = false;
    // Design D9 (deferred-latch): a fresh press starts a fresh failure streak.
    m_dragFailCount = 0;
    resumeOptimizationLocked();
  }
  else {
    // Release: clear the mask, restore full accuracy, and resume covering the
    // released atom from where the drag left it (spec "Drag ends"). Start a
    // fresh phase from the released geometry (the unmask changes the released
    // atom's gradient, so the old masked baseline is stale) and flag the first
    // evaluation for the elevated SCF budget (design D3).
    m_interactive = false;
    m_currentAccuracy = kXtbDefaultAccuracy;
    m_resetHistoryRequested = true;
    m_releaseElevatedIter = true;
    // Design D9 (deferred-latch): a release ends the failure streak; the
    // release-path evaluation latches immediately on failure (D4 unchanged).
    m_dragFailCount = 0;
    resetPhaseLocked();
  }
}

void XtbEngine::setTestFailurePattern(const std::vector<int>& failingTicks)
{
  // Test-only seam (design D9d): install a deterministic set of worker tick
  // indices that are treated as failed evaluations (NaN energy) without
  // touching the xTB library. Resets the counter so the pattern is indexed
  // from the next tick. An empty pattern restores production behavior.
  m_testFailingTicks = failingTicks;
  m_testTickCounter = 0;
}

void XtbEngine::resetPhaseLocked()
{
  // Start a fresh optimization phase from the current working geometry:
  // re-baseline the energy/gradient, reset the evaluation counter (the cap
  // bounds each phase, not the session's lifetime) and invalidate the
  // published snapshot. The history reset also zeroes LBfgs' internal
  // iteration counter, so a resumed phase gets a full step budget even when
  // the drag index did not change (e.g. pointer moves continuing past a
  // mid-drag convergence).
  m_converged = false;
  m_initialEvalDone = false;
  m_evals = 0;
  m_coordsValid = false;
  m_resetHistoryRequested = true;
  // Service the resume on the next loop pass even if no requestTick() is
  // pending (the session timer normally ticks, but an immediate wake makes
  // the pull register without waiting a timer period).
  m_tickPending = true;
  m_tickCond.wakeAll();
}

void XtbEngine::resumeOptimizationLocked()
{
  if (!m_converged)
    return;
  // A genuinely converged (or capped) idle session resumes from the dragged
  // geometry; resetPhaseLocked() re-baselines and re-arms the optimizer.
  resetPhaseLocked();
}

void XtbEngine::setInteractive(bool interactive)
{
  QMutexLocker locker(&m_stateMutex);
  if (m_interactive != interactive) {
    m_interactive = interactive;
    m_resetHistoryRequested = true;
  }
  m_currentAccuracy =
    interactive ? kXtbInteractiveAccuracy : kXtbDefaultAccuracy;
}

double XtbEngine::currentAccuracy() const
{
  QMutexLocker locker(&m_stateMutex);
  return m_currentAccuracy;
}

bool XtbEngine::takeLatestCoords(std::vector<double>& out)
{
  QMutexLocker locker(&m_stateMutex);
  if (!m_coordsValid) {
    return false;
  }
  out = m_latestCoords;
  m_coordsValid = false;
  return true;
}

bool XtbEngine::validateElements()
{
  // GFN1-xTB and GFN2-xTB have parameters for Z in [1, 86] (H-Rn). Elements
  // outside that range are not supported and would crash the library (which
  // has no basis for them), so reject them up front. GFN-FF is not validated
  // here (its parameter coverage is broader and is handled by the locator).
  if (m_method == 0 || m_method == 1) {
    for (int z : m_numbers) {
      if (z < 1 || z > 86) {
        emit setupFailed(
          QStringLiteral("xTB %1 does not support element Z=%2").arg(
            (m_method == 0) ? QStringLiteral("GFN1-xTB") : QStringLiteral("GFN2-xTB")).arg(z));
        return false;
      }
    }
  }
  return true;
}

void XtbEngine::applyOmpEnv(int threadsSetting)
{
  // OMP_WAIT_POLICY=PASSIVE and the auto OMP_NUM_THREADS are set once at
  // library load time (XtbApi::isAvailable), before the OpenMP runtime
  // initializes its thread pool. Mutating the process env via qputenv from
  // the GUI thread on every setup faults on Windows with a first-chance
  // access violation (the same mixed-runtime/DLL family as the
  // invalid-parameter fast-fail suppressed there), so the auto case is already
  // satisfied here and does NOT call qputenv. An explicit user thread count
  // (the UI choice wins, D7) is applied only when it differs from the current
  // value.
  if (threadsSetting > 0) {
    const QByteArray requested = QByteArray::number(threadsSetting);
    if (qEnvironmentVariable("OMP_NUM_THREADS") != requested) {
      XtbApi::setEnv("OMP_NUM_THREADS", requested.constData());
    }
  }
}

bool XtbEngine::createObjects()
{
  if (m_objectsCreated) {
    return true;
  }
  int natoms = static_cast<int>(m_numbers.size());
  m_env = XtbApi::xtb_newEnvironment()();
  // Keep the xTB SCF/optimizer chatter out of the application and test output.
  XtbApi::xtb_setVerbosity()(m_env, -1);
  m_mol = XtbApi::xtb_newMolecule()(m_env, &natoms, m_numbers.data(),
                                    m_coordsBohr.data(), &m_charge, &m_uhf,
                                    &m_lattice[0][0], m_periodic);
  m_calc = XtbApi::xtb_newCalculator()();
  m_results = XtbApi::xtb_newResults()();
  m_objectsCreated = true;
  return true;
}

bool XtbEngine::loadMethod()
{
  // GFN-FF loads its parameter file (.param_gfnff.xtb). xtb_loadGFNFF first
  // inquires the path we pass (or, with nullptr, a CWD-relative
  // '.param_gfnff.xtb') and only falls back to its own XTBPATH loop (rdpath)
  // when that inquire reports the file absent.
  //
  // On the bundled MinGW runtime the gfortran inquire itself fast-fails in a
  // console-less GUI process: it probes a file descriptor via _get_osfhandle
  // (fd 5, the gfortran standard-input unit, never opened in such a process)
  // and the UCRT default invalid-parameter policy terminates the process
  // (FAST_FAIL_INVALID_ARG / invoke_watson). This is independent of the file
  // path, so resolving the absolute path (below) is correct hygiene but is not
  // what prevents the crash: the crash is at fd resolution inside the inquire,
  // independent of the file path. The process-wide no-op invalid-parameter
  // handler installed in XtbApi::isAvailable() makes _get_osfhandle return -1
  // so the gfortran runtime completes the inquire instead of terminating. The
  // QByteArray must outlive the call, so it is declared at this scope.
  QByteArray gfnffParam;
  switch (m_method) {
    case 0:
      XtbApi::xtb_loadGFN1xTB()(m_env, m_mol, m_calc, nullptr);
      break;
    case 1:
      XtbApi::xtb_loadGFN2xTB()(m_env, m_mol, m_calc, nullptr);
      break;
    case 2: {
      gfnffParam = XtbParams::gfnffParameterPath().toUtf8();
      if (gfnffParam.isEmpty()) {
        // No resolvable param file (e.g. a stale user XTBHOME/XTBPATH that
        // points at a directory lacking .param_gfnff.xtb). Fail
        // deterministically instead of passing nullptr (which would leave
        // xtb_loadGFNFF to search its own XTBPATH loop; the invalid-parameter
        // guard above prevents that loop's inquire from terminating the
        // process, but an explicit diagnostic is still clearer).
        emit setupFailed(
          QStringLiteral("GFN-FF parameter data not found (probed XTBHOME,"
                         " XTBPATH, <appdir>/share/xtb, /usr/share/xtb)"));
        return false;
      }
#ifdef _WIN32
      // GFN-FF writes a topology cache file 'gfnff_topo' (relative path) into
      // the current working directory (xtb gfnff_setup.f90). The bundled MinGW
      // gfortran runtime cannot open it when the CWD path contains a space
      // ("Program Files") or is not writable for a standard user, and it aborts
      // the process on the resulting I/O error (gfortran_generate_error ->
      // common_exit -> ExitProcess). Run the calculator setup with the CWD
      // pointed at a writable, space-free temp directory, then restore it. The
      // cache is written once during newGFFCalculator, not per tick.
      const QString savedCwd = QDir::currentPath();
      QDir::setCurrent(QDir::tempPath());
      XtbApi::xtb_loadGFNFF()(m_env, m_mol, m_calc, gfnffParam.data());
      QDir::setCurrent(savedCwd);
#else
      XtbApi::xtb_loadGFNFF()(m_env, m_mol, m_calc, gfnffParam.data());
#endif
      break;
    }
    default:
      break;
  }

  if (XtbApi::xtb_checkEnvironment()(m_env) != 0) {
    int bufsize = kErrorBufferSize;
    char buffer[kErrorBufferSize];
    XtbApi::xtb_getError()(m_env, buffer, &bufsize);
    // xtb_getError does not reliably null-terminate the assembled error chain
    // within the buffer; terminate defensively so no uninitialized stack
    // bytes leak into the surfaced diagnostic.
    buffer[kErrorBufferSize - 1] = '\0';
    if (bufsize > 0 && bufsize <= kErrorBufferSize) {
      buffer[bufsize - 1] = '\0';
    }
    emit setupFailed(QString::fromLatin1(buffer));
    return false;
  }
  return true;
}

void XtbEngine::destroyObjects()
{
  if (m_objectsCreated) {
    if (m_results) {
      XtbApi::xtb_delResults()(&m_results);
    }
    if (m_calc) {
      XtbApi::xtb_delCalculator()(&m_calc);
    }
    if (m_mol) {
      XtbApi::xtb_delMolecule()(&m_mol);
    }
    if (m_env) {
      XtbApi::xtb_delEnvironment()(&m_env);
    }
    m_results = nullptr;
    m_calc = nullptr;
    m_mol = nullptr;
    m_env = nullptr;
    m_objectsCreated = false;
  }
}

double XtbEngine::evaluateOnce(const std::vector<double>& coords,
                               std::vector<double>& grad)
{
  // Defensive guard (D2/D4): never call into the xTB objects when they are
  // not created. A failed mid-session re-establishment destroys them and
  // latches m_setupBroken; if any path still reaches here (the latch is the
  // primary gate), calling xtb_setAccuracy()/xtb_singlepoint() on a null
  // environment is an access violation inside libxtb. Treat the tick as a
  // contained failure instead of a crash.
  if (!m_objectsCreated || m_env == nullptr) {
    latchEvaluationFailure(
      QStringLiteral("xTB objects unavailable (setup broken); restart the session"));
    return std::numeric_limits<double>::quiet_NaN();
  }

  int di = -1;
  double accuracy = kXtbDefaultAccuracy;
  int maxIter = kXtbDefaultMaxIter;
  {
    QMutexLocker locker(&m_stateMutex);
    di = m_draggedIndex;
    accuracy = m_currentAccuracy;
    maxIter = m_interactive ? kXtbInteractiveMaxIter : kXtbDefaultMaxIter;
    // Design D3: the fresh phase's first evaluation after a drag release is
    // attempted once at an elevated SCF iteration budget (2x the steady-state
    // budget), so a strained-but-recoverable released geometry converges into
    // the session instead of latching an evaluation failure. Consumed exactly
    // once; on failure the D4 containment latch applies unchanged.
    if (m_releaseElevatedIter) {
      maxIter = kXtbReleaseMaxIter;
      m_releaseElevatedIter = false;
    }
  }

  // The dragged atom's working coordinate already equals the pointer position:
  // the worker's per-tick re-baseline copies it from the live molecule (design
  // D3), and the line-search direction for the masked atom is zero (its
  // gradient is zeroed below), so the trial coordinates leave it where the
  // pointer put it. There is no separate drag-position side channel to read or
  // to guard — the molecule is the drag authority, so a press before the first
  // pointer move evaluates the atom at its pre-drag position (the safe value
  // the spec scenario requires) and never dereferences an empty vector.
  std::vector<double> pinned = coords;

  double energy = 0.0;
  {
    QMutexLocker locker(&m_mutex);
    XtbApi::xtb_setAccuracy()(m_env, m_calc, accuracy);
    XtbApi::xtb_setMaxIter()(m_env, m_calc, maxIter);
    XtbApi::xtb_updateMolecule()(m_env, m_mol, pinned.data(), nullptr);
    XtbApi::xtb_singlepoint()(m_env, m_mol, m_calc, m_results);

    // Design D4: after every singlepoint, check the xTB environment. A failed
    // SCF (for example a dissociated geometry the method cannot converge)
    // leaves the environment in an error state; the diagnostic is surfaced and
    // the tick is treated as failed.
    if (XtbApi::xtb_checkEnvironment()(m_env) != 0) {
      // Design D8 drag-phase containment refinement: the tether dynamics can
      // visit legitimate but SCF-awkward geometries within a drag (for
      // example a quasi-linear small molecule the tethers straighten). Before
      // latching, retry once at the release-elevation settings (full accuracy,
      // 2x the steady-state iteration budget) — the same bounded, deterministic
      // elevation the drag-release first evaluation gets. A fatal xTB error
      // leaves the environment in a state the C API cannot clear (calling the
      // library again on it corrupts its heap), so the retry runs on freshly
      // created objects — the same worker-side destroy/recreate discipline as
      // the D2 topology re-establishment. During an active drag only; a retry
      // that also fails (or a non-drag failure) takes the D4 containment latch
      // unchanged.
      if (di >= 0 && maxIter < kXtbReleaseMaxIter) {
        destroyObjects();
        if (createObjects() && loadMethod()) {
          XtbApi::xtb_setAccuracy()(m_env, m_calc, kXtbDefaultAccuracy);
          XtbApi::xtb_setMaxIter()(m_env, m_calc, kXtbReleaseMaxIter);
          XtbApi::xtb_updateMolecule()(m_env, m_mol, pinned.data(), nullptr);
          XtbApi::xtb_singlepoint()(m_env, m_mol, m_calc, m_results);
        }
      }
      if (XtbApi::xtb_checkEnvironment()(m_env) != 0) {
        int bufsize = kErrorBufferSize;
        char buffer[kErrorBufferSize];
        XtbApi::xtb_getError()(m_env, buffer, &bufsize);
        // Defensive termination: see the loadMethod() error path (the error
        // chain is not reliably null-terminated by the C API).
        buffer[kErrorBufferSize - 1] = '\0';
        if (bufsize > 0 && bufsize <= kErrorBufferSize) {
          buffer[bufsize - 1] = '\0';
        }
        QString reason = QString::fromLatin1(buffer);
        // The library's error-chain assembly can splice non-printable
        // fragments into the message; strip them so the surfaced diagnostic
        // (HUD + log stream) stays clean.
        // xtb diagnostics are pure ASCII; the library's error-chain assembly
        // can splice binary fragments (with embedded ASCII bytes) after the
        // real message, so truncate at the first non-ASCII byte and trim the
        // tail. A clean message is unaffected.
        int cut = reason.size();
        for (int i = 0; i < reason.size(); ++i) {
          const char16_t u = reason[i].unicode();
          if (u >= 0x7F || (u < 0x20 && u != '\n')) {
            cut = i;
            break;
          }
        }
        reason = reason.left(cut);
        while (reason.endsWith('\n') || reason.endsWith(' ')) {
          reason.chop(1);
        }
        if (reason.isEmpty()) {
          reason = QStringLiteral("xTB singlepoint failed");
        }
        // Design D9 (deferred-latch): during an active drag a failed
        // evaluation is surfaced but does not latch immediately — a hard pull
        // visits SCF-awkward geometries the backend sometimes cannot converge
        // (design D8 residual), and the spec scenario forbids a latch from the
        // pull alone. Record the raw reason and return NaN; the worker tick
        // loop counts consecutive failed ticks and engages the D4 latch only
        // when the drag is held fixed on a genuinely unconvergeable geometry.
        // Non-drag and drag-release failures latch immediately (D4 unchanged).
        {
          QMutexLocker locker(&m_stateMutex);
          m_evalFailureReason = reason;
        }
        if (di >= 0) {
          qWarning() << "AutoOpt xTB evaluation failed:" << reason;
          return std::numeric_limits<double>::quiet_NaN();
        }
        latchEvaluationFailure(reason);
        return std::numeric_limits<double>::quiet_NaN();
      }
    }

    XtbApi::xtb_getEnergy()(m_env, m_results, &energy);
    XtbApi::xtb_getGradient()(m_env, m_results, grad.data());
  }

  // Design D4: independently guard finiteness. A non-finite energy, gradient,
  // or coordinate must never reach the renderer via a published snapshot.
  {
    bool finite = std::isfinite(energy);
    if (finite) {
      for (size_t i = 0; i < grad.size() && finite; ++i) {
        finite = std::isfinite(grad[i]);
      }
    }
    if (finite) {
      for (size_t i = 0; i < pinned.size() && finite; ++i) {
        finite = std::isfinite(pinned[i]);
      }
    }
    if (!finite) {
      const QString reason =
        QStringLiteral("xTB evaluation returned non-finite results");
      // Design D9 (deferred-latch): a non-finite result during an active drag
      // defers the latch exactly like an SCF failure (see above); the tick
      // loop counts consecutive failed ticks.
      {
        QMutexLocker locker(&m_stateMutex);
        m_evalFailureReason = reason;
      }
      if (di >= 0) {
        qWarning() << "AutoOpt xTB evaluation failed:" << reason;
        return std::numeric_limits<double>::quiet_NaN();
      }
      latchEvaluationFailure(reason);
      return std::numeric_limits<double>::quiet_NaN();
    }
  }

  // Zero the dragged atom's gradient so the optimizer treats it as fixed.
  if (di >= 0 && static_cast<size_t>(di * 3 + 2) < grad.size()) {
    for (int k = 0; k < 3; ++k) {
      grad[di * 3 + k] = 0.0;
    }
  }

  // Design D1/D7: external tether for EVERY drawn bond (bond-graph
  // invariance). For each tethered bond with ends (a, c) and target d0
  // (bohr), add a harmonic term to the session's own energy/gradient:
  //   E += 0.5*k*(|r_a - r_c| - d0)^2
  //   grad_end += k*(|r_a - r_c| - d0) * (r_end - r_other)/|r_a - r_c|
  // Ends equal to the dragged atom are skipped (its gradient was zeroed above
  // and its position is pointer-pinned), so the tether acts only on the other
  // atoms. E and grad stay mutually consistent (valid L-BFGS curvature
  // pairs). Guard the degenerate (zero-distance) case. Tether state is
  // worker-owned and stable within this call, so no lock is taken.
  if (di >= 0) {
    const int n3 = static_cast<int>(pinned.size());
    // Design D1/D7: external tether for EVERY drawn bond (bond-graph
    // invariance). For each tethered bond with ends (a, c) and target d0
    // (bohr), add a harmonic term to the session's own energy/gradient:
    //   E += 0.5*k*(|r_a - r_c| - d0)^2
    //   grad_end += k*(|r_a - r_c| - d0) * (r_end - r_other)/|r_a - r_c|
    // Ends equal to the dragged atom are skipped (its gradient was zeroed
    // above and its position is pointer-pinned), so the tether acts only on
    // the other atoms. E and grad stay mutually consistent (valid L-BFGS
    // curvature pairs). Guard the degenerate (zero-distance) case. Tether
    // state is worker-owned and stable within this call, so no lock is taken.
    if (!m_tetherBondIndices.empty()) {
      for (size_t t = 0; t + 1 < m_tetherBondIndices.size(); t += 2) {
        const int a = m_tetherBondIndices[t];
        const int c = m_tetherBondIndices[t + 1];
        if (t / 2 >= m_tetherTargetBohr.size()) {
          continue;
        }
        const double d0 = m_tetherTargetBohr[t / 2];
        if (a < 0 || a * 3 + 2 >= n3 || c < 0 || c * 3 + 2 >= n3) {
          continue;
        }
        const double dx = pinned[a * 3] - pinned[c * 3];
        const double dy = pinned[a * 3 + 1] - pinned[c * 3 + 1];
        const double dz = pinned[a * 3 + 2] - pinned[c * 3 + 2];
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < kXtbTetherMinLengthBohr) {
          continue;
        }
        const double stretch = dist - d0;
        const double factor = kXtbTetherForce * stretch / dist;
        if (a != di) {
          grad[a * 3] += factor * dx;
          grad[a * 3 + 1] += factor * dy;
          grad[a * 3 + 2] += factor * dz;
        }
        if (c != di) {
          grad[c * 3] += factor * (-dx);
          grad[c * 3 + 1] += factor * (-dy);
          grad[c * 3 + 2] += factor * (-dz);
        }
        energy += 0.5 * kXtbTetherForce * stretch * stretch;
      }
    }
  }


  {
    QMutexLocker locker(&m_stateMutex);
    m_lastEnergy = energy;
    m_lastEvalCount = m_evals;
    // Publish the pinned coordinates (the dragged atom equals the pointer).
    m_latestCoords = pinned;
    m_coordsValid = true;
  }
  return energy;
}

bool XtbEngine::shouldStop(int evals) const
{
  if (m_stop.loadRelaxed()) {
    return true;
  }
  return evals >= m_maxEvaluations;
}

double XtbEngine::rmsGradientNorm() const
{
  const int natoms = static_cast<int>(m_coordsBohr.size() / 3);
  if (natoms <= 0) {
    return m_gnorm;
  }
  // L2 norm over 3N components -> RMS per atom: divide by sqrt(3N).
  return m_gnorm / std::sqrt(3.0 * static_cast<double>(natoms));
}

void XtbEngine::publishCoords(const std::vector<double>& coordsBohr)
{
  emit coordsReady(coordsBohr);
}

void XtbEngine::writeBackToMolecule()
{
  // Headless vector setup (tests) has no live source: nothing to keep in sync.
  if (m_molecule == nullptr) {
    return;
  }
  // Bounded-retry write acquisition, mirroring readLiveMolecule: retry for a
  // bounded number of spins yielding, and bail on stop/quiesce so a GUI writer
  // holding the lock never wedges the worker (and quiesce stays deadlock-free).
  int spins = 0;
  while (!m_molecule->lock()->tryLockForWrite()) {
    if (m_stop.loadRelaxed()) {
      return;
    }
    if (++spins > 4096) {
      return;
    }
    std::this_thread::yield();
  }
  // Named-constructor RAII holder that takes ownership of the ALREADY-acquired
  // lock without re-acquiring (QWriteLocker would deadlock on a non-re-entrant
  // QReadWriteLock). An explicit constructor is used instead of aggregate init
  // so this compiles on MSVC, which rejects aggregate-init of a class with a
  // user-provided destructor (the Windows installer build is strict).
  struct WriteLockGuard {
    explicit WriteLockGuard(QReadWriteLock* l) : lock(l) {}
    ~WriteLockGuard() { if (lock) lock->unlock(); }
    QReadWriteLock* lock;
  } writeLockGuard(m_molecule->lock());

  // Guard against a topology change between the read and this write: if the
  // atom count no longer matches, skip rather than writing out of bounds.
  const int n = m_molecule->numAtoms();
  if (n != static_cast<int>(m_coordsBohr.size() / 3)) {
    return;
  }
  // The dragged atom's position is the drag path's authority (the molecule,
  // driven by the tool's pointer interaction), not the optimizer's. Writing it
  // back would fight the drag and, worse, race the GUI-thread writer that holds
  // the same write lock. Skip it, exactly as the tool's write-back does.
  int dragIndex = -1;
  {
    QMutexLocker stateLocker(&m_stateMutex);
    dragIndex = m_draggedIndex;
  }
  // Preserve external edits (design D2): an atom whose position in the live
  // molecule no longer matches the snapshot read at the start of this tick was
  // changed externally (another tool, undo, a drag by another surface) after
  // the worker read it. Writing the optimizer's working coords back would
  // clobber that edit before the next tick could adopt it (the edit must win
  // and start a fresh phase). Skip such atoms; the next tick's re-baseline
  // diff (molecule vs m_coordsBohr) sees the edit and adopts it.
  constexpr double kEditEpsilonBohr = 1e-6;
  const bool haveSnapshot =
    m_readSnapshotBohr.size() == m_coordsBohr.size();
  for (int i = 0; i < n; ++i) {
    if (i == dragIndex) {
      continue;
    }
    if (haveSnapshot) {
      bool externallyEdited = false;
      for (int k = 0; k < 3; ++k) {
        const double currentBohr =
          m_molecule->atoms().at(i)->pos()->coeff(k) * kAngstromToBohr;
        if (std::abs(currentBohr - m_readSnapshotBohr[i * 3 + k]) >
            kEditEpsilonBohr) {
          externallyEdited = true;
          break;
        }
      }
      if (externallyEdited) {
        continue;
      }
    }
    Eigen::Vector3d p(
      m_coordsBohr[i * 3] * kBohrToAngstrom,
      m_coordsBohr[i * 3 + 1] * kBohrToAngstrom,
      m_coordsBohr[i * 3 + 2] * kBohrToAngstrom);
    m_molecule->atoms().at(i)->setPos(p);
  }
}

void XtbEngine::deleteObjectsOnWorker()
{
  // Guard so the four xTB objects are deleted exactly once, even if run() is
  // somehow entered more than once for the same lifetime.
  if (m_quiesced) {
    return;
  }
  m_quiesced = true;
  // The live-molecule source (if any) must not be dereferenced after the
  // session ends, so drop it here on the worker thread (design D2).
  m_molecule = nullptr;
  // Clear tether state on quiesce (design D5): no stale restraint may
  // survive into a later session on the same engine object.
  m_tetherBondIndices.clear();
  m_tetherTargetBohr.clear();
  m_prevDragIndex = -1;
  QMutexLocker locker(&m_mutex);
  destroyObjects();
}

bool XtbEngine::isEvaluationFailedLocked() const
{
  return m_evalFailed;
}

void XtbEngine::latchEvaluationFailure(const QString& reason)
{
  // A failed singlepoint latches the phase idle-with-failure (design D4): no
  // further evaluations for this phase, no publish, and the reason is surfaced
  // via the evaluationFailed signal. The latch is cleared on Stop/Start (a
  // fresh setup) or a topology re-establishment (resetPhaseForReBaseline).
  // Design D4: name the recovery path so a contained failure is not a silent
  // frozen session — the clause is appended to every surfaced message.
  const QString withRecovery =
    reason + QStringLiteral("; restart the session (Stop/Start) or edit the "
                            "molecule to re-establish it");
  // Surface the failure in the debug/log stream as well as the HUD: the HUD
  // line is transient paint state, so a captured debug log (or a debugger)
  // would otherwise show nothing when a session errors out.
  qWarning() << "AutoOpt xTB evaluation failed:" << withRecovery;
  {
    QMutexLocker locker(&m_stateMutex);
    m_evalFailed = true;
    m_evalFailureReason = withRecovery;
    m_coordsValid = false;
  }
  emit evaluationFailed(withRecovery);
}

bool XtbEngine::readLiveMolecule(std::vector<int>& numbers,
                                 std::vector<double>& positionsBohr,
                                 std::vector<int>& bondPairs)
{
  // Headless vector setup (tests) has no live source: skip re-baselining.
  if (m_molecule == nullptr) {
    return false;
  }

  // Worker-side read of the live molecule holds the read lock so it cannot
  // race GUI-thread addAtom/removeAtom/setPos during draw strokes. Acquisition
  // is bounded (the AutoOptThread::update pattern): retry for a bounded number
  // of spins, yielding, and bail out immediately on a stop (quiesce) or when
  // the retry budget is exhausted. A GUI writer holding the write lock must
  // never wedge the worker here during quiesce.
  int spins = 0;
  while (!m_molecule->lock()->tryLockForRead()) {
    if (m_stop.loadRelaxed()) {
      return false;
    }
    if (++spins > 4096) {
      return false;
    }
    std::this_thread::yield();
  }

  // Named-constructor RAII holder that takes ownership of the ALREADY-acquired
  // lock without re-acquiring (QReadLocker would deadlock on a non-re-entrant
  // QReadWriteLock). An explicit constructor is used instead of aggregate init
  // so this compiles on MSVC, which rejects aggregate-init of a class with a
  // user-provided destructor (the Windows installer build is strict).
  struct ReadLockGuard {
    explicit ReadLockGuard(QReadWriteLock* l) : lock(l) {}
    ~ReadLockGuard() { if (lock) lock->unlock(); }
    QReadWriteLock* lock;
  } readLockGuard(m_molecule->lock());

  const int n = m_molecule->numAtoms();
  numbers.clear();
  numbers.reserve(static_cast<size_t>(n));
  positionsBohr.clear();
  positionsBohr.reserve(static_cast<size_t>(n) * 3);
  foreach (const Atom* atom, m_molecule->atoms()) {
    numbers.push_back(atom->atomicNumber());
    const Eigen::Vector3d& p = *atom->pos();
    positionsBohr.push_back(p.x() * kAngstromToBohr);
    positionsBohr.push_back(p.y() * kAngstromToBohr);
    positionsBohr.push_back(p.z() * kAngstromToBohr);
  }
  // Copy the bond list (flat begin/end atom-index pairs) under the same read
  // lock, so the tether capture and the topology scan see a consistent bond
  // graph (design D2). Bonds whose endpoint atom index is out of range (a torn
  // read) are skipped rather than trusted.
  bondPairs.clear();
  bondPairs.reserve(static_cast<size_t>(m_molecule->numBonds()) * 2);
  foreach (const Bond* bond, m_molecule->bonds()) {
    if (bond == nullptr) {
      continue;
    }
    const Atom* beginAtom = bond->beginAtom();
    const Atom* endAtom = bond->endAtom();
    if (beginAtom == nullptr || endAtom == nullptr) {
      continue;
    }
    const int beginIndex = static_cast<int>(beginAtom->index());
    const int endIndex = static_cast<int>(endAtom->index());
    if (beginIndex < 0 || beginIndex >= n || endIndex < 0 || endIndex >= n) {
      continue;
    }
    bondPairs.push_back(beginIndex);
    bondPairs.push_back(endIndex);
  }
  return true;
}

// static
void XtbEngine::computeTetherTargets(int dragIndex,
                                     const std::vector<int>& numbers,
                                     const std::vector<int>& bondPairs,
                                     const std::vector<double>& positionsBohr,
                                     std::vector<int>& partnerOut,
                                     std::vector<double>& targetOut)
{
  partnerOut.clear();
  targetOut.clear();
  const int n = static_cast<int>(numbers.size());
  if (dragIndex < 0 || dragIndex >= n) {
    return;
  }
  if (positionsBohr.size() < static_cast<size_t>(n * 3)) {
    return;
  }
  // Degenerate input: an invalid dragged element gets no tether.
  if (numbers[dragIndex] <= 0) {
    return;
  }
  // Design D7: every drawn bond is tethered to its press length so the drawn
  // bond graph is an invariant of the drag-phase optimization. The repair cap
  // (D3) stays scoped to the bonds incident to the dragged atom so the drag
  // does not silently relax the rest of the molecule. partnerOut receives
  // flat atom-index pairs (two entries per bond); evaluateOnce applies the
  // restraint to both ends (ends equal to the dragged atom are skipped there:
  // its gradient is zeroed and its position is pointer-pinned).
  for (size_t b = 0; b + 1 < bondPairs.size(); b += 2) {
    const int a = bondPairs[b];
    const int c = bondPairs[b + 1];
    if (a < 0 || a >= n || c < 0 || c >= n || a == c) {
      continue;
    }
    // Degenerate input: an invalid element gets no tether.
    if (numbers[a] <= 0 || numbers[c] <= 0) {
      continue;
    }
    // Press length in bohr (the bond as it stands when grabbed).
    const double dx = positionsBohr[a * 3] - positionsBohr[c * 3];
    const double dy = positionsBohr[a * 3 + 1] - positionsBohr[c * 3 + 1];
    const double dz = positionsBohr[a * 3 + 2] - positionsBohr[c * 3 + 2];
    const double pressLength = std::sqrt(dx * dx + dy * dy + dz * dz);
    // Skip degenerate (near-zero-length) bonds at press.
    if (pressLength < kXtbTetherMinLengthBohr) {
      continue;
    }
    // Covalent-radii sum in bohr, from the OpenBabel element table already
    // linked into libavogadro (repair-cap bound and degeneracy guard).
    const double covSumBohr =
      (OpenBabel::OBElements::GetCovalentRad(static_cast<unsigned int>(numbers[a])) +
       OpenBabel::OBElements::GetCovalentRad(static_cast<unsigned int>(numbers[c]))) *
      kAngstromToBohr;
    // Degenerate radii (an element the table does not know): skip the bond.
    if (covSumBohr <= kXtbTetherMinLengthBohr) {
      continue;
    }
    double d0 = pressLength;
    if ((a == dragIndex || c == dragIndex) &&
        pressLength > kXtbTetherRepairThreshold * covSumBohr) {
      d0 = covSumBohr;
    }
    partnerOut.push_back(a);
    partnerOut.push_back(c);
    targetOut.push_back(d0);
  }
}

// Design D8: project a geometry onto the drawn-bond-graph manifold
// (bond-centric enforcement, SHAKE-style). Drawn bonds are clamped inside
// [0.5, 1.3] x the covalent-radii sum and non-bonded pairs are pushed out of
// new-bond range (1.5x the covalent-radii sum, 2.0x for hydrogen-hydrogen
// pairs). Corrections move only non-dragged atoms: the dragged atom keeps
// pointer authority, so a constraint touching it is satisfied entirely by
// moving the other end. Deterministic for identical input.
void XtbEngine::projectBondGraph(std::vector<double>& coordsBohr) const
{
  const int n = static_cast<int>(m_numbers.size());
  if (n < 2 || coordsBohr.size() < static_cast<size_t>(n * 3)) {
    return;
  }
  int di = -1;
  {
    QMutexLocker locker(&m_stateMutex);
    di = m_draggedIndex;
  }

  // Per-atom covalent radii (bohr); atoms with unknown radii opt out of
  // constraints they participate in (degenerate input, design D3).
  std::vector<double> covRad(static_cast<size_t>(n), 0.0);
  for (int i = 0; i < n; ++i) {
    const int z = m_numbers[static_cast<size_t>(i)];
    if (z > 0) {
      covRad[static_cast<size_t>(i)] =
        OpenBabel::OBElements::GetCovalentRad(static_cast<unsigned int>(z)) *
        kAngstromToBohr;
    }
  }

  // Bonded-pair lookup for the non-bonded sweep (encoded min,max keys).
  std::unordered_set<uint64_t> bonded;
  bonded.reserve(m_bondPairs.size() * 2 + 8);
  for (size_t b = 0; b + 1 < m_bondPairs.size(); b += 2) {
    const int a = m_bondPairs[b];
    const int c = m_bondPairs[b + 1];
    if (a < 0 || a >= n || c < 0 || c >= n || a == c) {
      continue;
    }
    const int lo = std::min(a, c);
    const int hi = std::max(a, c);
    bonded.insert(static_cast<uint64_t>(static_cast<uint32_t>(lo)) *
                    static_cast<uint64_t>(static_cast<uint32_t>(n)) +
                  static_cast<uint64_t>(static_cast<uint32_t>(hi)));
  }

  auto weightA = [di](int a, int c) {
    if (a == di) {
      return 0.0;
    }
    return (c == di) ? 1.0 : 0.5;
  };

  for (int sweep = 0; sweep < kXtbProjectionSweeps; ++sweep) {
    bool changed = false;
    // Drawn bonds: clamp into [min, max] x covSum so no drawn bond is
    // severed past its re-formation reach and none collapses.
    for (size_t b = 0; b + 1 < m_bondPairs.size(); b += 2) {
      const int a = m_bondPairs[b];
      const int c = m_bondPairs[b + 1];
      if (a < 0 || a >= n || c < 0 || c >= n || a == c) {
        continue;
      }
      const double covSum = covRad[static_cast<size_t>(a)] +
                            covRad[static_cast<size_t>(c)];
      if (covSum <= kXtbTetherMinLengthBohr) {
        continue;
      }
      const double maxLen = kXtbBondMaxLengthFactor * covSum;
      const double minLen = kXtbBondMinLengthFactor * covSum;
      const double dx = coordsBohr[static_cast<size_t>(a) * 3] -
                        coordsBohr[static_cast<size_t>(c) * 3];
      const double dy = coordsBohr[static_cast<size_t>(a) * 3 + 1] -
                        coordsBohr[static_cast<size_t>(c) * 3 + 1];
      const double dz = coordsBohr[static_cast<size_t>(a) * 3 + 2] -
                        coordsBohr[static_cast<size_t>(c) * 3 + 2];
      const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (dist < kXtbTetherMinLengthBohr ||
          (dist <= maxLen && dist >= minLen)) {
        continue;
      }
      const double target = (dist > maxLen) ? maxLen : minLen;
      const double wa = weightA(a, c);
      const double wc = weightA(c, a);
      const double shiftA = wa * (target - dist) / dist;
      const double shiftC = wc * (target - dist) / dist;
      for (int k = 0; k < 3; ++k) {
        const double unit = (coordsBohr[static_cast<size_t>(a) * 3 + k] -
                             coordsBohr[static_cast<size_t>(c) * 3 + k]) /
                            dist;
        coordsBohr[static_cast<size_t>(a) * 3 + k] += shiftA * unit;
        coordsBohr[static_cast<size_t>(c) * 3 + k] -= shiftC * unit;
      }
      changed = true;
    }
    // Non-bonded pairs: push out of new-bond range.
    for (int i = 0; i < n; ++i) {
      if (covRad[static_cast<size_t>(i)] <= 0.0) {
        continue;
      }
      for (int j = i + 1; j < n; ++j) {
        if (covRad[static_cast<size_t>(j)] <= 0.0) {
          continue;
        }
        const uint64_t key = static_cast<uint64_t>(static_cast<uint32_t>(i)) *
                               static_cast<uint64_t>(static_cast<uint32_t>(n)) +
                             static_cast<uint64_t>(static_cast<uint32_t>(j));
        if (bonded.count(key) != 0) {
          continue;
        }
        const double rmin =
          ((m_numbers[static_cast<size_t>(i)] == 1 &&
            m_numbers[static_cast<size_t>(j)] == 1)
             ? kXtbTetherFloorFactorHydrogenPair
             : kXtbTetherFloorFactor) *
          (covRad[static_cast<size_t>(i)] + covRad[static_cast<size_t>(j)]);
        const double dx = coordsBohr[static_cast<size_t>(i) * 3] -
                          coordsBohr[static_cast<size_t>(j) * 3];
        const double dy = coordsBohr[static_cast<size_t>(i) * 3 + 1] -
                          coordsBohr[static_cast<size_t>(j) * 3 + 1];
        const double dz = coordsBohr[static_cast<size_t>(i) * 3 + 2] -
                          coordsBohr[static_cast<size_t>(j) * 3 + 2];
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < kXtbTetherMinLengthBohr || dist >= rmin) {
          continue;
        }
        const double wa = weightA(i, j);
        const double wc = weightA(j, i);
        const double shiftI = wa * (rmin - dist) / dist;
        const double shiftJ = wc * (rmin - dist) / dist;
        for (int k = 0; k < 3; ++k) {
          const double unit = (coordsBohr[static_cast<size_t>(i) * 3 + k] -
                               coordsBohr[static_cast<size_t>(j) * 3 + k]) /
                              dist;
          // Push the ends APART: i moves along (i - j), j along (j - i).
          coordsBohr[static_cast<size_t>(i) * 3 + k] += shiftI * unit;
          coordsBohr[static_cast<size_t>(j) * 3 + k] -= shiftJ * unit;
        }
        changed = true;
      }
    }
    // Early-exit (design D8 hardening): once a full sweep applies no
    // correction, every constraint is satisfied; the sweep count is a cap,
    // not a fixed depth, so the residual (for example the thin ethane H-H
    // margin) never depends on running the full count. Cost is trivial next
    // to the SCF.
    if (!changed) {
      break;
    }
  }
}

void XtbEngine::updateTetherForDrag(const std::vector<int>& numbers,
                                    const std::vector<int>& bondPairs,
                                    const std::vector<double>& positionsBohr)
{
  // Read the current drag index (the drag lifecycle flags flow GUI->worker
  // through setDraggedAtom). Tether state is worker-owned, so no lock guards
  // it; the drag index is read under m_stateMutex because the GUI thread writes
  // it.
  int currentDrag = -1;
  {
    QMutexLocker locker(&m_stateMutex);
    currentDrag = m_draggedIndex;
  }
  if (currentDrag < 0) {
    // Release (or no drag): clear the tether (design D5). The bond-graph
    // projection is session-wide (design D8) and keeps enforcing the drawn
    // graph outside drags, so the floor restraints have no separate
    // lifecycle. The drag mask is already off, so evaluateOnce will not
    // apply a stale tether even before this clears (it guards on di >= 0).
    m_tetherBondIndices.clear();
    m_tetherTargetBohr.clear();
  }
  else if (m_prevDragIndex != currentDrag) {
    // Press, or the dragged atom changed between ticks: capture the tether
    // targets and floor pairs from the live bond graph (design D2/D7/D8).
    // Deterministic for identical molecule state (design D3). The !=
    // comparison (rather than a -1 -> >=0 test) catches a release+re-press
    // that the worker observed as a single index change (a busy tick can miss
    // the intermediate -1).
    computeTetherTargets(currentDrag, numbers, bondPairs, positionsBohr,
                         m_tetherBondIndices, m_tetherTargetBohr);
  }
  m_prevDragIndex = currentDrag;
}

void XtbEngine::resetPhaseForReBaseline()
{
  // A topology/position change re-baselines the optimizer: drop the
  // quasi-Newton history, clear the baseline, zero the eval counter, and clear
  // the converged/capped idle latch so the next tick evaluates from the
  // current geometry (design D2). Worker-thread only.
  m_lbfgs.resetHistory();
  m_initialEvalDone = false;
  m_evals = 0;
  m_resetHistoryRequested = false;
  {
    QMutexLocker locker(&m_stateMutex);
    m_converged = false;
    // A topology change or a re-established session clears a failed-evaluation
    // latch so the new phase evaluates normally (design D4 recovery path).
    m_evalFailed = false;
    m_evalFailureReason.clear();
  }
}

bool XtbEngine::reestablishForTopology(const std::vector<int>& numbers,
                                       const std::vector<double>& positionsBohr,
                                       const std::vector<int>& bondPairs)
{
  // Worker-side re-establishment (design D2): exactly one destroy/recreate of
  // the four xTB objects per topology change, under m_mutex (the GUI thread
  // never locks m_mutex after setup). Re-validate elements and re-load the
  // method; a failure routes to the failed-setup latch (no per-tick retries).
  QMutexLocker locker(&m_mutex);
  destroyObjects();
  m_numbers = numbers;
  m_bondPairs = bondPairs;
  m_coordsBohr = positionsBohr;
  m_grad.assign(m_coordsBohr.size(), 0.0);
  // The topology changed, so the grabbed atom may no longer exist (or its
  // index may have shifted). Drop the drag mask so the re-established session
  // starts clean; the next pointer interaction re-arms it.
  {
    QMutexLocker stateLocker(&m_stateMutex);
    m_draggedIndex = -1;
    m_interactive = false;
    m_currentAccuracy = kXtbDefaultAccuracy;
  }
  // The bond graph may have changed (design D5): clear the tether so no
  // removed bond stays tethered; the next press re-captures it. The
  // projection reads m_bondPairs (already updated above), so its drawn-graph
  // clamp follows the new graph immediately. Restraint state is
  // worker-owned, so no lock guards it; this runs on the worker thread.
  m_tetherBondIndices.clear();
  m_tetherTargetBohr.clear();
  m_prevDragIndex = -1;
  if (!validateElements()) {
    return false;
  }
  if (!createObjects()) {
    return false;
  }
  if (!loadMethod()) {
    destroyObjects();
    return false;
  }
  // Exactly one re-establishment: reset the optimizer phase for the new
  // topology (history, baseline, eval counter, converged latch), and clear the
  // setup-broken latch so a previously failed re-establishment (which left the
  // objects destroyed) no longer idles the phase. The objects now exist and
  // are valid, so the phase can evaluate again.
  {
    QMutexLocker stateLocker(&m_stateMutex);
    m_setupBroken = false;
  }
  resetPhaseForReBaseline();
  emit sessionReestablished(static_cast<int>(numbers.size()));
  return true;
}

bool XtbEngine::adoptExternalPositions(const std::vector<double>& livePositionsBohr)
{
  if (livePositionsBohr.size() != m_coordsBohr.size()) {
    return false;
  }
  // Read the drag index under the state mutex (excluded from the diff).
  int dragIndex = -1;
  {
    QMutexLocker locker(&m_stateMutex);
    dragIndex = m_draggedIndex;
  }
  // Diff against the session's working coords with an epsilon above unit
  // round-trip noise (~1e-6 bohr), excluding the dragged atom (the drag path
  // is the authority for that atom, so its motion is not an external edit).
  // A position-only change authored outside the session is adopted as the
  // starting geometry of a fresh optimization phase and is NOT reverted by
  // subsequent write-backs (spec scenario "External position edit").
  constexpr double kPositionEpsilonBohr = 1e-6;
  bool changed = false;
  for (int i = 0; i < static_cast<int>(m_coordsBohr.size()); ++i) {
    const int atomIndex = i / 3;
    if (atomIndex == dragIndex) {
      continue;
    }
    if (std::abs(livePositionsBohr[i] - m_coordsBohr[i]) > kPositionEpsilonBohr) {
      changed = true;
      break;
    }
  }
  if (changed) {
    m_coordsBohr = livePositionsBohr;
    m_grad.assign(m_coordsBohr.size(), 0.0);
    resetPhaseForReBaseline();
  }
  return changed;
}

void XtbEngine::run()
{
  // The worker thread also does Fortran I/O (evaluateOnce / SCF), so the same
  // console-less-process invalid-parameter fast-fail can fire on ticks. The
  // process-wide no-op handler installed in XtbApi::isAvailable() covers this
  // thread too.
  const int n = static_cast<int>(m_coordsBohr.size());
  m_grad.assign(n, 0.0);
  m_energy = 0.0;
  m_evals = 0;
  m_gnorm = 0.0;
  m_initialEvalDone = false;
  m_quiesced = false;

  while (!m_stop.loadRelaxed()) {
    // Wait for a tick (or stop). The GUI's requestTick()/stop() wake this.
    {
      QMutexLocker locker(&m_stateMutex);
      while (!m_tickPending && !m_stop.loadRelaxed()) {
        m_tickCond.wait(&m_stateMutex);
      }
      if (m_stop.loadRelaxed()) {
        break;
      }
      m_tickPending = false;
      // Test-only failure-injection seam (design D9d): if this tick's index is
      // in the test pattern, treat the tick as a failed evaluation without
      // touching the xTB library — set the non-finite path (NaN energy +
      // diagnostic) and skip the real evaluation so the deferred-latch / D4
      // containment machinery is driven deterministically. Production behavior
      // is untouched when the pattern is empty.
      m_testInjectFail = false;
      {
        const int idx = m_testTickCounter++;
        for (size_t i = 0; i < m_testFailingTicks.size(); ++i) {
          if (m_testFailingTicks[i] == idx) {
            m_testInjectFail = true;
            break;
          }
        }
      }
    }

    // Apply any deferred L-BFGS history reset (drag start/end, interactive
    // toggle) on the worker thread, where the history is actually used.
    {
      QMutexLocker locker(&m_stateMutex);
      if (m_resetHistoryRequested) {
        m_resetHistoryRequested = false;
        m_lbfgs.resetHistory();
      }
    }

    // Design D2: re-baseline from the live molecule when a source is set. The
    // worker reads the live molecule each tick (bounded-retry read lock) and
    // reacts to a topology change (worker-side re-establishment) or an
    // external position change (fresh phase). Placed BEFORE the converged
    // latch so an external move after convergence resumes the session.
    bool latchedDragMoved = false;
    bool pointerMovedThisPass = false;
    if (m_molecule) {
      std::vector<int> liveNumbers;
      std::vector<double> livePositionsBohr;
      std::vector<int> liveBondPairs;
      // A failed read (lock contention/quiesce) leaves no valid snapshot for
      // this tick: clear it so the write-back cannot use a stale one to skip
      // atoms it should actually update.
      m_readSnapshotBohr.clear();
      if (!readLiveMolecule(liveNumbers, livePositionsBohr, liveBondPairs)) {
        // No live source was read this tick (lock not acquired within the
        // retry budget, or a stop was requested). Proceeding to evaluate and
        // then write back would run an UNPROTECTED write (no snapshot to skip
        // externally-changed atoms), so skip the whole tick: do not evaluate,
        // do not publish, do not write back. This also shortens quiesce.
        continue;
      }
      {
        // Record the read snapshot so this tick's write-back can distinguish
        // the optimizer's own advance (no external edit: atom still matches the
        // snapshot) from a genuine external edit made between the read and the
        // write-back (atom changed: skip it, preserve the edit).
        m_readSnapshotBohr = livePositionsBohr;
        // Design D2/D5: re-establish on any topology change — an atom
        // added/removed (numbers differ) OR a bond added/removed (bond graph
        // differs, even though the atom numbers are unchanged). The bond-graph
        // comparison is what makes a bond removed mid-drag clear the tether
        // (no stale tether on a removed bond).
        if (liveNumbers != m_numbers || liveBondPairs != m_bondPairs) {
          if (!reestablishForTopology(liveNumbers, livePositionsBohr,
                                      liveBondPairs)) {
            // Failed-setup latch: deterministic, no per-tick retries. The xTB
            // objects were destroyed and could not be recreated, so the phase
            // is BROKEN, not merely idle. Set m_setupBroken (NOT m_converged):
            // m_converged is cleared by a drag or position edit
            // (resumeOptimizationLocked/resetPhaseForReBaseline), which would
            // otherwise make the next evaluateOnce() call into the destroyed
            // (null) xTB objects and crash. m_setupBroken is cleared only by a
            // successful setup()/setupFromMolecule() or reestablishForTopology().
            QMutexLocker locker(&m_stateMutex);
            m_setupBroken = true;
            m_coordsValid = false;
            continue;
          }
        }
        else {
          adoptExternalPositions(livePositionsBohr);
          // Design D3: the dragged atom's position is the pointer position,
          // which lives in the live molecule. Copy it into the working coords
          // so the optimizer and the published snapshot track the pointer. A
          // pure drag moves only this atom, so adoptExternalPositions' (non-
          // dragged) change detection would not carry it; this copy is the
          // drag path, not an external edit, so it does not reset the phase.
          {
            QMutexLocker locker(&m_stateMutex);
            const int di = m_draggedIndex;
            if (di >= 0 && di * 3 + 2 < static_cast<int>(m_coordsBohr.size()) &&
                di * 3 + 2 < static_cast<int>(livePositionsBohr.size())) {
              // Design D2: pointer-authored movement of the dragged atom
              // refreshes the phase's evaluation budget. A long continuous drag
              // would otherwise exhaust the per-phase cap mid-drag; the pull is
              // the phase's purpose, so a movement resets the counter (bounded
              // by user movement, not per tick). Compare against the working
              // coords (which still hold the previous drag position) before
              // overwriting.
              constexpr double kDragMoveEpsilonBohr = 1e-6;
              bool pointerMoved = false;
              for (int k = 0; k < 3; ++k) {
                if (std::abs(m_coordsBohr[di * 3 + k] -
                             livePositionsBohr[di * 3 + k]) >
                    kDragMoveEpsilonBohr) {
                  pointerMoved = true;
                  break;
                }
              }
              if (pointerMoved) {
                m_evals = 0;
                pointerMovedThisPass = true;
                // Design D9 (deferred-latch): a pointer-authored move is the
                // user's recovery gesture; reset the consecutive-failure
                // counter so a moving drag never latches from the pull alone.
                m_dragFailCount = 0;
                // Design D9 recovery: a pointer-authored move while the phase
                // is latched-with-failure arms one bounded recovery attempt
                // (recreated xTB objects + elevated budget at the failure
                // gate below) — the user retreating the pull is the natural
                // recovery gesture, and the fresh geometry deserves a fresh
                // SCF guess. (A failed evaluation can leave the calculator's
                // internal state poisoned: every re-evaluation on it then
                // fails regardless of geometry — "restarts failing" that no
                // retreat could fix.)
                if (m_evalFailed) {
                  latchedDragMoved = true;
                }
              }
              for (int k = 0; k < 3; ++k) {
                m_coordsBohr[di * 3 + k] = livePositionsBohr[di * 3 + k];
                // Mask the dragged atom's working gradient (worker-only state).
                // Without this, the dragged atom's gradient would jump from its
                // pre-grab value to zero across the step, storing a non-zero
                // curvature pair for that atom in the L-BFGS history and
                // continuously nudging the pinned atom off the pointer.
                if (static_cast<size_t>(di * 3 + k) < m_grad.size()) {
                  m_grad[di * 3 + k] = 0.0;
                }
              }
            }
          }
        }
      }
      // Design D2/D3: tether lifecycle at the drag transition. The tether is
      // captured on press (from the bond graph just read) and cleared on
      // release; a topology re-establishment (above) already cleared/rebuilds
      // it via reestablishForTopology. No-op when no drag transitioned.
      updateTetherForDrag(liveNumbers, liveBondPairs, livePositionsBohr);
    }

    // A converged (or capped) session idles: ticks are consumed without
    // evaluating, so CPU use stays at idle while the session stays open.
    // A user drag clears the latch (resumeOptimizationLocked), which both
    // re-arms the optimizer and makes the next tick re-baseline from the
    // dragged geometry. A failed-evaluation phase (D4) idles the same way. A
    // setup-broken phase (D2/D4) also idles — and unlike the converged latch,
    // m_setupBroken is NOT cleared by a drag/position edit, because the xTB
    // objects are destroyed and the phase cannot evaluate without crashing.
    {
      QMutexLocker locker(&m_stateMutex);
      // Design D1: the converged/capped idle is deferred while a drag is in
      // flight (a drag may be in flight across a latch set just before the
      // press cleared it). The failed-evaluation and setup-broken latches
      // still skip — a failed or broken phase is contained regardless of
      // drag — unless a pointer-authored move during the latched drag armed
      // the D9 recovery below.
      if ((m_converged && m_draggedIndex < 0) || m_setupBroken)
        continue;
      if (m_evalFailed && !latchedDragMoved)
        continue;
    }

    // Design D9 recovery: the pointer moved during a latched drag. Recreate
    // the four xTB objects (the failed evaluation can leave the calculator's
    // internal state poisoned — the C API has no error-clear call, so every
    // re-evaluation on them fails regardless of geometry) and reset the
    // phase, then fall through: this pass re-evaluates the retreated
    // geometry at the elevated budget. One bounded attempt per pointer move;
    // a recovery that fails latches again until the next move.
    if (latchedDragMoved) {
      {
        QMutexLocker locker(&m_stateMutex);
        if (!m_evalFailed) {
          latchedDragMoved = false;
        }
      }
      if (latchedDragMoved) {
        qWarning() << "AutoOpt xTB: latched-drag recovery: recreating xTB "
                      "objects for the retreated geometry";
        {
          QMutexLocker objLocker(&m_mutex);
          destroyObjects();
          if (!createObjects() || !loadMethod()) {
            QMutexLocker stateLocker(&m_stateMutex);
            m_setupBroken = true;
            m_coordsValid = false;
            qWarning() << "AutoOpt xTB: latched-drag recovery failed to "
                          "recreate the xTB objects; session is setup-broken";
            continue;
          }
        }
        resetPhaseForReBaseline();
        QMutexLocker relock(&m_stateMutex);
        m_releaseElevatedIter = true;
        latchedDragMoved = false;
      }
    }

    if (m_testInjectFail) {
      // Injected failure tick (design D9d): skip the real evaluation and route
      // NaN into the deferred-latch / D4 containment machinery below. Mark the
      // baseline done so the baseline block is skipped; the L-BFGS block below
      // is likewise skipped (m_testInjectFail), leaving m_energy NaN for the
      // deferred-latch to count.
      QMutexLocker locker(&m_stateMutex);
      m_energy = std::numeric_limits<double>::quiet_NaN();
      m_evalFailureReason = QStringLiteral("test-injected SCF failure");
      m_coordsValid = false;
      m_initialEvalDone = true;
    }
    else if (!m_initialEvalDone) {
      // First singlepoint: establish the energy/gradient baseline.
      // Design D5/D8: the projection is session-wide (every evaluated
      // geometry, drag or not), so the baseline is also projected onto the
      // drawn bond graph before the backend sees it. Without this, a freshly
      // grabbed atom at a dissociated pointer position reaches the SCF
      // un-projected (O-H1 at 6.0 A in the water repro) and fails to converge
      // every tick — the deferred-latch surfaces the failure and the pull
      // appears to freeze the optimization.
      projectBondGraph(m_coordsBohr);
      m_energy = evaluateOnce(m_coordsBohr, m_grad);
      // A failed baseline latches the phase idle-with-failure; the tick
      // publishes nothing (evaluateOnce clears m_coordsValid) and the next
      // loop iteration is skipped by the failure latch above.
      if (std::isnan(m_energy)) {
        continue;
      }
      m_evals = 1;
      m_gnorm = m_lbfgs.gradNorm(m_grad);
      m_initialEvalDone = true;
      {
        QMutexLocker locker(&m_stateMutex);
        m_lastEvalCount = m_evals;
      }
      emit progress(m_evals, m_maxEvaluations, m_energy);
      publishCoords(m_coordsBohr);
      // Keep the live molecule in sync with the working geometry (design D2) so
      // the next tick's re-baseline comparison (molecule vs m_coordsBohr) sees
      // no spurious change while the optimizer advances; only a genuine external
      // edit then resets the phase. Guarded inside by m_molecule.
      writeBackToMolecule();

      if (rmsGradientNorm() < m_threshold || shouldStop(m_evals)) {
        // Converged (or immediately capped) on the baseline: latch the idle
        // state but keep the session (worker + xTB objects) alive so a later
        // drag resumes optimization instead of requiring a restart. Design D1:
        // while a drag is in flight the masked gradient (dragged atom's
        // tension excluded) can reach threshold under a held pull; deferring
        // the latch keeps the free atoms relaxing until release.
        bool latch = false;
        {
          QMutexLocker locker(&m_stateMutex);
          if (m_draggedIndex < 0 && !m_converged) {
            m_converged = true;
            latch = true;
          }
        }
        if (latch)
          emit converged(m_energy);
      }
      continue;
    }

    // L-BFGS iterations (each line search calls evaluateOnce per trial).
    // Design D6 responsiveness (task 5.7 tuning): during an active drag the
    // tick runs up to kDragStepsPerTick iterations instead of one, so the
    // constrained structure keeps up with a fast pointer between updates —
    // a pinned atom that teleports a full angstrom per tick otherwise leaves
    // the neighbors strained at the projection clamps, which is exactly the
    // geometry class that trips the interactive SCF. Each iteration still
    // runs at the interactive budget; the counter/cap semantics are
    // unchanged (shouldStop and the eval cap bound the loop).
    // Note: the optimizer state (m_coordsBohr/m_energy/m_grad/m_evals/m_gnorm
    // and m_initialEvalDone below) is worker-owned; the m_stateMutex acquire
    // in the converged-skip block above is the happens-before edge that makes
    // resumeOptimizationLocked()'s GUI-thread writes (m_initialEvalDone,
    // m_evals) visible here — keep that block ahead of these reads.
    int dragSteps = 0;
    bool cont = true;
    if (!m_testInjectFail) {
      do {
        cont = m_lbfgs.step(
          m_coordsBohr, m_energy, m_grad, m_evals, m_gnorm,
          [this](const std::vector<double>& c, std::vector<double>& g) -> double {
            return evaluateOnce(c, g);
          },
          [this](int evals) -> bool { return shouldStop(evals); });
        if (m_evalFailed || std::isnan(m_energy)) {
          break;
        }
        ++dragSteps;
        if (m_draggedIndex < 0 || !pointerMovedThisPass) {
          // Multi-step is the fast-flick catch-up budget: it applies only to
          // the pass that observed a pointer move. A held-still pull relaxes
          // one calm iteration per tick, so the evaluation cap and the
          // constrained-geometry cost stay at the held-pull semantics.
          break;
        }
      } while (cont && dragSteps < kDragStepsPerTick);
    }

    // Design D4/D9: a latched failure (non-drag, or a drag held past the
    // deferred-latch threshold) idles the phase; publish nothing and skip the
    // rest of this tick (the next loop iteration is skipped by the failure
    // latch). A deferred drag failure (evaluateOnce returned NaN without
    // latching) is surfaced, counted, and latched only when the drag has been
    // held fixed on a genuinely unconvergeable geometry for the threshold
    // number of consecutive ticks — a moving or held-but-recoverable drag
    // never latches from the pull alone (spec scenario). A successful tick
    // resets the counter.
    {
      QString surfaceReason;
      bool skipTick = false;
      {
        QMutexLocker locker(&m_stateMutex);
        if (m_evalFailed) {
          skipTick = true;
        }
        else if (std::isnan(m_energy)) {
          ++m_dragFailCount;
          if (m_dragFailCount < kXtbDragFailLatchThreshold) {
            // Deferred: surface the raw diagnostic, keep evaluating.
            surfaceReason = m_evalFailureReason;
          }
          else {
            // Held long enough: engage the D4 latch with the recovery path.
            m_evalFailed = true;
            m_coordsValid = false;
            m_evalFailureReason += QStringLiteral(
              "; restart the session (Stop/Start) or edit the molecule to "
              "re-establish it");
            surfaceReason = m_evalFailureReason;
          }
          skipTick = true;
        }
        else {
          m_dragFailCount = 0;
        }
      }
      if (skipTick) {
        if (!surfaceReason.isEmpty()) {
          emit evaluationFailed(surfaceReason);
        }
        continue;
      }
    }

    bool reportConverged = false;
    {
      QMutexLocker locker(&m_stateMutex);
      m_lastEvalCount = m_evals;
      // Design D1: do not latch the converged idle while a drag is in flight;
      // the masked gradient reaching threshold under a held pull must not
      // freeze relaxation. The latch engages on release.
      if (m_draggedIndex < 0 && rmsGradientNorm() < m_threshold && !m_converged) {
        m_converged = true;
        reportConverged = true;
      }
    }
    emit progress(m_evals, m_maxEvaluations, m_energy);
    publishCoords(m_coordsBohr);
    // Keep the live molecule in sync with the optimized working geometry so the
    // next tick's re-baseline does not mistake the optimizer's own advance for
    // an external edit (design D2). Guarded inside by m_molecule.
    writeBackToMolecule();

    if (!cont && rmsGradientNorm() >= m_threshold && !m_stop.loadRelaxed()) {
      // step() exhausted a stop condition before reaching the threshold:
      // either the evaluation cap or LBfgs' internal step cap. The spec says
      // a capped session reports its last energy and stops evaluating (cap
      // != error), so the converged signal carries "session idle, last
      // energy shown" for both. Latch the idle state; a later drag resumes
      // with a fresh phase. Design D1: the cap idle is deferred while a drag
      // is in flight so a long pull never freezes partway through.
      QMutexLocker locker(&m_stateMutex);
      if (m_draggedIndex < 0 && !m_converged) {
        m_converged = true;
        reportConverged = true;
      }
    }
    if (reportConverged)
      emit converged(m_energy);
    // Keep looping: idle ticks are skipped above; stop() breaks the loop.
  }

  // The worker owns deletion: only here, after the loop observed stop or
  // finished, are the xTB objects destroyed. The GUI thread never calls
  // xtb_del*; it only calls stop()/wait().
  deleteObjectsOnWorker();
}

} // namespace Avogadro
