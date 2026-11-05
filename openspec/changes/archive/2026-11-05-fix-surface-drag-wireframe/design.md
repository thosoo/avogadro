# Design: Surface wireframe during camera drags

## Context

The modern/compat mesh flush hides surfaces during camera drags — an early
return added by `fix-surface-visualization-performance` (2026-11-01, design
D10 / task 4.3):

```
GLWidget::mouseMoveEvent (glwidget.cpp:2947)
    buttons held → m_cameraInteracting = true; timer.start(250)
GLWidget::flushCapturedMeshes (glwidget.cpp:1641)
    if (m_cameraInteracting) return;          ← surface vanishes
QTimer timeout (glwidget.cpp:576–583)
    m_cameraInteracting = false; update()     ← surface returns ~250 ms late
```

Two facts invalidate D10's cost model, both discovered in the same change that
introduced it:

1. **The VBO cache removed the guarded cost.** `GLPainterPrivate::ensureMeshVBO`
   (glpainter_p.cpp:298) caches per `(mesh, revision, stable, colored)`; a
   stable mesh during a drag is a pure cache hit — zero upload. The skip's
   stated rationale ("avoids re-uploading large surfaces every frame") no
   longer describes anything that happens.
2. **Idle frames already draw the mesh every frame.** When the camera is
   still, `flushCapturedMeshes` draws the mesh on every presented frame from
   cached VBOs. A drag adds no marginal mesh-draw cost — only the skip's
   user-facing cost (a blinking surface) was ever new.

The behavior the user actually remembers comes from the frozen legacy tier:
`SurfaceEngine::renderQuick` (surfaceengine.cpp:178) forces
`drawMesh(mesh, 1)` during quick renders, and `GLPainter::drawMesh`
(glpainter_p.cpp:1342) maps mode 1 to `glPolygonMode(GL_FRONT, GL_LINE)` with
lighting off — a wireframe net in the surface's color, solid again at rest.
The canonical spec's "render at interactive frame rates or render simplified"
has always described exactly this; hiding satisfies neither branch.

## Goals / Non-Goals

**Goals:**
- Surfaces remain visible during every frame of a camera drag, rendered as a
  wireframe net (legacy `renderQuick` parity) on the modern and compat tiers.
- Full-quality (user-selected mode) rendering returns when the motion ends,
  promptly on release.
- Keep the correctness guards (stability, mesh read lock) untouched.

**Non-Goals:**
- True mesh LOD/decimation (only relevant if wireframe drags measurably stall
  on multi-million-vertex surfaces; revisit with a benchmark if ever).
- Any change to the legacy tier (its `renderQuick` net is the reference
  behavior and stays as is).
- Picking, instanced molecule path, shaders, AutoOpt, upload scheduling.

## Decisions

### D1: Repurpose the interaction flag — wireframe instead of skip

In `flushCapturedMeshes`, replace the early return with a render-mode
substitution applied to both draw paths:

```cpp
const int flushMode = m_cameraInteracting ? 1 : inst.mode; // 1 = GL_LINE
if (inst.colored) d->painter->drawColorMesh(*inst.mesh, flushMode);
else              d->painter->drawMesh(*inst.mesh, flushMode);
```

`GLPainter::drawMesh`/`drawColorMesh` already implement mode 1 (line) for the
legacy tier, so the modern flush reuses the exact code path that produces the
net the legacy tier draws — no new rendering code.

Alternative considered: delete the skip entirely (solid during drag).
Rejected: a solid translucent surface composites with alpha blending across
its full projected area every frame of the drag — the expensive rasterization
path — while the net is 1-px lines with lighting off. Wireframe is both the
legacy-faithful and the cheaper choice. (Deleting the flag entirely was also
considered and rejected: D2 still wants interaction state.)

### D2: Clear the flag on release; keep the 250 ms timer as backstop

`GLWidget::mouseReleaseEvent` sets `m_cameraInteracting = false` (next to the
existing `d->quickRender = false`) and requests a repaint — the solid surface
returns on the first frame after release, exactly like the legacy tier. The
existing single-shot timer stays armed from the last move event as a backstop
for releases the widget never sees (pointer leaves the window with a button
held, focus steal mid-drag), bounded at 250 ms.

### D3: Keep backface culling enabled for the wireframe draw

`flushCapturedMeshes` already wraps each draw in
`glEnable(GL_CULL_FACE)/glCullFace(GL_BACK)`. Leave it: the net then shows the
front-facing hemisphere only — which is what the legacy `GL_FRONT` polygon
mode emphasized visually anyway — at half the line segments. Alternative
(disable culling during the drag to draw all edges like raw legacy) rejected:
doubles the line cost for a visually busier net no user asked for.

### D4: Colored (ESP) surfaces take the same substitution

The `inst.colored` branch passes `flushMode` to `drawColorMesh`, whose mode 1
sets `GL_LINE` and disables lighting; per-vertex colors still rasterize via
the color array (lighting off only skips shading, not the vertex color
attribute). Covered by the manual matrix (ESP surface net during drag).

### D5: Stability and try-lock skips are unchanged

A mesh being regenerated is skipped until stable, and a frame whose mesh read
lock is unavailable skips that mesh for that frame — both momentary,
correctness-motivated, and now the *only* permitted absences (the spec delta
names them explicitly). No new concurrency surface: the wireframe draw takes
the same read lock as the solid draw did.

### D6: Verification strategy

Offscreen/CI cannot drive real GLWidget mouse drags (the same limitation task
4.3 of the original change recorded), so behavior is verified by the manual
matrix (tilt/rotate with ORCA orbital and ESP surface: net during motion,
solid at rest, no blank frame, no post-release blink) plus the existing
headless invariants: `consumerparitytest` upload-counter (no re-upload per
frame — unchanged by this fix) and the full ctest suite. No new headless test
is added; the change is a one-branch behavior flip on a path tests cannot
drive.

## Risks / Trade-offs

- [Wireframe line cost on very large surfaces] → 3 line segments per triangle
  is vertex-bound, but strictly lighter than the blended fill it replaces and
  identical to what the legacy tier has always done during drags. If a
  multi-million-vertex surface stalls drags on weak GPUs, that is the trigger
  for a real mesh-LOD follow-up — not for reintroducing hiding.
- [Transient wireframe on release backstop] → if a release event is missed,
  the net lingers ≤ 250 ms (timer), then solid. Bounded, self-healing.
- [Visual delta during drags only] → at-rest rendering is byte-identical to
  today (mode passthrough unchanged); the drag-time net is the intended,
  spec-scoped change.
- [Colored net appearance] → lighting-off flat vertex colors may look
  slightly different from the solid ESP surface's shaded rendering; accepted
  (transient). Note: legacy renderQuick draws colored meshes via drawMesh with
  a solid positive/negative color, while this change draws a per-vertex-colored
  net (drawColorMesh); the divergence is accepted per D4.

## Migration Plan

Single self-contained behavior flip; no data or settings migration. Rollback
= revert the commit (restores the D10 skip).

## Open Questions

None material. Backface-culling choice (D3) and release-clearing (D2) are
decided here and trivially reversible without spec impact.
