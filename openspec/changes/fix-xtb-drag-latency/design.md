## Context

The xTB interaction loop is: pointer move → `AutoOptTool::mouseMoveEvent` writes the drag position
into the live molecule (immediate) → the tool's pacing timer fires → `XtbEngine::requestTick()` →
worker re-baselines and evaluates one L-BFGS step at interactive accuracy → `publishCoords()` +
`writeBackToMolecule()` (worker-side, immediate) → revision bump drives the modern-tier rebuild and
repaint. The only quantization left in the chain is the pacing timer: `startTimer(100)` in
`autoopttool.cpp` (introduced as deliberate CPU pacing). `XtbEngine::requestTick()` sets a single
pending flag under `m_stateMutex` and wakes the worker (`xtbengine.cpp:230`), so requests collapse —
a faster timer cannot queue work or overrun the worker; it only removes idle gaps.

Constraints: the convergence idle, evaluation cap, and drag deferral gates from the archived
`fix-xtb-drag-pause` (D1/D2) must keep their exact semantics — the spec's "converged session goes
idle" and "maximum evaluation count caps a stubborn session" scenarios are latch-driven, not
timer-driven, and stay untouched. MSVC rules apply (trivial change: one integer constant and a
comment). No new threading primitives; no API changes.

## Goals / Non-Goals

**Goals:**
- Pointer movement registers with the worker within at most a couple of frames.
- Relaxation advances at the evaluation rate on small molecules (≈33 L-BFGS steps/s ceiling instead
  of 10).
- Zero change to latch/idle semantics; converged and capped sessions keep returning CPU to idle.

**Non-Goals:**
- Adaptive pacing (interval derived from measured evaluation time) — deferred; the fixed interval is
  the experiment first.
- Pointer-driven tick requests from `mouseMoveEvent` — deferred; evaluate only if the fixed interval
  still feels laggy.
- Any engine-internal change (accuracy, SCF budget, latch conditions, cap handling).
- GUI-thread work reduction; thread confinement is already correct.

## Decisions

**D1 — Fixed 30 ms pacing interval.** `startTimer(100)` → `startTimer(30)` with the comment updated.
30 ms (≈33 ticks/s) is chosen over 16 ms deliberately: it is already faster than interactive-accuracy
singlepoints on small-to-mid molecules (so evaluations run back-to-back there), while leaving headroom
on the CPU-duty-cycle risk for mid-size molecules whose evaluations land in the 20–50 ms range. The
collapse semantics make the interval purely an idle-gap remover, never a queue. If field feel demands
more, 16 ms is a one-constant follow-up; if CPU use disappoints, adaptive pacing (D2 below) is the
next knob.

**D2 — No adaptive logic in this change.** An interval clamped to the measured evaluation time adds
state and a measurement path to the tool; the user explicitly chose fixed-first to test whether the
simple change suffices. Deferred by decision, not forgotten.

**D3 — Latch and cap semantics untouched.** The faster timer only changes *when ticks are requested*.
The converged/cap latches still stop evaluation regardless of timer rate (a stopped worker ignores
pending ticks), so "CPU use returns to idle" is preserved. D2 of `fix-xtb-drag-pause` (evaluation
counter reset on pointer-authored movement) continues to bound the drag budget; faster ticks make the
budget refresh *more often*, not less bounded.

**D4 — Verification.** Existing suites (`xtbenginetest`, `autooptreprotest`) drive their own tick
timers and are interval-insensitive; they gate regressions. A tool-level pacing check (progress
counter advancing at the new rate on a small distorted molecule, and staying flat after convergence)
is added if the existing harness makes it cheap; otherwise code inspection plus manual feel checks.
Full ctest + Windows installer offscreen leg per repo policy.

> **D4 empirical note (verified on this machine):** the tool-level pacing-rate assertion was
> attempted and found *not achievable on water* — not merely flaky. A GFN2-xTB water singlepoint
> at interactive accuracy (0.2) takes ~130–250 ms per L-BFGS step (each step's line search does
> several singlepoint trials), so the worker is the bottleneck at ~5–7 evals/s regardless of the
> 30 ms timer. A ">15 evals/s" assertion therefore always fails, and any achievable threshold
> (e.g. >5/s) passes identically under the old 100 ms pacing, so it cannot discriminate the change.
> Per the task's explicit escape hatch, the rate assertion was skipped and the pacing change is
> verified by code inspection (D1/D3: `requestTick` sets only `m_tickPending`; the faster interval
> only removes the idle gap between evaluations and cannot queue or restart work) plus the manual
> feel check (task 3.2). The latch/cap regression guards (`xtbenginetest`, `autooptreprotest` 35/35)
> confirm the faster pacing does not disturb the converged-idle behavior.

## Risks / Trade-offs

- **[Higher CPU duty cycle during sessions]** mid-size molecules whose evaluation takes 30–80 ms now
  keep the worker continuously busy instead of idling ~70% of each 100 ms window. → Accepted: this is
  the same duty cycle the force-field backend has always had, interactive accuracy bounds the cost,
  and the latches stop the burn once converged/capped. Revisit with adaptive pacing if it matters in
  the field.
- **[Timing test flakiness on loaded CI]** a rate assertion ("N evaluations in T ms") can flake under
  load. → Keep tolerances generous (≥15 evals/s discriminates the old 10/s ceiling) or verify by
  inspection only.
- **[Feel still not enough on very large molecules]** where evaluation itself dominates, no timer
  change helps; the deferred pointer-driven ticks are the designed follow-up.

## Migration Plan

Single PR against master; no settings, data, or API changes. Rollback = revert the constant.

## Open Questions

None blocking. (If 30 ms feels insufficient, next steps in order: 16 ms constant → pointer-driven
ticks → adaptive pacing.)
