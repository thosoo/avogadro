## Why

With the xTB optimization backends shipped on Windows, the two interaction paths the AutoOpt
tool exists for — dragging atoms and sketching while a session runs — are broken or crash the
application. A cdb capture on the Windows install shows the crash deterministically:
access violation at `Avogadro::XtbEngine::evaluateOnce+0x18b` (`mov rax,[rbx]` with `rbx=0`),
a read through an empty `std::vector`'s null data pointer, reached by pressing an atom during a
running session before the first pointer move. Independently, the xTB session is a closed
snapshot that never re-reads the live molecule, so any structural edit permanently freezes the
write-back, undo appears to snap geometry back, and element changes never reach the optimizer.

## What Changes

- **Fix the drag-marker crash**: enforce the engine invariant "dragged index set ⇒ drag
  position authored" at every write, and make the worker's pin step skip (never dereference)
  when no pointer position exists yet. Press-then-tick and bare clicks must be crash-free.
- **Make xTB sessions integrate live-molecule edits**: the worker re-baselines per tick from
  the live molecule under the existing bounded-retry read-lock pattern (as the force-field
  worker already does) — a topology change triggers a worker-side re-setup, an external
  position change resets the optimizer phase and clears the convergence latch. This dissolves
  the permanent write-back freeze, the 10 Hz revert-fight during strokes, the undo snap-back,
  and stale-element sessions, and lets the molecule (not a side channel) be the drag authority.
- **Contain failed evaluations**: check the xTB environment after every singlepoint; a failed
  or non-finite tick publishes nothing and never reaches the molecule; the session latches
  deterministically with a HUD diagnostic instead of feeding garbage into the render pipeline.
- **Regression coverage**: extend `autooptreprotest` / `xtbenginetest` with the press-before-move
  race, draw/erase/undo-during-xtb no-freeze scenarios, and failed-evaluation containment; the
  suite runs in the Windows installer CI offscreen leg.

Optimizer algorithm selection (L-BFGS vs ANCOPT) is explicitly out of scope — deferred to a
later change after xTB sessions are reliable.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities

- `auto-optimize-interaction`: add the drag-marker invariant scenario (press before first
  pointer move is crash-free) and non-finite-write-back containment to the coordinate
  write-back requirement; extend the concurrent-editing requirement with xTB scenarios
  (draw/erase/undo during a running xTB session integrate without freezing); add an xTB
  live-molecule re-baseline requirement; add a failed-evaluation containment requirement.

## Impact

- `libavogadro/src/xtb/xtbengine.{h,cpp}` — drag-marker invariant, per-tick live-molecule
  re-baseline (topology fingerprint, worker-side re-setup, position adoption, latch handling),
  post-singlepoint environment check, finiteness guards on publish.
- `libavogadro/src/tools/autoopttool.{h,cpp}` — write-back path simplification (drop the
  stale-size permanent skip), drag plumbing simplification, HUD failure diagnostic.
- Tests: `libavogadro/tests/autooptreprotest.cpp`, `libavogadro/tests/xtbenginetest.cpp`.
- No changes to the xtb C API shim (`xtbapi.h`), the bundled runtime, the installer, or
  OpenBabel. Force-field backend behavior is untouched.
