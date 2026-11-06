## Why

Deletion today requires the Draw tool and right-click, one atom at a time, with no
way to remove a group of atoms in a single gesture. The project is also preparing
touch input, and right-click has no touch analog — a dedicated eraser gives both
desktop users (group deletion that does not exist at all today) and future touch
users (encircling a group is the most finger-friendly erase gesture) a proper
deletion interaction.

## What Changes

- Add a new **Erase tool** to the tool palette (eraser icon, no keyboard shortcut
  in this change; F7 is reserved for a future shortcut).
- The tool deletes by **lasso**: press anywhere, drag a freehand loop, release —
  every atom enclosed by the loop is deleted; bonds follow their atoms.
- A press-and-release without significant path (a tap) is a **no-op**: deletion
  never happens on a single click, so accidental grazes erase nothing.
- One lasso = **one undo step** (an `UndoSequence` of existing
  `DeleteAtomDrawCommand`s); nothing is mutated before release.
- Managed-hydrogen policy matches the Draw tool's right-click delete: with
  adjust-hydrogens enabled, managed hydrogens inside the loop are skipped; heavy
  atoms inside the loop are deleted and their hydrogens churn via the commands'
  existing `adjustHydrogens` flag.
- The in-progress loop renders as a screen-space overlay via `Tool::paint()`,
  on every rendering tier, as required by the existing tool-overlay contract.
- Existing gestures are unchanged: Draw tool right-click delete and all other
  tools behave exactly as before.

## Capabilities

### New Capabilities
- `erase-tool-interaction`: the Erase tool's lasso-deletion contract — tap
  no-op, containment semantics, undo grouping, managed-hydrogen parity,
  lock discipline during optimization, interrupted-lasso cleanup, and
  overlay rendering on all tiers.

### Modified Capabilities
<!-- none: molecule-sketching's right-click delete and all other gestures are
     explicitly preserved; the erase tool is additive. -->

## Impact

- **New files**: `libavogadro/src/tools/erasetool.{h,cpp}`, an eraser icon and
  `.qrc`, one entry in `libavogadro/src/tools/CMakeLists.txt`.
- **No changes** to existing tools, `Tool` API, camera, rendering tiers, or
  undo commands — the tool reuses `DeleteAtomDrawCommand` /
  `DeleteBondDrawCommand` and `UndoSequence` as-is.
- **Molecule lock discipline**: erase assembly takes the molecule write lock
  (`tryLock`) like the Draw tool; during a running AutoOpt session contention
  aborts the gesture cleanly with no residue (same pattern as the Draw tool's
  release path).
- **Testing**: headless `QTest` mouse-drag sequences asserting containment
  deletions, single-undo restoration, tap no-op, and interrupted-lasso cleanup;
  runs under the offscreen QPA platform like the existing tool tests.
