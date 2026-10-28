## Why

During continuous scene change — drawing a molecule while AutoOpt runs, or any
sustained editing — the modern tier presents cleared (background-colored,
perceived as black) frames instead of the last ready state. `GLWidget::drawLastReady()`
presents nothing because `ModernRendererBackend::beginFrame()` zeroes the
per-frame instance counts before `endFrame()` draws, and results built by the
worker pool are discarded whenever the molecule revision advanced during the
build — which under continuous mouse movement is almost every build. The
pipeline's own spec ("frames continue to be presented from the last ready
state during the rebuild") is violated by the implementation; users see the
molecule blink off/on at the AutoOpt tick rate and vanish for the duration of
a draw stroke.

## What Changes

- `ModernRendererBackend::beginFrame()` no longer zeroes the draw counts; the
  last-pushed counts persist across frames so an `endFrame()` without pushes
  re-presents the retained GPU-resident geometry. Counts are zeroed only in
  `initialize()` (fresh/context-lost backend) and are watermark-managed at
  push time, which is where the stale-instance protection already lives.
- `GLWidget::drawLastReady()` sets the current MVP before presenting, so the
  retained geometry still tracks camera rotation during an in-flight rebuild.
- The `renderModern()` early-return on molecule read-lock failure presents the
  retained frame instead of a cleared one.
- Optionally (bounded scope): the builder pool coalesces queued jobs older
  than the newest queued generation, so sustained churn does not build stale
  snapshots that will be discarded on arrival.
- Landmines documented, not fixed: the upload-budget estimate, the fence
  wait, and the `ensureCapacity()` re-upload claim are out of scope (see
  design.md).

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `gpu-rendering-pipeline`: the "GUI thread is not blocked by the viewport"
  requirement is sharpened with an explicit presentation clause: while a
  rebuild is in flight, every presented frame SHALL contain the last ready
  scene (never a cleared frame), the retained scene SHALL continue to track
  camera rotation, and presentation SHALL NOT alternate with blank frames
  under continuous scene change (drawing, AutoOpt ticking).

## Impact

- `libavogadro/src/rendering/modernbackend.h/.cpp` — count lifecycle in
  `beginFrame()`/`initialize()`; comments updated (watermark rationale moves
  to where it is enforced).
- `libavogadro/src/glwidget.cpp` — `drawLastReady()` MVP, `renderModern()`
  lock-fail path.
- `libavogadro/src/rendering/builderpool.cpp/.h` — optional coalescing on
  submit.
- No tool changes, no OpenBabel changes, no workflow changes. Safe to run in
  parallel with `fix-autoopt-frozen-atoms` (disjoint files) and with
  `fix-modern-tier-lighting-space` (different functions in the shared file;
  see design.md merge-order note).
