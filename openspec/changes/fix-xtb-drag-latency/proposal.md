## Why

With `fix-xtb-drag-pause` archived, dragging during an xTB session no longer freezes — but the
feedback loop is still noticeably delayed. The worker only sees pointer movement when the tool's
100 ms pacing timer requests a tick (`autoopttool.cpp` `startTimer(100)`), and each tick advances
L-BFGS by a single iteration, so relaxation crawls at ~10 steps/s even when each interactive-accuracy
singlepoint takes a few milliseconds. The latency chain is: pointer move → (≤100 ms) timer →
evaluation → worker-side write-back → repaint; for small molecules nearly all of it is timer
quantization.

## What Changes

- **Faster fixed pacing**: the xTB session pacing timer drops from 100 ms to 30 ms, so ticks are
  requested at up to ~33 Hz and relaxation advances at the evaluation rate instead of a fixed slow
  cadence. This is safe by construction: `XtbEngine::requestTick()` collapses to a pending flag
  (`m_tickPending`), so a faster timer cannot queue work — the worker just runs back-to-back when
  evaluations are cheap and stays busy when they are slow.
- **Latch semantics unchanged**: the convergence idle, the evaluation cap, and the drag deferral
  gates from `fix-xtb-drag-pause` (D1/D2) are untouched. A converged or capped session still stops
  evaluating and CPU returns to idle; interactive accuracy (0.2) and the bounded SCF budget still
  bound the per-evaluation cost during drags.
- **Deferred (not in this change)**: adaptive pacing (interval clamped to measured evaluation time)
  and pointer-driven tick requests from `mouseMoveEvent`. If the fixed interval proves insufficient,
  those are the follow-up knobs.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities

- `auto-optimize-interaction`: modify the interactive-performance requirement — pin that session
  pacing SHALL NOT gate relaxation below the evaluation rate (tick pacing interval small enough that
  pointer movement registers within a frame or two), while keeping the converged/cap idle and
  drag-deferral behavior exactly as specified today.

## Impact

- `libavogadro/src/tools/autoopttool.cpp` — the `startTimer(100)` interval and its comment.
- Tests: `libavogadro/tests/xtbenginetest.cpp` and `libavogadro/tests/autooptreprotest.cpp` must
  stay green (they drive their own tick timers, so they are insensitive to the tool interval, but
  the full suites gate the change); a pacing-rate assertion may be added at tool level.
- No engine changes, no new threading primitives, no API changes. Windows installer offscreen
  ctest leg re-verification per repo policy.
