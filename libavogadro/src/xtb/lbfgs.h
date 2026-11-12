/**********************************************************************
  LBfgs - Limited-memory BFGS geometry optimizer

  Standard two-loop-recursion L-BFGS with a backtracking Armijo line search,
  implemented over a flat coordinate vector with an energy/gradient callback.
  Convergence is caller-declared: the caller compares gradNorm() against its
  own thresholds.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 ***********************************************************************/

#ifndef LBFGS_H
#define LBFGS_H

#include <avogadro/global.h>

#include <vector>
#include <cmath>

namespace Avogadro {
namespace Xtb {

/**
 * @class LBfgs
 * @brief Limited-memory BFGS (L-BFGS) minimizer over a flat coordinate array.
 *
 * Pure logic: no Qt, no Eigen, no xTB. The optimizer calls an energy/gradient
 * callback and a stop callback. Convergence is caller-declared through
 * shouldStop() and gradNorm().
 *
 * Two entry points:
 *  - minimize(): runs the full optimization to convergence/cap in one call.
 *  - step(): runs a single L-BFGS iteration, letting a caller (e.g. the xTB
 *    engine) drive pacing and publish accepted coordinates between iterations.
 */
class A_EXPORT LBfgs
{
public:
  /// Default L-BFGS history length (number of stored (s, y) pairs).
  static const int kLBfgsHistory = 10;

  LBfgs();
  ~LBfgs();

  /// Set the number of stored (s, y) pairs; resets the history.
  void setHistory(int m);

  /// Discard all stored history (used when the masked variables change).
  void resetHistory();

  /// Result of a minimize() run.
  struct Result
  {
    bool converged;       ///< true when the gradient norm reached ~0
    int evaluations;      ///< number of evaluate() calls made
    double finalGradNorm; ///< gradient norm at the final coordinates
  };

  /// L2 norm of a gradient vector.
  double gradNorm(const std::vector<double>& grad) const;

  /**
   * Run the L-BFGS minimization to convergence or until shouldStop returns
   * true.
   *
   * @param coords  flat coordinate vector (length N); updated in place.
   * @param evaluate returns the energy for @p coords and fills @p grad with
   *   the gradient (same length as coords). Signature:
   *   double(const std::vector<double>& coords, std::vector<double>& grad).
   * @param shouldStop returns true when the caller wants to stop (cancelled
   *   or a convergence cap). Signature: bool(int evaluations).
   */
  template <typename Eval, typename Stop>
  Result minimize(std::vector<double>& coords, Eval&& evaluate, Stop&& shouldStop)
  {
    const int n = static_cast<int>(coords.size());
    std::vector<double> grad(n, 0.0);
    double energy = evaluate(coords, grad);
    int evals = 1;
    double gnorm = gradNorm(grad);
    bool converged = false;

    while (step(coords, energy, grad, evals, gnorm, evaluate, shouldStop)) {
      // step() returns false on the numerical floor, a caller stop, or the
      // hard iteration cap.
    }
    if (gnorm < kGradFloor) {
      converged = true;
    }
    return Result{converged, evals, gnorm};
  }

  /**
   * Run a single L-BFGS iteration: compute the search direction, carry out the
   * backtracking Armijo line search, update the (s, y) history, and advance
   * @p coords by one accepted step. Returns false when the caller should stop
   * (shouldStop returned true, the gradient hit the numerical floor, or the
   * hard iteration cap was reached). State (energy, grad, evals, gnorm) is
   * carried by the caller between calls.
   *
   * @param coords  current coordinates (advanced by one step on return).
   * @param energy  current energy (in/out).
   * @param grad    current gradient (in/out).
   * @param evals   evaluation counter (in/out).
   * @param gnorm   gradient norm (in/out).
   * @param evaluate energy/gradient callback (see minimize()).
   * @param shouldStop stop callback (see minimize()).
   */
  template <typename Eval, typename Stop>
  bool step(std::vector<double>& coords, double& energy, std::vector<double>& grad,
            int& evals, double& gnorm, Eval&& evaluate, Stop&& shouldStop)
  {
    const int n = static_cast<int>(coords.size());

    if (gnorm < kGradFloor || shouldStop(evals) || ++m_iterations > kMaxIterations) {
      return false;
    }

    // Compute the search direction via the two-loop recursion.
    std::vector<double> q = grad;
    const int k = static_cast<int>(m_s.size());
    if (k > 0) {
      m_alpha.assign(k, 0.0);
      for (int i = k - 1; i >= 0; --i) {
        double dot = 0.0;
        for (int j = 0; j < n; ++j) {
          dot += m_s[i][j] * q[j];
        }
        m_alpha[i] = m_rho[i] * dot;
        for (int j = 0; j < n; ++j) {
          q[j] -= m_alpha[i] * m_y[i][j];
        }
      }
      // Initial inverse-Hessian scale from the most recent pair.
      double ys = 0.0;
      double yy = 0.0;
      for (int j = 0; j < n; ++j) {
        ys += m_s[k - 1][j] * m_y[k - 1][j];
        yy += m_y[k - 1][j] * m_y[k - 1][j];
      }
      double scale = (yy > 0.0) ? (ys / yy) : 1.0;
      for (int j = 0; j < n; ++j) {
        q[j] *= scale;
      }
      for (int i = 0; i < k; ++i) {
        double dot = 0.0;
        for (int j = 0; j < n; ++j) {
          dot += m_y[i][j] * q[j];
        }
        double beta = m_rho[i] * dot;
        for (int j = 0; j < n; ++j) {
          q[j] += (m_alpha[i] - beta) * m_s[i][j];
        }
      }
    }
    // The two-loop recursion computes H*g; the descent direction is -H*g.
    std::vector<double> dir(n, 0.0);
    for (int j = 0; j < n; ++j) {
      dir[j] = -q[j];
    }

    double gradDot = 0.0;
    for (int j = 0; j < n; ++j) {
      gradDot += grad[j] * dir[j];
    }

    // Safeguard: if the computed direction is not a descent direction, fall
    // back to steepest descent (Armijo with gradDot >= 0 would otherwise
    // accept an uphill step).
    if (gradDot >= 0.0) {
      for (int j = 0; j < n; ++j) {
        dir[j] = -grad[j];
      }
      gradDot = 0.0;
      for (int j = 0; j < n; ++j) {
        gradDot += grad[j] * dir[j];
      }
    }

    std::vector<double> newCoords(n, 0.0);
    std::vector<double> newGrad(n, 0.0);
    std::vector<double> s(n, 0.0);
    std::vector<double> y(n, 0.0);

    // Backtracking Armijo line search.
    double step = 1.0;
    double energyNew = energy;
    bool accepted = false;
    for (int trial = 0; trial < kMaxShrink; ++trial) {
      for (int j = 0; j < n; ++j) {
        newCoords[j] = coords[j] + step * dir[j];
      }
      energyNew = evaluate(newCoords, newGrad);
      ++evals;
      if (shouldStop(evals)) {
        // Accept this trial point and stop (caller-declared cap).
        coords = newCoords;
        grad = newGrad;
        energy = energyNew;
        gnorm = gradNorm(grad);
        return false;
      }
      if (energyNew <= energy + kArmijoC1 * step * gradDot) {
        accepted = true;
        break;
      }
      step *= kShrink;
    }
    if (!accepted) {
      // Fallback: a small fixed step scaled by the inverse gradient norm.
      double invNorm = (gnorm > 0.0) ? (1.0 / gnorm) : 1.0;
      step = 1e-3 * invNorm;
      for (int j = 0; j < n; ++j) {
        newCoords[j] = coords[j] + step * dir[j];
      }
      energyNew = evaluate(newCoords, newGrad);
      ++evals;
      if (shouldStop(evals)) {
        coords = newCoords;
        grad = newGrad;
        energy = energyNew;
        gnorm = gradNorm(grad);
        return false;
      }
    }

    // Record the (s, y) pair for the accepted step.
    double sy = 0.0;
    for (int j = 0; j < n; ++j) {
      s[j] = newCoords[j] - coords[j];
      y[j] = newGrad[j] - grad[j];
      sy += s[j] * y[j];
    }
    if (sy > 1e-12) {
      if (static_cast<int>(m_s.size()) >= m_history) {
        m_s.erase(m_s.begin());
        m_y.erase(m_y.begin());
        m_rho.erase(m_rho.begin());
      }
      m_s.push_back(s);
      m_y.push_back(y);
      m_rho.push_back(1.0 / sy);
    }

    // Accept the step.
    coords = newCoords;
    grad = newGrad;
    energy = energyNew;
    gnorm = gradNorm(grad);

    // Report whether the caller should continue.
    if (gnorm < kGradFloor) {
      return false;
    }
    if (shouldStop(evals)) {
      return false;
    }
    return true;
  }

private:
  static const int kMaxShrink = 20;
  static const int kMaxIterations = 100000;
  static const double kArmijoC1;
  static const double kShrink;
  static const double kGradFloor;

  int m_history;
  int m_iterations;
  std::vector<std::vector<double>> m_s; // position differences
  std::vector<std::vector<double>> m_y; // gradient differences
  std::vector<double> m_rho;            // 1 / (s^T y)
  std::vector<double> m_alpha;          // scratch for two-loop recursion
};

} // namespace Xtb
} // namespace Avogadro

#endif // LBFGS_H
