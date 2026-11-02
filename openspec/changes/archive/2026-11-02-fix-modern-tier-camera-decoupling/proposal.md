# Proposal: Camera-matrix decoupling for the modern rendering tier

## Why

The modern (shader) tier still reads its `projection · modelview` product from the
fixed-function GL matrix stack (`GLWidget::currentModelViewProjection()`,
`glGetFloatv`). On GL 3.3 Core-profile contexts the stack does not exist, so the
modern tier projects geometry with an identity transform — the giant/displaced
molecule observed on pre-Kepler nouveau (10.66.0.12). `fix-modern-tier-core-profile`
contained this honestly (loud diagnostic, zero GL errors) and recorded the durable
fix — decoupling the camera from the dead stack — as a follow-up non-goal. This
change implements that follow-up, completing correct rendering on Core-only drivers.

## What Changes

- **Camera becomes the single authority for projection and modelview matrices.**
  `Camera::applyProjection()` computes the perspective (and orthographic) matrix
  CPU-side in Eigen and stores it in `d->projection`, replacing the current
  `gluPerspective` → `glGetDoublev` stack round-trip. Stack writes remain only as an
  implementation detail of the compat/legacy tiers (which need the fixed-function
  pipeline) and the legacy `glRenderMode(GL_SELECT)` picking path.
- **`currentModelViewProjection()` reads from the Camera, not the stack.**
  `camera->projection · camera->modelview`, cast to `QMatrix4x4` — no
  `glGetFloatv`. All five uMVP consumers keep their existing call sites.
- **Modern tier renders correctly on Core-profile contexts.** The honest Core
  diagnostic narrows: camera decoupled, only the genuinely-degraded fixed-function
  consumers remain listed (axes glyph, text overlays, tool overlays, legacy mesh
  flush).
- **Spec invariant re-authored.** The `gpu-rendering-pipeline` requirement "uMVP
  SHALL equal projection · modelview of the live GL stack" becomes "uMVP SHALL equal
  projection · modelview as authored by the Camera (single authority); on contexts
  where the fixed-function stack is live it SHALL equal the stack contents
  (preserving native-path parity and the `gluProject` cross-check tests)."
- **Not breaking:** picking, tool placement, and `project()`/`unProject()` already
  read the stored Camera matrices and are untouched. The compat/legacy tiers keep
  their exact rendering path (stack writes in `paintGL` are unchanged on
  non-Core contexts).

## Capabilities

### New Capabilities

- (none — this change completes a requirement gap inside the existing
  `gpu-rendering-pipeline` capability rather than introducing a new one)

### Modified Capabilities

- `gpu-rendering-pipeline`: the "Modern-tier projection transform correctness"
  requirement is re-authored so the transform authority is the Camera object
  rather than the live GL matrix stack, with stack parity preserved as a derived
  guarantee on fixed-function-capable contexts; the Core-profile degradation
  scenario updates so geometry renders correctly on Core-only drivers and the
  diagnostic no longer warns of displaced geometry.

## Impact

- **Modified:** `libavogadro/src/camera.cpp` (CPU-side perspective/orthographic
  matrix authoring; profile-gated stack writes; ~2 functions),
  `libavogadro/src/glwidget.cpp` (`currentModelViewProjection()` reader swap;
  Core diagnostic wording).
- **Unchanged:** all five uMVP consumers, `project()`/`unProject()` and their
  callers, the compat/legacy rendering paths, shaders, picking pipeline.
- **Spec deltas:** `gpu-rendering-pipeline` (projection-transform requirement
  re-authored; Core degradation scenario updated).
- **Tests:** headless unit test for the CPU-side matrices vs. `gluPerspective`/
  `glOrtho` values; existing modernrendertest uMVP-vs-stack cross-check becomes the
  compat-parity guard; behavioral verification on 10.66.0.12 (methane renders
  correctly proportioned under nouveau Core).
- **Risk notes:** float-vs-double rounding at the QMatrix4x4 cast (covered by the
  existing tolerance-based parity tests); deprecated `applyPerspective`/
  `initializeViewPoint` API surface audited for callers before conversion;
  `paintGL2` is dead code (no callers) and left untouched to keep the diff minimal.
