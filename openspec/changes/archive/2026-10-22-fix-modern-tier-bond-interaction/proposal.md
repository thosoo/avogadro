## Why

Changing a bond's order by clicking it regressed on the modern rendering tier, in two
independent, experimentally confirmed ways. (1) *Hitbox*: the modern pick pass renders the
exact visible frame geometry with no slop, while the legacy pick scene deliberately
fattened bonds (`radius + 0.05`, full length) against atoms (`+0.03`) — with a 9×9 px
click box and depth-sorted hits, any click near an atom's silhouette now picks the atom,
and the draw tool's element-alchemy branch silently no-ops on matching elements. The
clickable area for order-cycling collapsed to roughly the middle of each bond. (2)
*Invisible mutations*: `Bond::setOrder` is a bare field write (no signal, no revision
bump), `Molecule::update()` never bumps the revision, and `Molecule::updateAtom()` (unlike
`updateBond()`) doesn't either — so with "adjust hydrogens" off, a clicked double bond (or
an element recolor) changes the data and renders **nothing**, permanently.

## What Changes

- The modern pick pass restores the legacy pick scene's affordance: bond pick geometry is
  inflated by the legacy slop (~+0.05, matching `BSDYEngine::renderPick`) so bond click
  targets are at least as forgiving as the legacy tier's; atom pick radii get the
  corresponding legacy slop.
- `Molecule::update()` bumps the molecule revision before emitting, so the documented
  "content changed" notification reaches the modern tier's revision-keyed render cache.
- `Molecule::updateAtom()` bumps the revision (symmetry with `updateBond()`), so element
  alchemy re-renders immediately.
- Restores (via the above): the drag-over-existing-bond order preview, invisible since the
  modern tier cached instance stores.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `gpu-rendering-pipeline`: the "Visual parity for ported engines" requirement is
  sharpened with (a) pick-affordance parity — a primitive's click target on the modern
  tier shall be no smaller than on the legacy pick scene — and (b) mutation visibility —
  any molecule mutation the legacy tier re-renders implicitly (bond order, element) shall
  reach the modern tier's render cache within one frame.

## Impact

- `libavogadro/src/rendering/modernbackend.{h,cpp}` — pick pass radius slop (shader
  uniform or pick-upload inflation; pick programs already separate from lit programs).
- `libavogadro/src/molecule.cpp` — two revision bumps (`update()`, `updateAtom()`).
- No workflow/build changes; legacy/compat tiers untouched (they already re-render per
  frame); no interaction with the sibling changes' files except glwidget.cpp adoption
  logic (`fix-modern-tier-revision-churn`), which is additive.
