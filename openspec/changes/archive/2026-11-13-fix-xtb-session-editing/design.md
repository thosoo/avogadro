## Context

The xTB backends shipped as a closed session: `XtbEngine` snapshots numbers/positions at
setup and never reads the live molecule again, while the GUI write-back (AutoOpt 100 ms
timer) drops every tick whose atom count no longer matches. Two consequences, both
reproduced in the field:

1. **Crash (Windows, cdb-captured).** `Avogadro::XtbEngine::evaluateOnce+0x18b` — access
   violation, `mov rax,[rbx]` with `rbx=0`. Chain: `AutoOptTool::mousePressEvent` calls
   `XtbEngine::setDraggedAtom(idx)`, which clears `m_dragPositionBohr` while publishing
   `m_draggedIndex >= 0`; the next tick's pin loop
   (`pinned[di*3+k] = dragPos[k]`, xtbengine.cpp:419) guards `pinned.size()` but **not**
   `dragPos.size()`, reading element 0 of an empty vector (null data pointer). The
   press→first-move window (~100 ms) overlaps the 100 ms tick cadence, so a bare click on
   an atom crashes the process. `applyDragToWorkingCoords` has the size guard;
   `evaluateOnce` does not.
2. **Dead sessions after edits.** Any atom-count change (draw, erase, click-extend,
   hydrogen churn at release, undo/redo) permanently silences the write-back guard
   (`atoms.size() != coordsBohr.size()/3 → return`); same-count edits (manipulate moves,
   element changes) are invisible to the session or reverted at 10 Hz by the write-back.
   The force-field backend never had this problem because its worker re-reads the live
   molecule every tick under the bounded-retry read lock
   (`AutoOptThread::update`, autoopttool.cpp:1090+).

Constraints: the xtb C API is singlepoint-only (no optimizer entry points); the canonical
`auto-optimize-interaction` spec requires every interaction behavior to hold identically
for every backend, and describes worker-side molecule reads under the molecule read lock.

## Goals / Non-Goals

**Goals:**
- Crash-free drag lifecycle: press-before-move (including bare clicks) is safe on every
  backend and timing.
- xTB sessions track the live molecule: edits integrate, nothing freezes, undo doesn't
  snap back, element changes take effect.
- Failed/non-finite evaluations never reach the molecule or the renderer.
- Regression tests that reproduce all three failure classes without GL.

**Non-Goals:**
- Optimizer algorithm selection (L-BFGS/ANCOPT) — separate later change.
- Changing the force-field backend, the xtb API shim, the bundled runtime, or the installer.
- Re-tuning convergence thresholds, tick cadence, or interactive accuracy.

## Decisions

**D1 — Crash fix: enforce the drag invariant at the pin site (minimal, shippable first).**
`evaluateOnce` skips the pin when `dragPos.size() < 3`; `applyDragToWorkingCoords` already
guards. The invariant "dragged index set ⇒ position authored" is additionally enforced at
every write of `m_draggedIndex` (a press alone no longer publishes an index without a
position; `notifyDragPosition` remains the promotion point). Rationale: one guard closes
the AV regardless of call order; the setter discipline prevents the half-authored state
from existing at all. Alternative considered — gate `setDraggedAtom` behind an "armed"
flag — rejected as redundant once the write-side invariant holds.

**D2 — Re-baseline: the worker reads the live molecule per tick (same discipline as the
force-field worker).** `XtbEngine` gains an optional molecule source pointer set at session
start (`setupFromMolecule(Molecule*)`); headless vector setup stays for tests and skips
re-baselining. Per tick, before evaluation: bounded-retry `tryLockForRead` (spin budget +
`std::this::yield`, bail on stop — copy the `AutoOptThread::update` pattern verbatim,
including the named-constructor RAII holder that MSVC requires), then:
- *Topology fingerprint* (atom count + atomic-number sequence) unchanged → copy positions
  (Å→bohr) and diff against the session's working coords with an epsilon above unit
  round-trip noise (~1e-6 bohr), excluding the dragged atom. External change → reset
  L-BFGS history, clear `m_initialEvalDone`, zero the eval counter, clear the converged
  latch (reuse `resumeOptimizationLocked` semantics) so the moved geometry starts a fresh
  phase. Unit round-trip alone (bohr→Å→bohr of our own write-back) must not trip this.
- *Topology changed* → worker-side re-establishment: destroy and recreate the four xTB
  objects (existing worker-owned `destroyObjects`/`createObjects` + `loadMethod` path,
  under `m_mutex`), re-validate elements, reset the optimizer and counters — exactly one
  re-establishment per topology change; a failure routes to the existing failed-setup
  latch (deterministic, no per-tick retries).
The GUI thread never gains new locking; the molecule pointer is cleared on quiesce
(`molecule destroyed → abort()` already waits for the in-flight tick first).
Alternative considered — GUI-side change detection pushing vectors into an engine buffer —
rejected: needs coalescing against signal storms, duplicates state in the tool, and
diverges from the spec's worker-side read discipline.

**D3 — Molecule becomes the drag authority; the position side-channel dissolves.** With D2,
the dragged atom's pointer-authored position already lives in the molecule when the worker
reads it (`translate()` wrote it on mouse move; on press-before-move it holds the atom's
pre-drag position — which is exactly the safe value the spec scenario requires). The pin
step reduces to masking: `setDraggedAtom(index)` keeps only the index (gradient zeroing,
write-back skip, interactive accuracy switch); `notifyDragPosition` and
`m_dragPositionBohr` are deleted. The D1 guards remain as defense in depth during
transition and for the headless (no molecule source) path used by tests.

**D4 — Failed evaluations latch the phase deterministically.** After every
`xtb_singlepoint`: `xtb_checkEnvironment(m_env)`; on error, fetch the diagnostic
(`xtb_getError`) and treat the tick as failed. Independently, check finiteness of energy,
gradient, and coordinates before publishing. A failed tick publishes nothing
(`m_coordsValid = false`), emits a new `evaluationFailed(reason)` signal, and latches the
session idle-with-failure (worker keeps the session alive; ticks are consumed without
evaluating, mirroring the converged latch). The HUD surfaces the reason via the existing
`m_xtbSetupFailedReason` plumbing pattern. Recovery paths: Stop/Start, or any topology
edit, which re-establishes the session per D2. Alternative considered — auto-retry at
reduced accuracy — rejected: unbounded retries contradict the failed-setup requirement's
determinism contract.

**D5 — Write-back simplification.** The tool's timer keeps `requestTick` +
`takeLatestCoords`; the size guard stays as a safety net (after D2 it should never fire)
but no longer defines session death. The dragged-atom skip and the write lock discipline
are unchanged. A non-finite snapshot that somehow arrives is dropped with a diagnostic
(belt and braces; the engine already blocks it).

**D6 — Test strategy (no GL required).**
- Engine-level crash regression: setup a water session, start the worker,
  `setDraggedAtom(0)` with no position authored, request a tick, pump — asserts no crash
  and that coordinates still publish. This is the deterministic form of the cdb AV
  (pre-fix it faults on the null vector read).
- Re-baseline tests: engine-level session started from a molecule source; mutate the
  molecule (add atom / remove atom / change element / move atoms) between ticks; assert
  re-establishment happens exactly once per topology change, the new topology is
  evaluated, and external moves are adopted (published coords continue from the moved
  geometry, not the pre-edit geometry).
- Containment test: drive a failing evaluation (unsupported geometry → SCF failure, or
  inject non-finite results where the seam allows) and assert no publish + failure latch.
- Tool-level: existing `autooptreprotest` harness gains the write-back-after-edit
  scenarios (synthesized events, `pumpUntil`), including undo-during-xtb.

## Risks / Trade-offs

- **[Re-establishment cost on large molecules]** object creation + method load on every
  topology change could reach tens of ms (worker-side). → Bounded and edit-triggered, not
  per-tick; the FF backend already re-setups every tick, so xTB remains strictly cheaper.
- **[Read-lock contention with the render path]** the worker now takes the molecule read
  lock at 10 Hz. → Same bounded-retry/yield pattern the FF worker has used since the
  thread-confinement fix; render-path writes (stroke release) take priority and the worker
  skips its tick instead of blocking.
- **[L-BFGS reset churn during continuous strokes]** every external position change resets
  the quasi-Newton model, so heavy editing degrades convergence efficiency (not
  correctness). → Accepted; matches FF semantics (which re-setups per tick); can debounce
  later without spec change.
- **[Epsilon false-positives in external-change detection]** round-trip noise or a
  pathological geometry could spuriously reset the optimizer phase. → Epsilon (1e-6 bohr)
  is ~10 orders of magnitude above double round-trip noise and ~6 below atomic scales;
  the dragged atom is excluded from the diff.
- **[MSVC divergence]** new code touches const methods and RAII holders. → Follow the
  repo's documented MSVC rules (`mutable` mutexes, named-constructor RAII, no aggregate
  init of classes with user-provided special members); the Windows installer ctest leg
  compiles and runs the new tests.
- **[Crash evidence loss]** the cdb capture lives in a user terminal. → The race trace and
  faulting frame are recorded here; the engine-level regression test reproduces the fault
  deterministically pre-fix.

## Migration Plan

Single PR against master; no settings or data migration; behavior of the force-field
backend unchanged. Rollback = revert the commit. Windows verification rides the installer
workflow's offscreen ctest leg (`gh workflow run 168745168 --ref <branch>`), which now
includes the new engine and tool tests.

## Open Questions

None blocking. (If the Windows crash resurfaces after D1–D5 on the target machine, the
next evidence step is the same cdb capture against the new build — the recorded frame
gives a direct before/after comparison.)
