# Proposal: fix-modern-tier-consumer-parity

## Why

The modern render tier silently drops several consumers of the legacy direct-GL
render path: every active tool's overlay paint (including the AutoOpt energy
readout and fixed/ignored halos), all mesh geometry (orbital and ESP surfaces —
ORCA results are computed but never displayed), unit-cell rendering, the
quality slider, fog, the quality-dependent second light, and Python extension
overlays. On the default tier these features are simply gone, and two emitter
entry points (`drawTorus`/`drawEllipsoid`) discard primitives without the
one-time diagnostic the existing spec requires.

## What Changes

- **Tool overlays render again on modern and compat tiers.** The tool paint
  call is moved out of the scene-emission pass and runs after the backend's
  frame, wrapped in a proper `GLPainter::begin()/end()` bracket on both
  modern-tier branches and the compat path. This restores AutoOpt's
  energy/fixed/ignored HUD and per-atom constraint halos, bond-centric length
  and angle labels, measure-tool labels, align-tool labels, and xtb-tool
  overlays. As a side effect it fixes the sync-path ordering wart where the
  tool rasterized *under* the molecule geometry.
- **Captured meshes are flushed through the real painter.** `SceneBuilder`
  keeps capturing `MeshInstance`s; after the instanced pass, modern (and
  compat, via the shared flush) draw them through `GLPainter::drawMesh`/
  `drawColorMesh`, reusing its existing stable-mesh VBO streaming instead of
  building a mesh pipeline inside `ModernRendererBackend`. The dead
  `pushMeshes` stub is removed from the backend interface. Transparency
  (meshes default to α = 0.75) is handled by drawing them after the opaque
  instanced geometry with blending enabled, matching legacy layering.
- **Unit cells render on the modern tier.** `renderCrystal`/crystal-axes
  drawing is extracted from the legacy-only branch and executed for the modern
  tier as an overlay pass (fixed-function box edges + cell axes, drawn under
  the shared painter bracket).
- **Render settings become effective or honestly gated.** Either the quality
  slider reaches the modern tier's tessellation (SceneBuilder/tessellator) and
  fog gets a shader term, or — for settings that stay legacy-only — the
  settings dialog disables them per resolved renderer tier with a visible
  reason. The decision between "implement" and "gate" per setting is made in
  design.md; the spec requires only that the UI no longer promises effects
  that cannot happen.
- **No silent primitive loss.** `SceneBuilder::drawTorus`/`drawEllipsoid`
  report their omission once (same pattern as `drawCone`), and
  `drawShadedQuadrilateral` gains the standard "accepted but coarsened"
  warning, bringing the emitter into compliance with the existing
  "no silent primitive loss" clause.
- **Out of scope:** the quantum/orbital extension freeze (queue-mutex wedge,
  GUI-thread `waitForFinished`, `MeshGenerator::run` unlock-without-hold UB) —
  tier-independent, needs a stack-trace-confirmed diagnosis, separate change.
  Compat-tier-only additions beyond the shared mesh flush (own fog, own quality
  path) stay out per the frozen-low-tiers requirement.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `gpu-rendering-pipeline`: adds requirements that tool overlay paint renders
  on every tier, captured meshes render on the modern tier, unit cells render
  on the modern tier, and render settings (quality, fog) either take effect on
  the resolved tier or are visibly disabled with a reason — closing the gap
  between the settings UI's promises and the modern tier's behavior.

## Impact

- **Modified:** `libavogadro/src/glwidget.cpp` (paint-flow restructure: shared
  overlay helper for mesh flush + bracketed tool paint on modern/compat; unit
  cell extraction; quality reach-through), `libavogadro/src/rendering/scenebuilder.{h,cpp}`
  (diagnostics for dropped/coarsened primitives; expose mesh store consumer
  contract), `libavogadro/src/rendering/modernbackend.{h,cpp}` +
  `rendererbackend.h` (remove `pushMeshes`), `avogadro/src/settingsdialog.cpp`
  and `avogadro/src/mainwindow.cpp` (per-tier setting gating).
- **Tests:** new headless parity cases alongside `meshstream`/`visualparity`
  (tool-overlay text renders on modern; a stable mesh drawn via the flush path
  produces the expected GL work; unit-cell pass executes; quality value
  reaches the tessellation path).
- **No changes:** quantum/orbital extensions, engines, shaders for existing
  instanced geometry, picking paths, OpenBabel pin.
