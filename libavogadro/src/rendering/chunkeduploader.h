/**********************************************************************
  ChunkedUploader - time-budgeted, chunked instance-buffer upload
  scheduler (gpu-rendering-pipeline task 2.2).

  The submit stage must upload a possibly large set of instance-record ranges
  to the GPU every frame, but a single frame may spend no more than a fixed
  budget of CPU time on uploads (default 2 ms) or input stalls. ChunkedUploader
  slices the work: it accepts the prefix of pending ranges whose estimated
  upload cost fits in the remaining per-frame budget and defers the rest for the
  next frame, so a 50k-atom rebuild is never uploaded in one unbounded stall.

  The scheduling decision is decoupled from OpenGL via a cost function
  (microseconds for a byte count), so the scheduler is unit-testable headlessly
  with a synthetic cost model. The real backend supplies a measured, calibrated
  cost model and the actual glBufferSubData calls.

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

#ifndef AVOGADRO_RENDERING_CHUNKEDUPLOADER_H
#define AVOGADRO_RENDERING_CHUNKEDUPLOADER_H

#include <cstddef>
#include <functional>
#include <vector>

namespace Avogadro {
namespace rendering {

class ChunkedUploader {
public:
  //! Estimated upload cost: microseconds required to upload `bytes`.
  using CostFn = std::function<double(std::size_t bytes)>;

  //! Default per-frame upload budget in microseconds (2 ms).
  static constexpr int kDefaultBudgetMicros = 2000;

  explicit ChunkedUploader(int budgetMicros = kDefaultBudgetMicros);

  void setBudgetMicros(int budgetMicros);
  int budgetMicros() const { return m_budgetMicros; }

  //! Reset the per-frame accumulator. Call once per frame before scheduling.
  void beginFrame();

  //! A pending upload: a byte range within one instance VBO.
  struct Range {
    std::size_t offset = 0; // byte offset into the VBO
    std::size_t bytes = 0;  // byte length to upload
  };

  //! Decide which pending ranges fit in the remaining budget this frame.
  //!
  //! Ranges are considered in order; a range is accepted while its estimated
  //! upload cost keeps the frame accumulator within the budget, and every range
  //! afterwards is deferred (a scene rebuild uploads contiguously, so once the
  //! budget is exhausted the rest waits for the next frame). Returns the number
  //! of accepted ranges; deferredCount() reports the rest.
  int schedule(const std::vector<Range>& ranges, const CostFn& costFn);

  //! Record the measured upload time of one accepted range (microseconds) so
  //! the backend can report the true worst-case single upload for budget
  //! verification. Does not affect the scheduling decision (that uses the cost
  //! estimate); it only tracks the maximum for assertion/logging.
  void recordUploadMicros(double us);

  int acceptedCount() const { return m_accepted; }
  int deferredCount() const { return m_deferred; }
  int totalScheduled() const { return m_accepted + m_deferred; }

  //! Largest single measured upload this frame (microseconds). 0 if none.
  double maxSingleUploadMicros() const { return m_maxSingleUploadMicros; }

  //! Whether the whole scheduled set was accepted (nothing deferred this frame).
  bool fullyAccepted() const { return m_deferred == 0; }

private:
  int m_budgetMicros;
  double m_frameMicros = 0.0;
  double m_maxSingleUploadMicros = 0.0;
  int m_accepted = 0;
  int m_deferred = 0;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_CHUNKEDUPLOADER_H
