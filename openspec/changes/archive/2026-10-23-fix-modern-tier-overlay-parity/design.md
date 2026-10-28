## Context

The modern tier routes every engine through `SceneBuilder` (a Painter implementation that
appends instance records to chunk stores). Two text paths were lost in that port:

1. **Engine-emitted text.** `SceneBuilder::drawText` (3 overloads,
   `scenebuilder.cpp:290–295`) has an unnamed-parameter no-op body — accepted, discarded.
   The LabelEngine draws per-atom labels with `pd->painter()->drawText(worldPos, ...)`
   after `setColor`; on the modern tier those calls vanish.
2. **Widget overlays.** `GLWidget::render()` runs `renderAxesOverlay()` and
   `renderTextOverlay()` only in the legacy branch below the tier dispatch, so both tiers
   that need them most never execute them. `renderTextOverlay` is also the only place the
   modern-tier debug counters (`backendDrawCalls`/`backendFrames`) would ever display.

Precedent for the fix shape already exists: the active tool paints through the widget's
*real* painter (`d->tool->paint(this)`) in both modern branches, and `AutoOptTool`'s HUD
halos draw that way today. Text is low-volume overlay content — it does not need
instancing or shader work; it needs capture-and-flush.

## Goals / Non-Goals

**Goals:**
- Engine text renders identically on all three tiers.
- Axes + debug/text overlays function on modern and compat.
- A contract (and diagnostic) against silent primitive loss.
- Legacy tier byte-for-byte unchanged.

**Non-Goals:**
- A texture-atlas/text-in-shader renderer (no instanced text; flush through the existing
  GLPainter keeps this change small).
- Porting the remaining non-text stubs (cones, sectors, shaded triangles) to full
  geometry — they are *approximated* today (lines), not silently dropped; documenting them
  is enough for now.
- Changing `drawMultiCylinder` visual output unless verification shows a real divergence
  with legacy (task, not decision).

## Decisions

1. **Capture-then-flush.** `SceneBuilder` records text items instead of discarding them:
   `{ QString text; QColor color; bool worldSpace; Vector3d worldPos; int x, y; }`,
   cleared in `begin()/clearStores()` alongside the other stores. After
   `backend->endFrame()`, `renderModern()` (and `renderCompat()`) flush the items through
   `d->painter` exactly as the legacy branch draws text: screen-space items at their
   coordinates; world-space items projected with the live camera (the GL matrices
   `paintGL()` set are still current, matching legacy `GLPainter::drawText(Vector3d)`
   behavior).
2. **Tier-gate the flush.** On the legacy tier, engines draw through the real painter
   directly — the capture list stays empty, but `renderModern`'s flush is skipped anyway
   so no path can double-draw.
3. **Overlays move above the dispatch.** `renderAxesOverlay()` and `renderTextOverlay()`
   execute after the tier-specific 3D pass for all tiers, wrapped in the same
   `d->painter->begin(this)` / `end()` coordination the legacy branch performs (the
   legacy branch keeps its existing inline call; guard against double-draw by moving, not
   copying). The debug HUD thereby gains the modern-tier counters it references.
4. **Painter contract made loud.** The remaining no-op stubs in `SceneBuilder` are
   audited: text becomes implemented (this change); approximated primitives (cone/sector/
   triangle → line approximations) get a one-time `qWarning` category note and a comment
   documenting the approximation, so "accepted but coarsened" is distinguishable from
   "accepted and gone".
5. **Extras folded in as verification/consistency tasks:** multi-cylinder spread parity
   check against `GLPainter::drawMultiCylinder` (align constants if they diverge);
   tool-overlay ordering unification (supersedes
   `fix-modern-tier-revision-churn` task 4.1 — text/overlay flushes land in one place
   after the 3D pass).

## Risks / Trade-offs

- [Text flush cost per frame] → LabelEngine-scale text is tens of items; the GLPainter
  text path already serves the legacy tier at this volume. Negligible.
- [Painter begin/end coordination on modern paths] → The flush runs outside
  `backend->endFrame()`; the GL matrices and context are current (same conditions the
  tool overlay paints under today). The begin/end wrap matches legacy text assumptions.
- [Double-draw regressions] → Gated: flush runs only on modern/compat; overlay calls are
  *moved* from the legacy branch, not duplicated. Legacy screenshot comparison is a task.

## Verification (maps to spec scenarios)

1. LabelEngine labels visible on modern tier; follow atoms during rotation; same
   content/color as `AVOGADRO_RENDERER=legacy`.
2. Render Axes on modern/compat — axes overlay appears; toggle works.
3. Debug HUD on modern — renders, and the modern-tier counters finally display.
4. Extension text overlay visible on modern.
5. Legacy screenshot parity: axes/HUD/labels on legacy unchanged (no double-draw).
6. Deliberately call a coarsened primitive (axes engine cone) on modern — one-time
   diagnostic logged.
7. `ctest` full suite green; new SceneBuilder text-capture unit test.
