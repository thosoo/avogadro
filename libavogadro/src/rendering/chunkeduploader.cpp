/**********************************************************************
  ChunkedUploader implementation (gpu-rendering-pipeline task 2.2).

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 *****************************************************************/

#include "chunkeduploader.h"

namespace Avogadro {
namespace rendering {

ChunkedUploader::ChunkedUploader(int budgetMicros)
    : m_budgetMicros(budgetMicros > 0 ? budgetMicros : kDefaultBudgetMicros) {}

void ChunkedUploader::setBudgetMicros(int budgetMicros) {
  m_budgetMicros = budgetMicros > 0 ? budgetMicros : kDefaultBudgetMicros;
}

void ChunkedUploader::beginFrame() {
  m_frameMicros = 0.0;
  m_maxSingleUploadMicros = 0.0;
  m_accepted = 0;
  m_deferred = 0;
}

int ChunkedUploader::schedule(const std::vector<Range>& ranges,
                              const CostFn& costFn) {
  beginFrame();
  const bool haveCost = static_cast<bool>(costFn);
  bool overflow = false;
  for (const Range& r : ranges) {
    if (r.bytes == 0)
      continue;
    double est = haveCost ? costFn(r.bytes) : 0.0;
    // Once the budget is exhausted every subsequent range is deferred: a rebuild
    // uploads contiguously, so the overflow point is a clean frame boundary.
    if (!overflow && (m_frameMicros + est) <= m_budgetMicros) {
      m_frameMicros += est;
      ++m_accepted;
    } else {
      overflow = true;
      ++m_deferred;
    }
  }
  return m_accepted;
}

void ChunkedUploader::recordUploadMicros(double us) {
  if (us > m_maxSingleUploadMicros)
    m_maxSingleUploadMicros = us;
}

} // namespace rendering
} // namespace Avogadro
