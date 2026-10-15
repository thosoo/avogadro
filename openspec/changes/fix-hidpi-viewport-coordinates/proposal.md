## Why

On Windows HiDPI displays (devicePixelRatio != 1) the modern renderer tier is
severely broken: the scene renders squeezed into the bottom-left corner of the
viewport, and picking/geometry placement mismatches make inserted fragments
land at wrong world positions, so the camera appears to end up inside newly
added geometry. The gpu-rendering-pipeline change was validated only in
dpr=1 environments (offscreen, xvfb, CI), so this never surfaced. The fix is
a coordinate-convention correction, and it is needed now because the modern
tier is the default tier on capable hardware.

## What Changes

- The modern tier's frame viewport, pick-pass framebuffer and scissor
  mathematics switch from logical widget pixels to device pixels
  (`width * devicePixelRatioF`), matching the conventions already used by
  `GLWidget::resizeGL`, the legacy tier, the scaled mouse events handed to
  tools, and `Camera::project()/unProject()`.
- `GLWidget::renderModern()`, `renderCompat()`, and `drawLastReady()` compute
  the viewport rectangle once (device pixels) and pass it to every
  `beginFrame()` call site.
- The molecule read lock taken by `renderModern()`/`renderCompat()` is
  released on all exit paths (RAII), closing a lock leak that deadlocks
  writers (`animation.cpp`, `crystallographyextension.cpp` take
  `lockForWrite`).
- A dpr=2 regression test is added (via `QT_SCALE_FACTOR=2`), asserting the
  frame spans the full device-pixel framebuffer and that pick round-trips
  (project an atom, pick its projected position, expect that atom's id).

## Capabilities

### New Capabilities

- `hidpi-render-coordinates`: Device-pixel coordinate conventions for the
  render and picking pipeline — frame viewport, pick scissor/framebuffer,
  and camera projection consumers must share one coordinate space on
  displays with `devicePixelRatio != 1`.

### Modified Capabilities

## Impact

- `libavogadro/src/glwidget.cpp` — `renderModern()` (three `beginFrame`
  sites incl. worker-adoption and `drawLastReady()`), `renderCompat()`,
  read-lock RAII.
- `libavogadro/src/rendering/modernbackend.cpp/.h` — viewport handling in
  `beginFrame()`, pick-pass size/scissor assumptions (`m_frameWidth/Height`
  become device pixels); verify `ensurePickFbo()` resizes correctly.
- `libavogadro/src/rendering/compatbackend.cpp` — stores the viewport but
  does not apply it (fixed function inherits `resizeGL`'s viewport); audit
  any consumer of the stored rect.
- New test under `libavogadro/tests/` (dpr=2 rendering + pick round-trip;
  skips cleanly when the offscreen platform cannot provide a context).
- No API changes; no tier-selection changes; frozen tiers untouched except
  the shared `renderCompat()` call site (device-pixel rect only — behavior
  on dpr=1 unchanged).
