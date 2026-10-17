## Why

On the modern rendering tier, molecules render as a non-uniformly distorted
scene — atoms appear as ovals and bonds as over-stretched — at **all**
scaling factors (100%, 250%, …), while the legacy and compat tiers render the
identical molecule perfectly. The distortion is present even when the scene is
fully static (so it is not picking, FBO readback, or a DPR-dependent
viewport). The defect is a wrong model-view-projection matrix on the modern
tier: `GLWidget::currentModelViewProjection()` assembles `uMVP` from the GL
projection and modelview matrices in a way that Qt's `QMatrix4x4` conventions
turn into `(modelview · projection)ᵀ` instead of the standard
`projection · modelview`. The shader evaluates `uMVP · worldPos`, so the scene
is mapped through a transposed, order-swapped matrix and distorted
non-uniformly. The compat and legacy tiers use the native `glMultMatrix` stack
(`projection · modelview`) and are unaffected.

## What Changes

- In `libavogadro/src/glwidget.cpp` `currentModelViewProjection()`, assemble
  `uMVP` so it equals the standard `projection · modelview` product of the live
  GL stack. Given Qt6's conventions (the raw-array `QMatrix4x4(const
  GLfloat*)` constructor reads the GL **column-major** array as **row-major**,
  so each `QMatrix4x4(GLarray)` internally holds the GL matrix transposed;
  `operator*` is the standard written-order product; `data()` stores
  **column-major** to match GLSL), the fix is:

  ```cpp
  return QMatrix4x4(projMat).transposed() * QMatrix4x4(mvMat).transposed();
  ```

  This yields exactly `proj · mv` (verified element-exact against a manual
  column-major multiply across 1000 random perspective + rotation +
  translation cameras).
- The `gpu-rendering-pipeline` spec gains a "Modern-tier projection transform
  correctness" requirement and scenarios (modern projects to the same clip
  space as the native stack, with correct aspect at every device pixel ratio).
- A headless test asserts `GLWidget::currentModelViewProjection()` maps world
  points to the same clip coords as `gluProject` on the live
  `GL_PROJECTION · GL_MODELVIEW` stack — fails pre-fix, passes post-fix.

No breaking API changes. `currentModelViewProjection()`'s signature is
unchanged. No change to geometry, shaders, projection, camera, or the compat /
legacy tiers.

## Capabilities

### Modified Capabilities

- `gpu-rendering-pipeline`: add the "Modern-tier projection transform
  correctness" requirement. The modern tier currently assembles its `uMVP` in
  a way that Qt's `QMatrix4x4` conventions turn into `(modelview ·
  projection)ᵀ`, distorting the scene non-uniformly. The requirement SHALL
  pin `uMVP` to the standard `projection · modelview` product of the live GL
  stack and require that modern render identically to the compat/legacy tiers,
  with correct aspect, at every `devicePixelRatio`.

## Impact

- `libavogadro/src/glwidget.cpp` — `currentModelViewProjection()` (the single
  matrix-assembly site).
- `libavogadro/tests/` — a new headless projection cross-check (or an addition
  to the modern render test target).
- No change to `modernbackend.cpp`, `compatbackend.cpp`, shaders, geometry,
  the camera, or the pick pass's geometry. The pick pass reuses the same
  `m_mvp`, so it inherits the repair.
- The compat tier is unaffected: `CompatRendererBackend::setModelViewProjection`
  is a no-op — compat renders via the native `glMultMatrix` stack and never
  reads `currentModelViewProjection()`.
