# Proposal: DrawTool click-to-extend (ChemDraw-style)

## Why

With the DrawTool active, clicking an atom whose element already equals the
selected element (carbon selected, click a carbon) is a silent no-op: the
"alchemy" branch in `DrawTool::mousePressEvent` only mutates the atom when the
elements differ, so the release path assembles nothing. In 2D editors this
exact gesture is the highest-frequency sketching action — click an atom, grow
the molecule. Avogadro only offers it through a precise drag.

## What Changes

- **Click-to-extend gesture.** A click (press → release with no significant
  movement) on an atom whose element equals the selected element now extends
  the molecule: a new atom of the selected element is created and bonded to
  the clicked atom, honoring the tool's selected bond order and the existing
  adjust-hydrogens bookkeeping.
- **Placement policy (new, pure geometry).** The new atom is placed at a
  chemically plausible position: angle from the clicked atom's bond graph
  (sp → 180°, sp2 → 120°, sp3 → 109.47°), bond length from the covalent
  radii sum, neighbor geometry computed over heavy atoms only, staggered
  where the neighbor's substituents determine a reference. Deterministic and
  finite — never random, never non-finite (unlike `OBBuilder::GetNewBondVector`
  on this codebase's explicit-hydrogen molecules, see design.md).
- **Generalized rule.** The same-element ⇒ extend rule applies to every
  element, not just carbon. Clicking an atom of a *different* element keeps
  the existing alchemy behavior (change element) unchanged.
- **Gesture documentation.** The tool tooltip and any user-facing gesture
  documentation are updated to describe click, drag, and right-click
  behavior — currently the tooltip advertises only "Click and Drag".
- **Stroke-state hygiene (pre-existing bugs).** While touching the release
  path, three latent DrawTool defects are fixed: unguarded `m_forceField`
  dereferences (crash when no force field is loaded), early returns that
  skip stroke cleanup (dangling state wedges the next gesture), and the
  lock-failure exit that leaves stale state behind under a running
  optimization.
- All other gestures unchanged: drag from an atom, drag onto an existing
  atom, click empty space (new atom), click a bond (cycle order),
  right-click (delete). Bond orders ≥ 3 (including metal–metal quadruple
  bonds) remain fully supported.

## Capabilities

### New Capabilities

- `molecule-sketching`: interaction contract of the DrawTool's click and drag
  gestures — click-to-extend semantics (same element) vs. alchemy (different
  element), placement plausibility, undo/redo parity with drag-created bonds,
  coexistence with a running AutoOpt session, and documentation matching
  actual gestures.

### Modified Capabilities

- (none — `auto-optimize-interaction`'s concurrent-editing requirements already
  govern tool interplay; this change adds a new gesture that must comply with
  them, verified as a scenario inside the new capability)

## Impact

- **New files:** a placement helper (header + source, pure vector math, no GL
  or OB molecule graph) and its unit test; exact location in design.md.
- **Modified:** `libavogadro/src/tools/drawtool.{h,cpp}` — movement threshold
  (the existing `m_movedSinceButtonPressed` member is dead: set false, never
  true), a same-element candidate flag at press, release-path synthesis of the
  drag end-state so the existing UndoSequence/adjust-hydrogens machinery
  assembles the command; tooltip string.
- **No changes:** rendering tiers/backends, shaders, the OpenBabel pin, camera
  or picking paths. OpenBabel usage is limited to the stateless
  `OBElements::GetCovalentRad()`.
