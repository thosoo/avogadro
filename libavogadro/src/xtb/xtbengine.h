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

  /// Test seam for the projection load-bearing regression (design D8):
  /// disables the bond-graph projection so a test can demonstrate that the
  /// drawn-graph invariant is enforced by the projection. Call before start().
  void setBondGraphProjectionForTesting(bool enabled)
  {
    m_projectionEnabled = enabled;
  }

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
   * @param bondPairs the drawn bond graph (flat begin/end atom-index pairs).
   * The bond-graph projection (design D8) needs it to distinguish drawn
   * bonds (clamped into re-formation reach) from pairs that must stay out of
   * new-bond range; a bondless session enforces no drawn-graph constraints.
   */
  bool setup(const std::vector<int>& atomicNumbers,
             const std::vector<double>& positionsAngstrom, int method, int level,
             int threadsSetting,
             const std::vector<int>& bondPairs = std::vector<int>());

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

  /// Computes the tether targets for EVERY drawn bond from a live bond graph
  /// and positions (design D2/D3/D7). Pure: no engine state, so it is callable
  /// from a headless test. @p bondPairs is a flat vector of atom-index pairs
  /// (begin,end per bond) as read from the live molecule; @p numbers carries
  /// the per-atom atomic numbers (for the repair-cap covalent radii);
  /// @p positionsBohr carries the per-atom positions in bohr. Fills
  /// @p partnerOut with flat atom-index pairs (two entries per tethered bond)
  /// and @p targetOut with the tether target lengths (one per bond, bohr).
  /// The repair cap applies only to bonds incident to @p dragIndex so a drag
  /// does not silently relax the rest of the molecule (design D7). Skips
  /// degenerate (near-zero-length or invalid element) bonds.
  static void computeTetherTargets(int dragIndex,
                                   const std::vector<int>& numbers,
                                   const std::vector<int>& bondPairs,
                                   const std::vector<double>& positionsBohr,
                                   std::vector<int>& partnerOut,
                                   std::vector<double>& targetOut);
  /// Projects a geometry onto the drawn-bond-graph manifold (design D8,
  /// bond-centric enforcement): every drawn bond is clamped inside its
  /// re-formation reach (1.3x the covalent-radii sum) and every non-bonded
  /// pair is pushed out of new-bond range (1.5x the covalent-radii sum, 2.0x
  /// for hydrogen-hydrogen pairs). SHAKE-style: corrections move only the
  /// non-dragged atoms (the dragged atom keeps pointer authority), so after
  /// the projection the backend never evaluates a geometry that violates the
  /// drawn bond graph. Two correction sweeps; deterministic for identical
  /// input. Reads worker-owned session state (m_numbers, m_bondPairs,
  /// m_draggedIndex). @p coordsBohr is modified in place (bohr).
  void projectBondGraph(std::vector<double>& coordsBohr) const;

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
  // lock, copying the current topology fingerprint (atomic numbers), the
  // per-atom positions (A->bohr), and the bond list (flat begin/end atom-index
  // pairs) for the tether capture (design D2). Returns true when the read
  // completed, false when no source is set, the lock was not acquired within
  // the retry budget, or a stop was requested. Never dereferences a
  // null/destroyed molecule.
  bool readLiveMolecule(std::vector<int>& numbers,
                        std::vector<double>& positionsBohr,
                        std::vector<int>& bondPairs);
  // Worker-side tether lifecycle at the drag transition (design D2/D3/D5):
  // reads m_draggedIndex, compares it to m_prevDragIndex, captures the tether
  // targets on a press (drag index -1 -> >=0) and clears them on a release
  // (>=0 -> -1), then records the current index for the next tick. Worker
  // thread only; tether state is worker-owned (no cross-thread access).
  void updateTetherForDrag(const std::vector<int>& numbers,
                           const std::vector<int>& bondPairs,
                           const std::vector<double>& positionsBohr);
  // Resets the quasi-Newton phase (history, eval counter, baseline, converged
  // latch) so the next tick re-baselines from the current working geometry.
  // Worker-thread only; caller must not hold m_stateMutex.
  void resetPhaseForReBaseline();
  // Worker-side re-establishment on a topology change (design D2): destroy and
  // recreate the four xTB objects under m_mutex, adopt the new numbers/positions,
  // re-validate elements, and reset the optimizer phase. Failure routes to the
  // failed-setup latch (deterministic, no per-tick retries). Returns true on
  // success. @p bondPairs is the flat begin/end atom-index bond graph, stored
  // as the session's current topology (design D2/D5).
  bool reestablishForTopology(const std::vector<int>& numbers,
                              const std::vector<double>& positionsBohr,
                              const std::vector<int>& bondPairs);
  // Adopts externally-authored positions when they differ from the working
  // geometry beyond epsilon (excluding the dragged atom), starting a fresh
  // optimization phase. Returns true when adopted.
  bool adoptExternalPositions(const std::vector<double>& livePositionsBohr);
  // Clears the converged/capped idle latch and re-arms the optimizer so a user
  // drag after convergence (or after the evaluation cap) resumes evaluation
  // from the dragged geometry. Caller must hold m_stateMutex.
  void resumeOptimizationLocked();
  // Starts a fresh optimization phase from the current working geometry
  // (re-baseline, eval-counter reset, history drop, wake). The unconditional
  // body of resumeOptimizationLocked(); used directly on drag release where
  // the unmask changes the released atom's gradient so the old baseline is
  // stale. Caller must hold m_stateMutex.
  void resetPhaseLocked();
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
  // The session's current bond graph, as flat begin/end atom-index pairs
  // (worker-owned). Compared against the live read each tick so a bond
  // addition/removal (which does not change atom numbers) still triggers the
  // one-time topology re-establishment (design D2/D5), which clears the tether
  // so a removed bond leaves no stale tether.
  std::vector<int> m_bondPairs;
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
  // Tether state (design D2/D3/D5/D7): worker-owned. m_tetherBondIndices
  // holds the flat atom-index pairs of every drawn bond (two entries per
  // bond) and m_tetherTargetBohr holds the corresponding target length in
  // bohr (one per bond). Cleared on drag end, topology re-establishment,
  // quiesce, and setup(). m_prevDragIndex is the previous tick's drag index,
  // used to detect the drag-start/end transition. All touched only on the
  // worker thread (except setup(), which runs before the thread starts), so
  // no mutex guards them.
  std::vector<int> m_tetherBondIndices;
  std::vector<double> m_tetherTargetBohr;
  // Bond-graph projection (design D8): true (the shipped behavior) while the
  // projector clamps drawn bonds into re-formation reach and pushes
  // non-bonded pairs out of new-bond range on every optimizer trial. Test
  // seam only; set before start(). Worker reads it in the projector.
  bool m_projectionEnabled = true;
  int m_prevDragIndex;
  bool m_interactive;
  bool m_converged;
  bool m_resetHistoryRequested;
  double m_currentAccuracy;
  // Design D3: when true, the next singlepoint after a drag release uses an
  // elevated SCF iteration budget (one bounded attempt) so a strained but
  // recoverable released geometry converges into the session instead of
  // latching an evaluation failure. Consumed exactly once by evaluateOnce.
  // Guarded by m_stateMutex.
  bool m_releaseElevatedIter;
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
