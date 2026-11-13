## Context

`fix-xtb-session-editing` made xTB sessions live-editable: the worker re-baselines from the
molecule each tick, the drag atom's gradient is masked (`evaluateOnce` zeroes it, the
re-baseline copies the pointer position into the working coords), and `resumeOptimizationLocked()`
(reached only from `setDraggedAtom`, i.e. press and release) clears the converged latch. The
convergence and cap checks run on that masked gradient (`xtbengine.cpp:985`, `shouldStop` at
500 evals per phase), and the converged-skip consumes ticks before any molecule read registers
drag movement — and the dragged atom is excluded from `adoptExternalPositions`' diff by design
(pulling must not reset the optimizer phase). Net effect, observed in the field: hold a pull
longer than the free atoms need to relax → converged idle mid-drag → relaxation frozen → the
user can pull a bond apart with zero internal resistance → release lands on a dissociated
geometry whose singlepoint fails SCF → the `m_evalFailed` latch (which release does NOT clear —
`resumeOptimizationLocked` only clears `m_converged`) freezes the session with a static bond
record still drawn in the GUI.

Constraints: the canonical spec's interactive-performance requirement already demands
responsive relaxation during drags; the fix-xtb-session-editing change is verified and pending
archive, and this change's release-recovery requirement is written to complement (not modify)
its failed-evaluation containment requirement; MSVC rules apply (mutable mutexes, named RAII
holders); the force-field backend has no convergence latch, so all changes stay xtb-engine-local.

## Goals / Non-Goals

**Goals:**
- A held pull never idles the session: relaxation continues for the whole drag.
- The evaluation cap cannot freeze an active drag.
- Release at a strained geometry converges into the session; release at a dissociated geometry
  is contained with a failure diagnostic that names the recovery path.
- The user can see bond strain developing during the drag.

**Non-Goals:**
- Changing L-BFGS itself (`lbfgs.{h,cpp}` is untouched; masked-entry/masked-exit gradient pairs
  are self-consistent for the reduced problem).
- Live bond re-perception or distance-based bond pruning in the GUI (churns undo, races the
  worker; the HUD report makes the divergence visible instead).
- Changing convergence thresholds, tick cadence, or the force-field backend.
- Reworking the drag write-back authority (design D3 of fix-xtb-session-editing is unchanged).

## Decisions

**D1 — Release-gate the idle latches (do not latch converged or capped while a drag is in
flight).** In both convergence-check sites (`xtbengine.cpp` baseline-check and post-step check)
and in `shouldStop`'s cap handling, the latch is skipped while `m_draggedIndex >= 0`: the
session keeps ticking at interactive accuracy until release. Release runs the existing
`resumeOptimizationLocked()` (counter reset, fresh phase), so a genuinely converged released
system reaches the converged idle immediately — the deferral changes *when* the idle engages,
never the final state. Alternative considered — waking the latch on pointer movement (tool pings
the engine per move) — rejected: it re-freezes on every hold, which is exactly the reported
behavior, and adds a GUI→worker ping path for no gain over simply not latching.

**D2 — Drag-scoped evaluation budget.** The 500-eval cap is per-phase; a drag-start resets it
(today), but a long drag exhausts it mid-drag. While dragging, the cap does not latch (D1);
to keep the budget bounded, each pointer-authored movement of the dragged atom resets the
evaluation counter (the pull is the phase's purpose; the reset is bounded by user movement, not
per tick). This mirrors how `adoptExternalPositions` treats external edits as fresh-phase
triggers, applied only to the masked atom.

**D3 — Elevated first evaluation at release.** `setDraggedAtom(-1)` already restores full
accuracy and resets the phase. The fresh phase's first evaluation runs with an elevated SCF
iteration budget (e.g. `2 × kXtbDefaultMaxIter`, one attempt, no retry-after-failure): a
strained-but-recoverable geometry converges instead of latching. If it fails, the existing D4
containment latch applies unchanged. This keeps the containment contract intact ("no retries on
subsequent ticks") — the elevated attempt *is* the phase's first evaluation, not a retry.
Alternative considered — retrying after a failed release baseline — rejected: contradicts the
no-retry determinism wording for no user-visible gain over elevating the first attempt.

**D4 — Failure diagnostics name the recovery path.** `latchEvaluationFailure` messages gain a
recovery clause ("restart the session (Stop/Start) or edit the molecule to re-establish it").
Cheap, addresses the silent-frozen-session feel of the dissociated-release ending.

**D5 — HUD strain report is tool-side, computed from molecule data.** During an active drag,
the tool computes the dragged atom's distance to its bonded partners from the live molecule
(read lock held by the existing timer path) and appends a strain line to the existing HUD text
when the distance exceeds `1.3 × (sum of covalent radii)` — the same perception convention
Avogadro uses elsewhere. No engine changes, no new cross-thread state beyond the drag index the
tool already owns. The report clears at release or when distance returns in range.

**D6 — Test strategy (no GL required).**
- Engine: hold-pull convergence test — start a session from a molecule source, set a drag
  atom far from equilibrium, hold it across many ticks, assert evaluations continue
  (progress counter advances) even after the free atoms' masked gradient would converge;
  assert the converged latch engages only after release.
- Engine: cap-deferral test — verify a drag longer than the cap budget keeps evaluating, and
  the counter reset on pointer movement.
- Engine: release-elevation test — release at a strained geometry asserts the elevated first
  evaluation converges (session continues); release at a dissociated geometry asserts exactly
  one latch and no publish (existing containment test pattern).
- Tool (`autooptreprotest`): hold-and-pull keeps the free atoms publishing (write-back count
  advances across a held pull); HUD strain line appears when the drag exceeds bonding range and
  clears at release.

## Risks / Trade-offs

- **[Continuous SCF during drags]** never latching mid-drag means evaluation runs for the whole
  drag. → Interactive accuracy (0.2) and the bounded iteration budget exist for exactly this;
  cost is bounded by the drag duration and the small per-evaluation cost at reduced accuracy.
- **[Cap reset on pointer movement could unbound total work]** a user wiggling the pointer
  indefinitely resets the counter forever. → Accepted: the FF backend optimizes unboundedly by
  design; a wiggle is deliberate interaction, and the cap's purpose (cap a *stubborn* automatic
  phase) is unaffected. The idle state still engages once the drag ends.
- **[Elevated release budget masks a genuinely broken geometry for one evaluation]** → one
  bounded attempt at 2× iterations is tens of ms on interactive-scale systems; the containment
  latch then applies as before.
- **[HUD strain threshold false positives]** unusual-but-valid geometries (transition states,
  van der Waals complexes) could show strain lines. → Threshold uses the standard 1.3× covalent
  convention; the report is informational, names the dragged atom, and clears on release.
- **[MSVC divergence]** new code touches state guarded by `m_stateMutex` (already `mutable`)
  and no new threading primitives. → Windows installer offscreen ctest leg compiles and runs the
  new tests.

## Migration Plan

Single PR against master; no settings or data migration; force-field backend untouched.
Rollback = revert the commit. Windows verification rides the installer workflow's offscreen
ctest leg (`gh workflow run 168745168 --ref <branch>`), which includes the xtb engine and tool
tests.

## Open Questions

None blocking. (If field testing shows the elevated release budget insufficient for common
dissociations, the next knob is accuracy reduction on the release attempt — spec-compatible:
the requirement fixes only "one bounded elevated attempt", not its exact shape.)
