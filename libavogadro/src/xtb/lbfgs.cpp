/**********************************************************************
  LBfgs - Limited-memory BFGS geometry optimizer

  Non-template members. See lbfgs.h for the public interface and the
  template minimize() definition.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 ***********************************************************************/

#include "lbfgs.h"

#include <algorithm>

namespace Avogadro {
namespace Xtb {

const double LBfgs::kArmijoC1 = 1e-4;
const double LBfgs::kShrink = 0.5;
const double LBfgs::kGradFloor = 1e-30;

LBfgs::LBfgs()
  : m_history(kLBfgsHistory)
  , m_iterations(0)
{
}

LBfgs::~LBfgs()
{
}

void LBfgs::setHistory(int m)
{
  m_history = (m > 0) ? m : kLBfgsHistory;
  resetHistory();
}

void LBfgs::resetHistory()
{
  m_s.clear();
  m_y.clear();
  m_rho.clear();
  m_alpha.clear();
  m_iterations = 0;
}

double LBfgs::gradNorm(const std::vector<double>& grad) const
{
  double sum = 0.0;
  for (double g : grad) {
    sum += g * g;
  }
  return std::sqrt(sum);
}

} // namespace Xtb
} // namespace Avogadro
