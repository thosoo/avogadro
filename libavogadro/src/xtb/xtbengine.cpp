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

#include <QtGlobal>
#include <thread>
#include <QReadWriteLock>
#include <cmath>
#include <limits>

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
constexpr double kXtbInteractiveAccuracy = 0.2;
constexpr int kXtbInteractiveMaxIter = 30;
constexpr double kXtbDefaultAccuracy = 1.0;
constexpr int kXtbDefaultMaxIter = 100;
// Design D3: the fresh phase's first evaluation after a drag release is
// attempted once at this elevated SCF iteration budget (2x the steady-state
// budget) so a strained-but-recoverable released geometry converges into the
// session instead of latching an evaluation failure. Bounded, deterministic.
constexpr int kXtbReleaseMaxIter = 2 * kXtbDefaultMaxIter;

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
                      int level, int threadsSetting)
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
  // A fresh setup must not carry a stale drag index or release-elevation flag
  // from a previous session.
  m_draggedIndex = -1;
  m_releaseElevatedIter = false;

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
  {
    QReadLocker locker(molecule->lock());
    const int n = molecule->numAtoms();
    numbers.reserve(static_cast<size_t>(n));
    positionsAngstrom.reserve(static_cast<size_t>(n) * 3);
    foreach (const Atom* atom, molecule->atoms()) {
      numbers.push_back(atom->atomicNumber());
      const Eigen::Vector3d& p = *atom->pos();
      positionsAngstrom.push_back(p.x());
      positionsAngstrom.push_back(p.y());
      positionsAngstrom.push_back(p.z());
    }
  }

  if (!setup(numbers, positionsAngstrom, method, level, threadsSetting)) {
    return false;
  }
  m_molecule = molecule;
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
    resetPhaseLocked();
  }
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
  // OMP_WAIT_POLICY=PASSIVE always when unset.
  if (!qEnvironmentVariableIsSet("OMP_WAIT_POLICY")) {
    qputenv("OMP_WAIT_POLICY", "PASSIVE");
  }
  // An explicit user thread count always takes effect (the UI choice wins).
  // An auto request (threadsSetting == 0) is applied only when the variable is
  // unset, so a user-set environment is respected (D7).
  if (threadsSetting > 0) {
    qputenv("OMP_NUM_THREADS", QByteArray::number(threadsSetting));
  }
  else if (!qEnvironmentVariableIsSet("OMP_NUM_THREADS")) {
    int threads = qMax(1, qMin(8, QThread::idealThreadCount() - 1));
    qputenv("OMP_NUM_THREADS", QByteArray::number(threads));
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
  switch (m_method) {
    case 0:
      XtbApi::xtb_loadGFN1xTB()(m_env, m_mol, m_calc, nullptr);
      break;
    case 1:
      XtbApi::xtb_loadGFN2xTB()(m_env, m_mol, m_calc, nullptr);
      break;
    case 2:
      XtbApi::xtb_loadGFNFF()(m_env, m_mol, m_calc, nullptr);
      break;
    default:
      break;
  }

  if (XtbApi::xtb_checkEnvironment()(m_env) != 0) {
    int bufsize = kErrorBufferSize;
    char buffer[kErrorBufferSize];
    XtbApi::xtb_getError()(m_env, buffer, &bufsize);
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
      int bufsize = kErrorBufferSize;
      char buffer[kErrorBufferSize];
      XtbApi::xtb_getError()(m_env, buffer, &bufsize);
      QString reason = QString::fromLatin1(buffer);
      if (reason.isEmpty()) {
        reason = QStringLiteral("xTB singlepoint failed");
      }
      latchEvaluationFailure(reason);
      return std::numeric_limits<double>::quiet_NaN();
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
      latchEvaluationFailure(
        QStringLiteral("xTB evaluation returned non-finite results"));
      return std::numeric_limits<double>::quiet_NaN();
    }
  }

  // Zero the dragged atom's gradient so the optimizer treats it as fixed.
  if (di >= 0 && static_cast<size_t>(di * 3 + 2) < grad.size()) {
    for (int k = 0; k < 3; ++k) {
      grad[di * 3 + k] = 0.0;
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
  {
    QMutexLocker locker(&m_stateMutex);
    m_evalFailed = true;
    m_evalFailureReason = withRecovery;
    m_coordsValid = false;
  }
  emit evaluationFailed(withRecovery);
}

bool XtbEngine::readLiveMolecule(std::vector<int>& numbers,
                                 std::vector<double>& positionsBohr)
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
  return true;
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
                                       const std::vector<double>& positionsBohr)
{
  // Worker-side re-establishment (design D2): exactly one destroy/recreate of
  // the four xTB objects per topology change, under m_mutex (the GUI thread
  // never locks m_mutex after setup). Re-validate elements and re-load the
  // method; a failure routes to the failed-setup latch (no per-tick retries).
  QMutexLocker locker(&m_mutex);
  destroyObjects();
  m_numbers = numbers;
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
    if (m_molecule) {
      std::vector<int> liveNumbers;
      std::vector<double> livePositionsBohr;
      // A failed read (lock contention/quiesce) leaves no valid snapshot for
      // this tick: clear it so the write-back cannot use a stale one to skip
      // atoms it should actually update.
      m_readSnapshotBohr.clear();
      if (!readLiveMolecule(liveNumbers, livePositionsBohr)) {
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
        if (liveNumbers != m_numbers) {
          if (!reestablishForTopology(liveNumbers, livePositionsBohr)) {
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
      // press cleared it). The failed-evaluation and setup-broken latches still
      // skip — a failed or broken phase is contained regardless of drag.
      if ((m_converged && m_draggedIndex < 0) || m_evalFailed || m_setupBroken)
        continue;
    }

    if (!m_initialEvalDone) {
      // First singlepoint: establish the energy/gradient baseline.
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

      if (m_gnorm < m_threshold || shouldStop(m_evals)) {
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

    // One L-BFGS iteration (its line search calls evaluateOnce per trial).
    // Note: the optimizer state (m_coordsBohr/m_energy/m_grad/m_evals/m_gnorm
    // and m_initialEvalDone below) is worker-owned; the m_stateMutex acquire
    // in the converged-skip block above is the happens-before edge that makes
    // resumeOptimizationLocked()'s GUI-thread writes (m_initialEvalDone,
    // m_evals) visible here — keep that block ahead of these reads.
    bool cont = m_lbfgs.step(
      m_coordsBohr, m_energy, m_grad, m_evals, m_gnorm,
      [this](const std::vector<double>& c, std::vector<double>& g) -> double {
        return evaluateOnce(c, g);
      },
      [this](int evals) -> bool { return shouldStop(evals); });

    // Design D4: if any trial evaluation latched a failure, the phase is
    // idle-with-failure; publish nothing and skip the rest of this tick (the
    // next loop iteration is skipped by the failure latch).
    {
      QMutexLocker locker(&m_stateMutex);
      if (m_evalFailed) {
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
      if (m_draggedIndex < 0 && m_gnorm < m_threshold && !m_converged) {
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

    if (!cont && m_gnorm >= m_threshold && !m_stop.loadRelaxed()) {
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
