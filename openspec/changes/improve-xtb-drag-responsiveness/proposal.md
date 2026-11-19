## Why

With the 30 ms pacing (fix-xtb-drag-latency) the xTB session still feels laggy on drag. On a
timer-bound backend (GFN-FF) each evaluation is ~5 ms, so the pacing timer — not the worker — is
the bottleneck: a pointer move can wait up to a full 30 ms interval before the worker sees it, and
that quantization is the felt delay. On a worker-bound backend (GFN2) the dominant cost is the
evaluation itself, which this change deliberately does not touch; the pointer-driven tick still
removes the idle-gap component of the latency there.

## What Changes

- **Pointer-driven tick requests**: the AutoOpt tool's `mouseMoveEvent` calls `XtbEngine::requestTick()`
  so a pointer-authored movement registers with the worker on the next loop pass, instead of waiting
  for the pacing timer. `requestTick()` collapses to a single pending flag and wakes the worker, so
  the extra call cannot queue work or overrun the worker — it only removes the idle gap. The pacing
  timer remains as the steady-state driver (it keeps a slow evaluation busy between moves) and for
  pointer jitter below a real movement.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities

- `auto-optimize-interaction`: modify the interactive-performance requirement to pin that a
  pointer-authored movement in an xTB session registers with the worker within a bounded number of
  frames (pointer-driven tick), so the drag latency on a timer-bound backend is not gated by the
  pacing interval.

## Impact

- `libavogadro/src/tools/autoopttool.cpp` — `mouseMoveEvent` now calls `m_xtbEngine->requestTick()`
  when an xTB session is active.
- No engine changes, no new threading primitives, no API changes. The `XtbEngine::requestTick`
  collapse semantics are unchanged.
- Tests: `ctest -R "xtbengineTest|autooptreproTest"` must stay green (autooptreproTest 26.5s pass,
  xtbengineTest 30.6s pass on this machine).
