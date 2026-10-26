// Forward-progress adoption of modern-tier worker results
// (fix-modern-tier-revision-churn, design D1).
//
// Worker results are tagged with the molecule revision (generation) they were
// built for. Under sustained molecule-revision churn the live revision
// advances faster than builds complete, so the old exact-revision gate
// (adopt only when generation == molecule->revision(), discard when
// generation < revision) is an unreachable target: every build is stale on
// arrival, discarded, resubmitted, and the display freezes at the last adopted
// revision. Adoption therefore makes forward progress instead: a result is
// adopted only when it is strictly newer than what has already been presented,
// and everything older-or-equal is discarded. Monotonicity guarantees a stale
// result can never overwrite newer presented content, and presentation latency
// becomes bounded by one build regardless of churn rate.
//
// This header isolates the *decision* from the GLWidget state so it can be unit
// tested without a GL context (the modern instanced backend is a no-op there).
#pragma once

namespace Avogadro {
namespace rendering {

//! Outcome of classifying a worker result for forward-progress adoption.
enum class ResultAdoption {
  Adopt,   //!< strictly newer than presentedRevision: store and present it.
  Discard, //!< older-or-equal to presentedRevision: drop it.
};

//! Decide whether a worker result built for `generation` shall be adopted, and
//! whether a fresh build of the live molecule should be resubmitted, for the
//! modern-tier worker path.
//!
//! Parameters:
//! - `generation`: the molecule revision the result was built for.
//! - `presentedRevision`: generation of the last ADOPTED result (the
//!   forward-progress cursor). Monotonic — never regresses.
//! - `currentRevision`: the molecule's current live revision.
//! - `submittedRevision`: the revision already submitted to the pool (0 =
//!   nothing submitted).
//! - `resubmit`: set on return to whether a new build of the live molecule is
//!   warranted (currentRevision != submittedRevision).
//!
//! Rules (design D1):
//! - Adopt when `generation > presentedRevision` (strictly newer than anything
//!   presented); discard otherwise (older-or-equal: superseded by a newer
//!   presented result, or an exact duplicate).
//! - In both cases, resubmit the newest live revision when it has not already
//!   been submitted, so the display catches up once churn stops.
inline ResultAdoption decideResultAdoption(unsigned int generation,
                                           unsigned int presentedRevision,
                                           unsigned int currentRevision,
                                           unsigned int submittedRevision,
                                           bool &resubmit) {
  resubmit = (currentRevision != submittedRevision);
  return generation > presentedRevision ? ResultAdoption::Adopt
                                        : ResultAdoption::Discard;
}

} // namespace rendering
} // namespace Avogadro
