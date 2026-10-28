## Context

The modern tier replaced the legacy pick scene and the legacy implicit re-render with (a)
a pick pass over exact frame geometry and (b) a revision-keyed instance-store cache. Both
are correct designs that regressed two behaviors the legacy tier provided for free:

- `BSDYEngine::renderPick` drew bonds with `m_bondRadius + 0.05` across the full bond
  extent and atoms with `+0.03` (heavy) / `−0.06` (hydrogen) — deliberately fattened click
  targets. The modern pick (`ModernRendererBackend::pick`) renders the visible frame's
  instance data verbatim. `DrawTool` reads only `m_hits[0]` (depth-sorted via
  `GLHit::operator<`, 9×9 px `SEL_BOX`), so near-atom clicks now resolve to the atom and
  the element-alchemy branch silently no-ops when the selected element matches.
- Legacy re-rendered from live molecule data every frame, so any mutation was visible
  immediately. The modern tier rebuilds only when `Molecule::revision` changes or engines
  report dirty. `Bond::setOrder` is a bare field write; `Molecule::update()` only emits;
  `Molecule::updateAtom()` sets flags but never bumps `d->revision` (its sibling
  `updateBond()` does). Result: with adjust-hydrogens off, order clicks and element
  recolors never re-render; the drag-over-bond preview never shows.

Experiment matrix (user-confirmed): middle-of-bond clicks work with defaults;
near-atom-end clicks do nothing (pick layer); hydrogens-off clicks change nothing visually
(notification layer); during AutoOpt nothing appears (starvation — out of scope here, see
`fix-modern-tier-revision-churn`).

## Goals / Non-Goals

**Goals:**
- Bond click targets on the modern tier at least as forgiving as legacy's.
- Any legacy-visible mutation reaches the modern render cache within one frame.

**Non-Goals:**
- The adoption-starvation fix and the AutoOpt write-war (sibling change
  `fix-modern-tier-revision-churn`).
- Rubber-band pick changes (band semantics unaffected by radius slop).
- Text/axes overlay stubs (separate parity audit).
- Making `Bond::setOrder` emit `updated()` — the `Molecule::update()` revision bump covers
  it with less signal churn (every `setOrder` call site, including the temporary drag
  previews, flows through a `molecule->update()` at the completion point).

## Decisions

1. **Pick slop in the pick pass, not the instance data.** The pick programs
   (`kSpherePickVertexShader` / `kCylinderPickVertexShader`) are already separate from the
   lit programs; add a `uniform float uPickRadiusSlop` added to `aInstRadius` in both pick
   vertex shaders, set per pass (bonds: `+0.05`; spheres: `+0.03`), mirroring
   `renderPick`'s constants. This keeps the visible frame byte-identical and avoids a
   second pick-instance upload. Bond extent is already full-length in aggregate (bonds are
   emitted as begin→mid + mid→end pairs), so no geometry change is needed for extent.
   *Alternative rejected:* uploading separate inflated pick instances — doubles upload
   traffic on rebuild frames for the same effect.
2. **Revision notification at the molecule level.** `Molecule::update()` does
   `d->revision++` before `emit updated()`; `Molecule::updateAtom()` adds `d->revision++`
   (symmetry with `updateBond()`). Rationale: `update()` is the documented "content
   changed" notifier that every command's redo/undo already calls; bumping there covers
   bond order, element alchemy, and every future signal-less mutation routed through it,
   with two lines. The extra rebuild submissions from `update()`'s liberal call sites are
   deduplicated by the worker path's `submittedRevision` gate and by the sync path's
   per-frame `sceneChanged` check.
3. **Constants stay in one place.** Reference the legacy pick constants (`0.05`/`0.03`)
   with a comment pointing at `BSDYEngine::renderPick` so the two tiers drift together, or
   hoist them to `global.h` next to `SEL_BOND_EXTRA_RADIUS` if review prefers.

## Risks / Trade-offs

- [Fatter bond pick targets can shadow atom clicks near silhouettes] → This restores the
  legacy balance deliberately (legacy fattened bonds *more* than atoms). Row-2/row-1
  matrix rows verify both directions still behave.
- [`update()` revision bumps increase rebuild frequency] → Bounded by existing
  deduplication; worst case is one rebuild per frame during continuous strokes, which the
  pipeline already sustains (AutoOpt ticks).
- [Shared glwidget.cpp region with `fix-modern-tier-revision-churn`] → This change touches
  `modernbackend.*` + `molecule.cpp`; the sibling touches adoption logic and
  `autoopttool.cpp`. No overlap; either order lands.

## Verification (maps to spec scenarios)

Manual on the built app (defaults: adjust-hydrogens ON, AutoOpt OFF unless stated):

1. Row 1/2: click the exact middle and the near-atom end of a long bond — both cycle the
   order and the double bond renders (near-atom case is the pick-slop fix).
2. Row 3: adjust-hydrogens OFF, click a bond — double bond renders within one frame (the
   revision-bump fix; pre-fix it stayed single forever).
3. Element recolor: click an atom with a different element selected — color/radius update
   immediately.
4. Drag-over-bond: stroke across an existing bond — preview order visible during the
   drag, reverts at release.
5. `ctest --test-dir build --output-on-failure` green; no new warnings from
   `modernbackend.cpp` / `molecule.cpp`.
