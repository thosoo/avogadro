// Overflow handling for the legacy/compat GL_SELECT pick pass (gpu-rendering-
// pipeline task 4.2, design A4).
//
// The selection buffer is capped at SEL_BUF_MAX_SIZE (global.h:116). When it
// fills before every name is recorded, glRenderMode(GL_RENDER) returns -1. The
// old bug stored that return value in an unsigned counter, turning -1 into a
// huge count and silently truncating the hits. This header isolates the
// *decision* made on that return value from the OpenGL calls so the behaviour
// can be unit tested without a compatibility GL context (GL_SELECT is a no-op
// in a core context, so the overflow can never be triggered through it there).
#pragma once

#include <algorithm>

namespace Avogadro {

//! Outcome of classifying a glRenderMode(GL_RENDER) result for the pick pass.
enum class SelectRefine {
  Clean,   //!< >= 0 hits recorded, no overflow: parse them and stop.
  Retry,   //!< -1 (overflow) with refinement left: halve the box, centre fixed.
  Stop,    //!< -1 (overflow) but refinement attempts are exhausted: stop, report.
};

//! Decide the next action for a GL_SELECT pass that returned `renderModeResult`
//! (the value returned by glRenderMode(GL_RENDER)).
//!
//! - A clean result (>= 0) never overflows: return Clean.
//! - An overflow (-1) shrinks the selection rectangle about its fixed centre
//!   (halving width and height, clamped to 1) and asks for a retry, until the
//!   refinement budget `maxAttempts` is spent, after which it returns Stop so
//!   the caller can report rather than silently truncate.
//!
//! On a Retry it updates `x`, `y`, `w`, `h` in place to the next rectangle and
//! increments `attempt`.
inline SelectRefine decideSelectRefine(int renderModeResult, int &attempt,
                                       int maxAttempts, int &x, int &y, int &w,
                                       int &h) {
  if (renderModeResult >= 0)
    return SelectRefine::Clean;

  const int cx = w / 2 + x;
  const int cy = h / 2 + y;
  if (++attempt >= maxAttempts)
    return SelectRefine::Stop;

  const int nw = std::max(1, w / 2);
  const int nh = std::max(1, h / 2);
  x = cx - nw / 2;
  y = cy - nh / 2;
  w = nw;
  h = nh;
  return SelectRefine::Retry;
}

} // namespace Avogadro
