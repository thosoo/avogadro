/**********************************************************************
  XtbEngine - Headless xTB optimization session engine

  A QThread-based worker that owns the xTB library objects (environment,
  molecule, calculator, results) for a single optimization session and drives
  a client-side L-BFGS optimizer over library singlepoints. See xtbengine.cpp.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 ***********************************************************************/

#ifndef XTBENGINE_H
#define XTBENGINE_H

#include "xtbapi.h"
#include "lbfgs.h"

#include <avogadro/global.h>

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QAtomicInt>
#include <QString>
#include <vector>

namespace Avogadro {

class Molecule;

/**
 * @class XtbEngine
 * @brief Headless xTB optimization session engine.
 *
 * setup() creates the four xTB objects from an atomic-number list and a
 * coordinate set (in angstrom) and configures the method/level. start() then
 * runs run() on the worker thread, which drives one L-BFGS iteration per tick
 * (paced by requestTick()) until convergence, the evaluation cap, or stop().
 * The worker thread owns the xTB objects and deletes them at exit.
 */
class A_EXPORT XtbEngine : public QThread
{
  Q_OBJECT

public:
  XtbEngine(QObject* parent = nullptr);
  ~XtbEngine();

  /**
   * Configure the session. Returns false (and does not start the thread) when
   * the xTB library is unavailable or setup fails. On a setup failure the
   * setupFailed signal is emitted exactly once and the session is aborted.
   *
   * @param atomicNumbers per-atom atomic numbers.
   * @param positionsAngstrom per-atom positions (3*N doubles, angstrom).
   * @param method 0=GFN1-xTB, 1=GFN2-xTB, 2=GFN-FF.
   * @param level 0..7 (crude..extreme).
   * @param threadsSetting 0 = auto, otherwise the OMP thread count.
   */
  bool setup(const std::vector<int>& atomicNumbers,
             const std::vector<double>& positionsAngstrom, int method, int level,
             int threadsSetting);

  /**
   * Configure the session from a live molecule (design D2). Equivalent to
   * setup() but additionally records @p molecule as the optional source the
   * worker re-reads each tick for live-molecule re-baselining. The headless
   * vector setup above stays for tests and skips re-baselining.
   *
   * The molecule pointer is cleared on quiesce (worker exit), so the engine
   * never dereferences a destroyed molecule. Returns false (and does not start
   * the thread) when the xTB library is unavailable or setup fails.
   *
   * @param molecule the live source molecule (not owned).
   * @param method 0=GFN1-xTB, 1=GFN2-xTB, 2=GFN-FF.
   * @param level 0..7 (crude..extreme).
   * @param threadsSetting 0 = auto, otherwise the OMP thread count.
   */
  bool setupFromMolecule(Molecule* molecule, int method, int level,
                         int threadsSetting);

  /// Request that the engine stop (atomic flag + wake). The worker finishes
  /// any in-flight singlepoint before observing the stop.
  void stop();

  /// Allow one more tick's worth of work (pacing driven by the GUI tool).
  void requestTick();

  /// Set the atom being dragged (-1 = none). Resets the L-BFGS history. The
  /// dragged atom's position is the pointer position, which the worker reads
  /// from the live molecule on its per-tick re-baseline (design D3).
  void setDraggedAtom(int index);

  /// Toggle interactive (drag) accuracy adaptation.
  void setInteractive(bool interactive);

  /// The currently applied xTB accuracy (0.2 interactive, 1.0 full).
  double currentAccuracy() const;

  /// Copy the latest published coordinates (bohr) into @p out. Returns false
  /// when nothing new is available; consumes the "new" flag on success.
  bool takeLatestCoords(std::vector<double>& out);

Q_SIGNALS:
  void progress(int evaluation, int maxEvaluations, double energyHartree);
  void coordsReady(const std::vector<double>& coordsBohr);
  void converged(double finalEnergyHartree);
  void setupFailed(const QString& reason);
  // Emitted on the worker thread exactly once per failed singlepoint (D4): a
  // post-singlepoint environment error, or a non-finite energy/gradient/
  // coordinate. A failed tick publishes nothing and latches the phase
  // idle-with-failure until Stop/Start or a topology re-establishment.
  void evaluationFailed(const QString& reason);
  // Emitted on the worker thread exactly once per live-molecule topology
  // change, after the xTB objects were destroyed and recreated (design D2).
  // Tests use it to assert re-establishment happens exactly once, and the HUD
  // could use it to reset per-session state.
  void sessionReestablished(int atomCount);

protected:
  void run() override;

private:
  bool createObjects();
  void destroyObjects();
  bool loadMethod();
  bool validateElements();
  void applyOmpEnv(int threadsSetting);
  double evaluateOnce(const std::vector<double>& coords, std::vector<double>& grad);
  // Latches the phase idle-with-failure (D4): sets the failure flag, clears the
  // published-coordinate validity, and emits evaluationFailed(reason). Worker
  // thread only.
  void latchEvaluationFailure(const QString& reason);
  // Returns true when the phase is latched idle-with-failure (D4). Caller
  // must hold m_stateMutex.
  bool isEvaluationFailedLocked() const;
  // Reads the live molecule (when a source is set) under a bounded-retry read
  // lock, copying the current topology fingerprint (atomic numbers) and the
  // per-atom positions (A->bohr). Returns true when the read completed, false
  // when no source is set, the lock was not acquired within the retry budget,
  // or a stop was requested. Never dereferences a null/destroyed molecule.
  bool readLiveMolecule(std::vector<int>& numbers,
                        std::vector<double>& positionsBohr);
  // Resets the quasi-Newton phase (history, eval counter, baseline, converged
  // latch) so the next tick re-baselines from the current working geometry.
  // Worker-thread only; caller must not hold m_stateMutex.
  void resetPhaseForReBaseline();
  // Worker-side re-establishment on a topology change (design D2): destroy and
  // recreate the four xTB objects under m_mutex, adopt the new numbers/positions,
  // re-validate elements, and reset the optimizer phase. Failure routes to the
  // failed-setup latch (deterministic, no per-tick retries). Returns true on
  // success.
  bool reestablishForTopology(const std::vector<int>& numbers,
                              const std::vector<double>& positionsBohr);
  // Adopts externally-authored positions when they differ from the working
  // geometry beyond epsilon (excluding the dragged atom), starting a fresh
  // optimization phase. Returns true when adopted.
  bool adoptExternalPositions(const std::vector<double>& livePositionsBohr);
  // Clears the converged/capped idle latch and re-arms the optimizer so a user
  // drag after convergence (or after the evaluation cap) resumes evaluation
  // from the dragged geometry. Caller must hold m_stateMutex.
  void resumeOptimizationLocked();
  bool shouldStop(int evals) const;
  void publishCoords(const std::vector<double>& coordsBohr);
  // Writes the optimizer's working coordinates (bohr->A) back to the live
  // molecule under a bounded-retry write lock (design D2). Keeps the molecule
  // in sync with m_coordsBohr so the per-tick re-baseline comparison
  // (molecule vs working geometry) is correct: without this the static molecule
  // would appear to be an external change every tick the optimizer advances,
  // falsely resetting the phase and preventing convergence. Worker-thread only;
  // no-op when no source is set. Never dereferences a null/destroyed molecule.
  void writeBackToMolecule();
  void deleteObjectsOnWorker();

  // xTB objects (created in setup, owned and deleted by the worker).
  xtb_TEnvironment m_env;
  xtb_TMolecule m_mol;
  xtb_TCalculator m_calc;
  xtb_TResults m_results;
  bool m_objectsCreated;

  // Setup parameters (worker-owned working state).
  std::vector<int> m_numbers;
  std::vector<double> m_coordsBohr;
  std::vector<double> m_grad;
  // Read snapshot of the live molecule at the start of the current tick
  // (worker-owned, design D2). The write-back compares each atom's current
  // molecule position against this snapshot and skips any atom that changed
  // externally since the read, so an external edit (another tool, undo, a drag
  // by another surface) is never clobbered by the optimizer's own write-back.
  std::vector<double> m_readSnapshotBohr;
  // Optional live-molecule source (design D2). Non-null only when the session
  // was started with setupFromMolecule(); the worker re-reads it each tick for
  // re-baselining. Cleared on quiesce so it never dereferences a destroyed
  // molecule. Not owned.
  Molecule* m_molecule;
  int m_method;
  int m_level;
  int m_threadsSetting;
  // xTB molecule construction parameters (neutral closed-shell, non-periodic).
  double m_charge;
  int m_uhf;
  double m_lattice[3][3];
  bool m_periodic[3];

  // L-BFGS state.
  Avogadro::Xtb::LBfgs m_lbfgs;
  double m_energy;
  int m_evals;
  double m_gnorm;
  bool m_initialEvalDone;
  double m_threshold;
  int m_maxEvaluations;

  // Shared state.
  // m_mutex guards the xTB library objects (created in setup, used by the
  // worker's singlepoints). The GUI thread NEVER locks m_mutex after setup.
  // m_stateMutex guards the published/drag state that both threads touch, so
  // the GUI thread never blocks on the worker's per-tick xTB work.
  mutable QMutex m_mutex;
  mutable QMutex m_stateMutex;
  QWaitCondition m_tickCond;
  bool m_tickPending;
  QAtomicInt m_stop;
  int m_draggedIndex;
  bool m_interactive;
  bool m_converged;
  bool m_resetHistoryRequested;
  double m_currentAccuracy;
  double m_lastEnergy;
  int m_lastEvalCount;
  std::vector<double> m_latestCoords;
  bool m_coordsValid;
  bool m_quiesced;
  // D4 failed-evaluation latch: when true the phase is idle-with-failure and
  // ticks are consumed without evaluating. Guarded by m_stateMutex.
  bool m_evalFailed;
  QString m_evalFailureReason;
  // D2/D4 setup-broken latch: set when a live-molecule topology re-establishment
  // fails (destroyed the xTB objects and could not recreate them). Unlike
  // m_converged, this must NOT be cleared by resumeOptimizationLocked() or
  // resetPhaseForReBaseline() — those re-arm the optimizer and would otherwise
  // make the next evaluateOnce() call into the destroyed (null) xTB objects,
  // a crash. Cleared only by a successful setup()/setupFromMolecule() or a
  // successful reestablishForTopology(). Guarded by m_stateMutex.
  bool m_setupBroken;
};

} // namespace Avogadro

#endif // XTBENGINE_H
