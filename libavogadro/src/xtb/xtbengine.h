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

  /// Request that the engine stop (atomic flag + wake). The worker finishes
  /// any in-flight singlepoint before observing the stop.
  void stop();

  /// Allow one more tick's worth of work (pacing driven by the GUI tool).
  void requestTick();

  /// Set the atom being dragged (-1 = none). Resets the L-BFGS history.
  void setDraggedAtom(int index);

  /// Record the pointer-authored position (bohr) for the dragged atom.
  void notifyDragPosition(int index, const std::vector<double>& positionBohr);

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

protected:
  void run() override;

private:
  bool createObjects();
  void destroyObjects();
  bool loadMethod();
  bool validateElements();
  void applyOmpEnv(int threadsSetting);
  double evaluateOnce(const std::vector<double>& coords, std::vector<double>& grad);
  void applyDragToWorkingCoords();
  // Clears the converged/capped idle latch and re-arms the optimizer so a user
  // drag after convergence (or after the evaluation cap) resumes evaluation
  // from the dragged geometry. Caller must hold m_stateMutex.
  void resumeOptimizationLocked();
  bool shouldStop(int evals) const;
  void publishCoords(const std::vector<double>& coordsBohr);
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
  std::vector<double> m_dragPositionBohr;
  bool m_interactive;
  bool m_converged;
  bool m_resetHistoryRequested;
  double m_currentAccuracy;
  double m_lastEnergy;
  int m_lastEvalCount;
  std::vector<double> m_latestCoords;
  bool m_coordsValid;
  bool m_quiesced;
};

} // namespace Avogadro

#endif // XTBENGINE_H
