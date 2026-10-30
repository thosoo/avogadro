# Design: fix-modern-tier-consumer-parity

## Context

The modern/compat tiers replaced the legacy direct-GL paint with an
emit-into-`SceneBuilder` → chunk-store → backend pass. The audit behind this
change (session log, 2026-10-21) found five consumer families with no
modern-tier counterpart:

1. **Tool overlay paint.** `d->tool->paint(this)` runs on the modern tier with
   no active `GLPainter::begin()` bracket (`paintGL()` holds none; `end()`
   zeroes the painter's widget at the end of the previous frame's overlay
   block). Tools draw via the raw `GLWidget::painter()` — not the
   override-aware `PainterDevice` painter — so every call hits
   `isValid() == false` and silently returns, flooding the log with
   "GLPainter not active." On the sync path the tool paint also happens inside
   `emitSceneToBuilder()` *before* `backend->endFrame()`, so even a bracketed
   paint would rasterize under the molecule.
2. **Meshes.** `SceneBuilder::drawMesh/drawColorMesh` capture `MeshInstance`s,
   but nothing consumes `meshes()`: `ModernRendererBackend::pushMeshes` is an
   empty stub, the compat backend does not implement it at all, and neither
   `renderModern` nor `renderCompat` flushes the store. Orbitals are computed,
   the Surface engine is enabled, the repaint happens — the geometry is
   dropped.
3. **Unit cells.** `renderCrystal`/crystal-axes run only inside the
   legacy-tier branch of `GLWidget::render()` (glwidget.cpp ~1565–1636).
4. **Settings.** `GLWidget::setQuality` reaches only the legacy `GLPainter`;
   `SceneBuilder::m_quality` stays at its default 2, the modern backend's
   `beginFrame` ignores its quality parameter, and the snapshot tessellator
   has no quality input. The fog block (`glFog*`, glwidget.cpp ~1544) executes
   only in the legacy branch, and the modern shaders have no fog term.
5. **Emitter diagnostics.** `drawTorus`/`drawEllipsoid` discard silently;
   `drawShadedQuadrilateral` coarsens without the warning its siblings emit —
   both contrary to the existing "no silent primitive loss" clause.

Constraints that shape the design: the compat tier is frozen (bug fixes only);
`GLPainter` already has a tested stable-mesh VBO streaming path
(`GLPainterPrivate::ensureMeshVBO`, meshstream/visualparity test coverage);
the worker-adopt branch of `renderModern` already paints the tool after
`endFrame()` — only the bracket is missing there; `Mesh` draws must respect
`Mesh::lock()` (SurfaceEngine already try-locks before calling).

## Goals / Non-Goals

**Goals:**
- Restore every audited consumer on the modern tier with the smallest change
  that reuses tested machinery.
- Keep legacy-tier behavior byte-identical.
- Make the settings UI honest per resolved tier where effects are not
  implemented.
- Bring the emitter to full render-or-report compliance.

**Non-Goals:**
- Mesh rendering *inside* the instanced backend (no mesh instancing, no mesh
  in the worker snapshot path — meshes force the sync path as today).
- Mesh picking on the modern tier (legacy parity for display only).
- New fog/quality semantics for the compat tier beyond the shared flush.
- The quantum-extension freeze (separate change, needs a stack trace).
- Camera-aware placement or LOD changes.

## Decisions

### D1: Bracketed overlay pass after the backend frame — not capture
Wrap tool paint in `d->painter->begin(this)` … `end()` and move it *after*
`backend->endFrame()` on both modern branches and compat. Concretely: extract
a small helper (e.g. `paintOverlaysAfterFrame()`) called by `renderModern`
(both worker-adopt and sync paths, replacing the in-emission tool paint) and
`renderCompat`, ordering: backend frame → unit-cell pass (D3) → mesh flush
(D2) → tool paint (bracketed) → `flushCapturedText()` (existing).

*Rationale:* tools draw via the raw `widget->painter()`; routing them through
the SceneBuilder capture would require changing every tool to the
`PainterDevice` painter — a much larger, riskier diff. Immediate GL drawing
after the instanced frame is exactly what the worker-adopt branch already
does; this generalizes it and deletes the sync-path ordering wart noted in
`emitSceneToBuilder`'s comment. `emitSceneToBuilder()` stops painting the
tool entirely (engines only), which also makes the worker tessellator's
engine-only contract cleaner.
*Alternative rejected:* capture tool output in the builder (uniform, but
touches every tool, adds a capture format for halos/quads/arcs, and still
needs a bracketed flush for anything painter-specific).

### D2: Meshes flush through `GLPainter::drawMesh` after the frame — not a backend mesh pipeline
After the backend frame, iterate `d->builder.meshes()` under a painter
bracket: resolve the `Mesh*`, skip unstable ones, call
`drawColorMesh`/`drawMesh` per the instance's `colored` flag with the
captured color. `GLPainter`'s existing VBO streaming handles upload-once and
per-vertex colors; blending is enabled around the flush (meshes are in the
legacy transparent pass today). Remove `pushMeshes` from
`RendererBackend`/`ModernRendererBackend` (the compat backend never had it).

*Rationale:* the GLPainter path is already written, already tested
(meshstreamtest), already handles the CoreProfile fallback and context-loss
cases; a backend mesh pipeline would duplicate all of that for no measured
need (a handful of orbital meshes per scene). Compat gets the fix for free
via the same shared helper, which is a bug fix (parity for basics), not a
frozen-tier feature.
*Alternatives rejected:* implementing `pushMeshes` in the modern backend
(duplicates GLPainter's streaming/context handling); leaving meshes
modern-only-invisible with a UI warning (user-facing regression remains).

### D3: Unit cell as a fixed-function overlay pass on the modern tier
Extract the crystal box/axes drawing from the legacy-only branch and invoke it
from the shared overlay helper when `molecule->OBUnitCell()` is non-null.
Grounding detail that shrinks this task: `renderCrystalAxes()` (and the
TU-local `renderClippedBox` helper it calls, which draws via
`d->painter->drawBoxEdges`) is already painter-based — the only display-list
dependency is `renderCrystal(displayList)`'s `glCallList` repetition loop,
and `aCells/bCells/cCells` are permanently 1 (glwidget.cpp:197, no setter),
so that loop never repeats in practice. The modern-tier pass is therefore:
call the axes/box drawing under the helper's existing painter bracket. The
legacy branch keeps its current `renderCrystal` + display-list path
untouched (byte-identical by construction). Draw order: before the mesh
flush so translucent surfaces composite over cell edges, matching legacy
layering (cell edges are opaque lines).

*Rationale:* the geometry is a few line loops per frame — no display list or
instancing needed; the same painter primitives already verified on that path
draw it identically on any tier.
*Alternative rejected:* porting cell geometry into chunk stores (bigger
change, no visual benefit).

### D4: Quality reaches the backend geometry; fog is gated honestly
- **Quality (corrected by implementation grounding):** the modern tier has no
  per-instance tessellation to tune — `SceneBuilder::drawSphere` appends a
  position/radius/color record, and detail lives entirely in the backend's
  fixed unit geometry (`SphereGeometry`/`CylinderGeometry`, built once at the
  Medium preset's 12 rings / 24 segments; `lodParams`/`ZoomLodController` in
  lod.{h,cpp} exist but are unwired). So the mechanism is: map the slider's
  0–4 value onto the three existing `LodLevel` presets exactly as the settings
  dialog already labels it (0–1 Low, 2 Medium, 3–4 High —
  settingsdialog.cpp `qualityChanged`), and give the modern backend a
  `setDetailLevel(LodLevel)` that rebuilds its unit sphere/cylinder index
  buffers at `lodParams(level)`. Instance records are level-independent, so
  worker results stay valid across a change — no re-keying, no rebuild of the
  snapshot path. `SceneBuilder::m_quality` becomes dead and is removed.
- **Fog:** implement a shader fog term (distance-based, legacy `GL_LINEAR`
  parameters computed from `d->fogLevel`/camera as the legacy branch does)
  in the modern tier's lit shaders, fed via a per-frame uniform.
- Both keep legacy-tier code paths untouched.

*Rationale:* the preset mapping reuses the existing `LodParams` presets and
the dialog's existing Low/Medium/High labels, gives an observable
tessellation effect as the spec scenario demands, and is a bounded backend
change (two unit meshes, rebuilt at most twice per slider drag because the
mapping is many-to-one). Fog is a viewer-level depth cue; with the uniform
plumbing the modern tier can match the legacy linear fog closely, and the
alternative (disabling the slider on modern) would remove a feature the tier
can genuinely support. If the shader-fog step proves visually divergent in
the parity harness, the fallback is the gated-UI route allowed by the spec
(disable fog on modern with a visible reason) — decide at implementation by
visual comparison, the spec permits either as long as nothing silently no-ops.
*Alternatives rejected:* threading quality into the snapshot tessellator
(nothing there consumes it — instances carry no tessellation); wiring
`ZoomLodController` for real zoom-driven LOD (a feature of its own, out of
scope); gating both sliders (drops a working quality path); per-engine fog
(wrong scope).

### D5: Render-or-report compliance in the emitter
`SceneBuilder::drawTorus`/`drawEllipsoid` gain the standard one-shot
`qWarning` (pattern of `drawCone`); `drawShadedQuadrilateral` gains the
"captured as an outline" warning. No geometry changes.

### D6: Settings-dialog honesty is a fallback, not the primary path
With D4 implementing both settings on modern, the dialog stays enabled
everywhere; the per-tier gating requirement is satisfied vacuously today and
remains the escape hatch if D4's fog step is rolled back. Keep the gating
scaffolding out of this change unless the fallback triggers.

## Risks / Trade-offs

- [Tool overlay ordering changes on the sync path] → The tool previously
  rasterized *under* molecule geometry there; moving it above matches the
  worker-adopt branch and legacy layering. Parity matrix covers AutoOpt halos
  over atoms and labels over bonds.
- [Mesh flush on the GUI thread] → One-time VBO upload when a mesh becomes
  stable; per-frame cost is a `glDrawArrays` of a cached VBO — identical to
  what the legacy tier does today. Large meshes (multi-million vertices)
  accept the same one-time stall the legacy tier has; noted, not fixed here.
- [Mesh flush skips the lock discipline] → The flush must respect the
  SurfaceEngine's existing `Mesh::lock()` try-lock pattern; drawing an
  unstable mesh is skipped, mirroring `GLPainterPrivate::ensureMeshVBO`'s
  existing stability gate.
- [Shader fog diverges visually from fixed-function fog] → Visual comparison
  on the parity harness decides; fallback is honest gating (spec-permitted).
- [Quality rebuild churn] → The slider maps many-to-one onto three discrete
  presets, so a drag triggers at most two unit-geometry rebuilds; rebuild is
  skipped unless the mapped level actually changed.
- [Removing `pushMeshes` breaks out-of-tree backends] → None exist in-tree;
  the interface is internal to this repo. Compiler will find any straggler.
- [Tool overlay brackets nest with `flushCapturedText()`'s own begin/end] →
  `GLPainter::begin/end` count overflow (glpainter_p.cpp:1516–1530), so
  nesting is safe; the helper may either rely on that or inline the flush
  loop into its own single bracket. Either is fine — pick one and keep it.

## Implementation anchors

Line numbers are as of the 2026-10-21 audit; expect drift — the symbol names
are the stable reference.

| Where | What |
|---|---|
| glwidget.cpp `render()` ~1500–1660 | Tier dispatch; legacy-only branch holds fog (~1544) and `GL_LIGHT1` quality gate (~1539); unit-cell calls at 1565–1636; shared overlay block (axes + debug HUD) at ~1652–1663 already bracketed |
| glwidget.cpp `renderModern()` ~1050–1180 | Worker-adopt branch paints `d->tool->paint(this)` + `flushCapturedText()` at ~1131–1141; sync path flushes text after `backend->endFrame()` at ~1173; both get the helper call |
| glwidget.cpp `renderCompat()` 1455–1497 | `emitSceneToBuilder()` → `compatBackend` push/endFrame → `flushCapturedText()`; helper call goes after endFrame |
| glwidget.cpp `emitSceneToBuilder()` ~1230–1300 | Remove the in-emission `d->tool->paint(this)` block (~1608–1617) and its ordering comment; engines-only emission remains |
| glwidget.cpp `renderCrystal`/`renderCrystalAxes` 1673–1735, `renderClippedBox` anon-namespace ~1737–1840 | Cell drawing is painter-based (`drawBoxEdges` at 1829) — needs only relocation into the helper's bracket, not a rewrite; `aCells/bCells/cCells` are fixed at 1 (glwidget.cpp:197, no setter), so the `glCallList` repetition loop never repeats in practice |
| glpainter_p.cpp `GLPainter::begin/end` 1516–1530, `isValid()` 218–224, `drawText` 1351–1385, mesh VBO path 260–340 + `drawMesh` ~1290 | The bracket target, the "GLPainter not active." warning source, and the mesh streaming machinery D2 reuses |
| rendering/scenebuilder.cpp `drawMesh`/`drawColorMesh` 153–181, `drawTorus` 311, `drawEllipsoid` 316, `drawShadedQuadrilateral` 247, `drawCone` warning pattern 183–193 | Capture store the flush consumes; diagnostics targets |
| rendering/records.h `MeshInstance` 71–76 | `const Mesh *mesh`, `int mode` (0 filled/1 lines/2 points), `color[4]` rgba 0–255, `bool colored` |
| rendering/chunkstore.h `size()` 68, `at()` 81 | Iteration API for the flush loop |
| rendering/modernbackend.cpp `pushMeshes` stub 310–313; unit geometry builds 627 (`SphereGeometry`), 702 (`CylinderGeometry`); uniform updates 773–776 | Stub to delete; geometry builds to extract into a rebuildable helper for `setDetailLevel`; uniform-update site for fog |
| rendering/rendererbackend.h `pushMeshes` pure virtual 69 | Interface entry to remove |
| rendering/lod.{h,cpp} `LodParams`/`lodParams` (Medium = 12 rings / 24 segments) | Preset source for `setDetailLevel` |
| rendering/shaders.h shared preamble 46–68, vertex 119–131 | Fog uniforms + `vViewPos` varying insertion points |
| avogadro/src/settingsdialog.cpp `qualityChanged` 75–91, `fogChanged` 93–114 | Existing Low/Medium/High and None…Lots labels the quality mapping must match; fallback-gating touchpoint |
| libavogadro/tests/hidpiviewporttest.cpp `makeHiDpiWidget` ~123 | Offscreen real-GLWidget harness pattern for the new test |
| libavogadro/tests/glselectoverflowtest.cpp:312 | `qInstallMessageHandler` warning-count pattern |
| libavogadro/tests/scenebuildertest.cpp:525–531 | `textItems()` store-assertion pattern |
| libavogadro/tests/CMakeLists.txt tests list + `MATCHES "^(parity\|compatibility\|…)` env block | New test registration; add its name to the `LIBGL_ALWAYS_SOFTWARE=1` regex (it drives a real GLWidget) |

## Migration Plan

Single self-contained rendering change; no data or settings migration.
Rollback = revert the commit. Verification: existing suite (`ctest`), new
parity tests (tool text, mesh flush, unit cell, quality reach-through), plus
the manual AutoOpt + ORCA matrix from the session log.

## Open Questions

- Whether shader-fog visual parity passes the harness (decided at
  implementation; both outcomes spec-compliant).
- Exact `MeshInstance` field needs for the flush (mode/`colored` are already
  captured; color application path is shared with the legacy painter).
