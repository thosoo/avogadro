## Why

Two classes of on-screen text survive only on the legacy tier, silently. (1)
`SceneBuilder::drawText` — the Painter the modern tier routes every engine through — is a
stub (unnamed parameters, no-op body), so the LabelEngine's atom labels simply vanish on
the modern tier with no warning. (2) `GLWidget::render()` dispatches to
`renderModern()`/`renderCompat()` and returns *before* `renderAxesOverlay()` and
`renderTextOverlay()` execute, so the "Render Axes" feature, the debug HUD (FPS, atom
counts — including the modern-tier draw-call counters, which therefore can never display),
and extension text overlays are dead on the modern and compat tiers. The AutoOpt HUD only
survives because `tool->paint()` is explicitly invoked in the modern paths. Both defects
violate the tier contract: switching tiers must not silently change which primitives and
features exist.

## What Changes

- `SceneBuilder` **captures** text items (screen-space and world-space variants, with the
  color current at capture time) instead of discarding them; `renderModern()`/`renderCompat()`
  flush the captured text through the widget's real painter after the 3D frame, projecting
  world-space text with the live camera — identical to legacy output.
- `renderAxesOverlay()` and `renderTextOverlay()` move out of the legacy-only branch and
  run after the tier dispatch on **all** tiers (with the painter begin/end coordination the
  legacy path already performs), restoring Render Axes, the debug HUD, and extension text
  overlays on modern and compat.
- Painter-contract guard: a Painter implementation that accepts a primitive and silently
  discards it is a defect — the remaining no-op stubs in `SceneBuilder` are audited and
  either implemented, loudly skipped with a one-time warning, or documented as unsupported
  for the engines that emit them.
- Extras (task-level, same parity surface): verify the modern `drawMultiCylinder` spread
  matches legacy `GLPainter::drawMultiCylinder` for multi-bond orders (suspected ±shift vs
  ±shift/2 divergence); unify tool-overlay draw ordering between the emission and
  worker-adopt paths (halos paint before vs after transparent geometry today).

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `gpu-rendering-pipeline`: the "Visual parity for ported engines" requirement is extended
  — text primitives emitted by engines render on every tier; tier-exclusive features (unit
  cell axes overlay, debug/text overlays) function on the modern and compat tiers; and a
  Painter implementation shall not accept a primitive and discard it silently.

## Impact

- `libavogadro/src/rendering/scenebuilder.{h,cpp}` — text capture store (replacing the
  stub), audited remaining stubs.
- `libavogadro/src/glwidget.cpp` — post-frame text flush on modern/compat;
  `renderAxesOverlay`/`renderTextOverlay` moved to all-tier execution.
- No backend/shader changes (text flushes through the existing GLPainter, as tool overlays
  already do); no legacy-tier behavior change (engines already draw through the real
  painter there — the flush must be tier-gated so legacy does not double-draw).
- Overlaps with `fix-modern-tier-revision-churn` task 4.1 (overlay ordering) — supersede
  that task when this lands, or land this first.
