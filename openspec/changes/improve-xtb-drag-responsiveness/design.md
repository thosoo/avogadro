## Context

See proposal.md for motivation. The xTB interaction loop is: pointer move → `AutoOptTool::mouseMoveEvent`
writes the drag position into the live molecule (immediate) → the tool's pacing timer fires →
`XtbEngine::requestTick()` → worker re-baselines and evaluates one L-BFGS step at interactive accuracy
→ `publishCoords()` + `writeBackToMolecule()` (worker-side, immediate) → revision bump drives the
modern-tier rebuild and repaint.

With the 30 ms pacing in place (`fix-xtb-drag-latency`), the remaining quantization is the pacing
timer: a pointer move can wait up to a full 30 ms interval before the worker sees it. On a
timer-bound backend (GFN-FF, ~5 ms/step) that interval — not the worker — is the felt latency.

## Goals / Non-Goals

**Goals:**
- A pointer-authored movement of a dragged atom registers with the worker on the next loop pass, so
  the drag latency on a timer-bound backend is bounded by evaluation cost rather than gated by the
  pacing interval.
- Zero change to the collapse/latch semantics: `requestTick()` still sets only a single pending flag,
  so the extra call cannot queue work or overrun the worker.

**Non-Goals:**
- Cheaper drag-time evaluation (lower interactive accuracy/SCF budget). Investigated and REJECTED:
  lowering `kXtbInteractiveAccuracy` (0.2→0.1) or `kXtbInteractiveMaxIter` (30→20) makes the
  drag-time gradient less accurate, so the session hits the convergence threshold prematurely at a
  stretched drag geometry and stops evaluating — breaking the drag-resume contract. Verified both
  directions: `dragAfterConvergenceResumesEvaluation` fails with accuracy 0.1 and with maxIter 20,
  passes with the original 0.2/30. This lever needs a loosened convergence threshold and is a
  separate concern.
- Adaptive pacing (interval derived from measured evaluation time) — deferred.

## Decisions

**D1 — Pointer-driven tick from `mouseMoveEvent`.** `AutoOptTool::mouseMoveEvent` calls
`m_xtbEngine->requestTick()` when an xTB session is active. `requestTick()` sets a single pending
flag under `m_stateMutex` and wakes the worker (`xtbengine.cpp`), so an extra call here cannot queue
work or overrun the worker — it only removes the idle gap. The pacing timer remains as the
steady-state driver (it keeps a slow evaluation busy between moves) and for pointer jitter below a
real movement. Alternatives considered: a shorter timer (16 ms) — the pointer-driven tick removes
the interval entirely rather than shrinking it, and keeps the steady-state timer at 30 ms.

**D2 — Rejected: cheaper drag-time evaluation.** Lowering the interactive accuracy or SCF budget
degrades the drag-time gradient enough to fake convergence at a stretched geometry, so the session
stops evaluating mid-drag (violates the "holding a stretched pull does not idle the session"
scenario). Keeping the original 0.2/30 preserves the drag-resume contract.

**D3 — Collapse semantics unchanged.** `requestTick()` collapses while an evaluation is in flight, so
the faster/pointer-driven tick cadence cannot queue unbounded work; the worker runs back-to-back when
evaluations are cheap and stays busy when they are slow. The converged/cap latches still stop
evaluation regardless of the tick rate.

## Risks / Trade-offs

- **[Extra `requestTick()` calls during a drag]** — the pointer moves many times per second, so the
  worker receives a wake on every move. → Accepted: `requestTick()` collapses to a single pending
  flag, so this is bounded by the evaluation rate, not the pointer rate; it only removes idle gaps
  between a move and the next loop pass.
- **[GFN2 stays worker-bound]** — the dominant drag delay on GFN2 is the evaluation cost, which this
  change does not touch. → Accepted and documented: the pointer-driven tick removes only the
  idle-gap component; the evaluation-cost component needs the (rejected) cheaper-evaluation lever or
  a loosened convergence threshold.
