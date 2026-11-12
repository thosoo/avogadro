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

#include <QtGlobal>

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

constexpr int kErrorBufferSize = 1024;

} // namespace

XtbEngine::XtbEngine(QObject* parent)
  : QThread(parent)
  , m_env(nullptr)
  , m_mol(nullptr)
  , m_calc(nullptr)
  , m_results(nullptr)
  , m_objectsCreated(false)
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
  , m_lastEnergy(0.0)
  , m_lastEvalCount(0)
  , m_coordsValid(false)
  , m_quiesced(false)
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
  // A fresh session must not inherit the stop flag from a previous one; reset
  // it here so a re-setup after quiescing starts cleanly (not immediately
  // exiting run() because the prior stop() left the flag set).
  m_stop.storeRelaxed(0);
  m_lbfgs.resetHistory();

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
  if (m_draggedIndex != index) {
    m_draggedIndex = index;
    m_dragPositionBohr.clear();
    // A change to the masked set invalidates the quasi-Newton model (D6); the
    // worker resets the history at the start of the next tick (resetting here
    // would race the worker's use of the L-BFGS history).
    m_resetHistoryRequested = true;
    // Adapt accuracy to the interaction state (D7): dragging uses reduced
    // accuracy so the pulled structure relaxes responsively.
    if (index >= 0) {
      m_interactive = true;
      m_currentAccuracy = kXtbInteractiveAccuracy;
    }
    else {
      m_interactive = false;
      m_currentAccuracy = kXtbDefaultAccuracy;
    }
  }
}

void XtbEngine::notifyDragPosition(int index,
                                   const std::vector<double>& positionBohr)
{
  QMutexLocker locker(&m_stateMutex);
  if (m_draggedIndex != index) {
    m_draggedIndex = index;
    m_resetHistoryRequested = true;
    if (index >= 0) {
      m_interactive = true;
      m_currentAccuracy = kXtbInteractiveAccuracy;
    }
    else {
      m_interactive = false;
      m_currentAccuracy = kXtbDefaultAccuracy;
    }
  }
  m_dragPositionBohr = positionBohr;
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
  int di = -1;
  std::vector<double> dragPos;
  double accuracy = kXtbDefaultAccuracy;
  int maxIter = kXtbDefaultMaxIter;
  {
    QMutexLocker locker(&m_stateMutex);
    di = m_draggedIndex;
    if (di >= 0) {
      dragPos = m_dragPositionBohr;
    }
    accuracy = m_currentAccuracy;
    maxIter = m_interactive ? kXtbInteractiveMaxIter : kXtbDefaultMaxIter;
  }

  // Pin the dragged atom to the pointer-authored position before the SCF.
  std::vector<double> pinned = coords;
  if (di >= 0 && static_cast<size_t>(di * 3 + 2) < pinned.size()) {
    for (int k = 0; k < 3; ++k) {
      pinned[di * 3 + k] = dragPos[k];
    }
  }

  double energy = 0.0;
  {
    QMutexLocker locker(&m_mutex);
    XtbApi::xtb_setAccuracy()(m_env, m_calc, accuracy);
    XtbApi::xtb_setMaxIter()(m_env, m_calc, maxIter);
    XtbApi::xtb_updateMolecule()(m_env, m_mol, pinned.data(), nullptr);
    XtbApi::xtb_singlepoint()(m_env, m_mol, m_calc, m_results);
    XtbApi::xtb_getEnergy()(m_env, m_results, &energy);
    XtbApi::xtb_getGradient()(m_env, m_results, grad.data());
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

void XtbEngine::applyDragToWorkingCoords()
{
  QMutexLocker locker(&m_stateMutex);
  if (m_draggedIndex < 0) {
    return;
  }
  const int idx = m_draggedIndex;
  const int base = idx * 3;
  if (base + 3 > static_cast<int>(m_coordsBohr.size()) ||
      static_cast<int>(m_dragPositionBohr.size()) < 3) {
    return;
  }
  // The optimizer's working coordinate for the dragged atom always equals the
  // pointer position, so a release resumes covering it from where the drag left
  // it (D6) and the published coordinate is never regressed (write-back). Zero
  // its working gradient too: the direction is computed from m_grad at the
  // start of the step, and a stale (pre-drag) gradient would otherwise move the
  // pinned atom off the pointer position on the first tick after the drag.
  for (int k = 0; k < 3; ++k) {
    m_coordsBohr[base + k] = m_dragPositionBohr[k];
    if (base + k < static_cast<int>(m_grad.size())) {
      m_grad[base + k] = 0.0;
    }
  }
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

void XtbEngine::deleteObjectsOnWorker()
{
  // Guard so the four xTB objects are deleted exactly once, even if run() is
  // somehow entered more than once for the same lifetime.
  if (m_quiesced) {
    return;
  }
  m_quiesced = true;
  QMutexLocker locker(&m_mutex);
  destroyObjects();
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

    // Keep the optimizer's working coordinates in sync with the drag: the
    // dragged atom's coordinate equals the pointer position, so a release
    // resumes covering it from where the drag left it.
    applyDragToWorkingCoords();

    // Apply any deferred L-BFGS history reset (drag start/end, interactive
    // toggle) on the worker thread, where the history is actually used.
    {
      QMutexLocker locker(&m_stateMutex);
      if (m_resetHistoryRequested) {
        m_resetHistoryRequested = false;
        m_lbfgs.resetHistory();
      }
    }

    if (!m_initialEvalDone) {
      // First singlepoint: establish the energy/gradient baseline.
      m_energy = evaluateOnce(m_coordsBohr, m_grad);
      m_evals = 1;
      m_gnorm = m_lbfgs.gradNorm(m_grad);
      m_initialEvalDone = true;
      {
        QMutexLocker locker(&m_stateMutex);
        m_lastEvalCount = m_evals;
      }
      emit progress(m_evals, m_maxEvaluations, m_energy);
      publishCoords(m_coordsBohr);

      if (m_gnorm < m_threshold || shouldStop(m_evals)) {
        emit converged(m_energy);
        break;
      }
      continue;
    }

    // One L-BFGS iteration (its line search calls evaluateOnce per trial).
    bool cont = m_lbfgs.step(
      m_coordsBohr, m_energy, m_grad, m_evals, m_gnorm,
      [this](const std::vector<double>& c, std::vector<double>& g) -> double {
        return evaluateOnce(c, g);
      },
      [this](int evals) -> bool { return shouldStop(evals); });

    {
      QMutexLocker locker(&m_stateMutex);
      m_lastEvalCount = m_evals;
      if (m_gnorm < m_threshold) {
        m_converged = true;
      }
    }
    emit progress(m_evals, m_maxEvaluations, m_energy);
    publishCoords(m_coordsBohr);

    if (m_converged) {
      emit converged(m_energy);
      break;
    }
    if (!cont) {
      // step() stopped on the eval cap or a caller stop.
      if (m_gnorm < m_threshold) {
        emit converged(m_energy);
      }
      break;
    }
  }

  // The worker owns deletion: only here, after the loop observed stop or
  // finished, are the xTB objects destroyed. The GUI thread never calls
  // xtb_del*; it only calls stop()/wait().
  deleteObjectsOnWorker();
}

} // namespace Avogadro
