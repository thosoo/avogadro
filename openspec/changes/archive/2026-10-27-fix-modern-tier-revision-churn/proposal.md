## Why

Under sustained molecule-revision churn the modern tier's worker path stops adopting
rebuilt scene content. The adoption gate requires an exact revision match
(`readyRevision == revision`) and `builderResultReady` discards anything older than the
current revision — while AutoOpt ticks bump the revision ~20×/s (each tick's coordinate
write-back bumps unconditionally, converged or not) and atom drags bump it per mouse move.
Builds that lose this race are discarded and resubmitted in a loop, so the display falls
permanently behind: a clicked bond order never appears while AutoOpt runs (user-confirmed),
and a dragged atom trails the cursor by whole rebuild cycles, jumping forward only when
motion stops (user-confirmed: "considerable input lag"). A second, independent lag source
sits in `AutoOptTool::finished()`: each tick writes the force field's coordinates back over
*every* atom — including the dragged one, whose force-field coordinate is up to one tick
stale — then re-applies the drag delta from that stale anchor, so the atom is teleported
backward at tick rate while being pulled.

## What Changes

- Worker-path result adoption changes from exact-revision matching to **forward-progress
  adoption**: a finished build is adopted whenever it is newer than what is currently
  presented, regardless of how far the live molecule has moved on; the newest revision is
  resubmitted immediately. Presentation latency becomes bounded by one build under any
  churn rate; the starvation window closes.
- `builderpooltest`'s contract is updated to the new policy (the exact-revision discard
  expectation is the defect; a previous coalescing attempt was reverted because it broke
  this test — commit 5ec623fff).
- `AutoOptTool::finished()` skips the coordinate write-back for the atom under an active
  drag (it is force-field-fixed and re-anchored by the drag path; writing it back from a
  stale snapshot regresses it). All other atoms' write-back is unchanged.
- Bond pick hit names now carry the bond's primitive index (`BondInstance::pickId`, read
  by the cylinder pick shader) instead of the raw half-segment instance index, so a
  click on any segment of a bond resolves to that bond (order cycling, bond deletion).
  Found by the live task 3.1 verification; without it most bond clicks no-op or mutate
  the wrong bond on the modern tier.
- Extras (task-level): tool-overlay draw-order consistency between the emission and
  worker-adopt paths (halos paint before vs after transparent geometry today).

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `gpu-rendering-pipeline`: the "No blank frames during scene rebuild" requirement gains a
  sustained-churn clause — adoption shall make forward progress under any revision-churn
  rate and presentation latency shall be bounded by one build (exact-revision matching,
  which permits unbounded starvation, is the defect).
- `auto-optimize-interaction`: new requirement — the per-tick coordinate write-back shall
  not regress an atom under an active drag.

## Impact

- `libavogadro/src/glwidget.cpp` — `builderResultReady()`/`renderModern()` adoption rule
  (new `presentedRevision` tracking; discard only results older than presented).
- `libavogadro/tests/builderpooltest.cpp` — contract update (this test's expectations are
  the codified defect; the earlier revert 5ec623fff is superseded by changing the contract
  here rather than re-attempting submission coalescing).
- `libavogadro/src/tools/autoopttool.cpp` — `finished()` drag-atom skip.
- No shader, geometry, or upload changes; the sync (non-worker) path is untouched.
