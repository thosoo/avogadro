## 1. Fix the modern-tier projection transform

- [x] 1.1 In `libavogadro/src/glwidget.cpp` `currentModelViewProjection()`,
  assemble `uMVP = projection · modelview` correctly given Qt6's
  `QMatrix4x4` conventions (raw-array ctor reads GL column-major as
  row-major → each `QMatrix4x4(GLarray)` holds the GL matrix transposed;
  `operator*` is written-order; `data()` is column-major). Extracted the
  composition into the protected static helper
  `GLWidget::composeModelViewProjection()` (`ModernRenderTest` is a friend) so
  it is unit-testable without a live GL context; `currentModelViewProjection()`
  now delegates to it:

  ```cpp
  return composeModelViewProjection(QMatrix4x4(projMat), QMatrix4x4(mvMat));
  // composeModelViewProjection():
  return projMat.transposed() * mvMat.transposed();
  ```

  Verify: builds (`cmake --build build --target modernrenderertest`); the
  Qt-only test `testProjectionMatrixComposition()` asserts the result equals a
  manual column-major `proj · mv` element-wise, and asserts the naive
  `QMatrix4x4(proj) * QMatrix4x4(mv)` does NOT.

- [x] 1.2 Confirm the compat tier is unaffected: `CompatRendererBackend::
  setModelViewProjection` is a no-op (`compatbackend.h:60`, `Q_UNUSED(mvp)`);
  compat renders via native `glMultMatrix` on the GL stack, so this change only
  alters the modern tier's `uMVP`. Verify: `git diff` for the logic change
  touches `currentModelViewProjection()`/`composeModelViewProjection()` only;
  `compatbackend.h` is untouched.

- [x] 1.3 Confirm no other consumer of `currentModelViewProjection()` is
  broken. Call sites (`glwidget.cpp:1025`, `:1065`, `:1092`) are the modern
  dispatch path and `emitSceneToBuilder()` (pick pass), which reuses the same
  `m_mvp`; fixing `uMVP` also fixes pick projection. Verify: `rg
  currentModelViewProjection` shows only those sites, all correct with the fix.

## 2. Add the headless projection cross-check test

- [x] 2.1 `testProjectionMatchesNativeStack()` in `modernrendertest`: under an
  offscreen GL context with a known projection/modelview on the live stack,
  compares `gluProject`'s window coords to `GLWidget::composeModelViewProjection()`
  applied to the same world point (via the viewport transform). Compiles and
  links (GLU linked for this target); QSKIPs gracefully where no GL 3.3+ context
  exists. Verified to FAIL with the buggy composition and PASS with the fix.

- [x] 2.2 `testProjectionMatrixComposition()` in `modernrendertest`: pure Qt,
  no GL. Asserts `composeModelViewProjection(QMatrix4x4(proj),
  QMatrix4x4(mv))` equals a manual column-major `proj · mv` element-wise for a
  distinct-aspect perspective + rotated translation, and that the naive
  `QMatrix4x4(proj) * QMatrix4x4(mv)` does not. PASSES; confirmed it FAILs when
  the composition is reverted to the buggy form.

## 3. Update the spec

- [x] 3.1 Added the "Modern-tier projection transform correctness"
  requirement and 4 scenarios to `openspec/specs/gpu-rendering-pipeline/spec.md`,
  and removed the `hidpi-render-coordinates` delta from this change (the
  viewport/dpr requirements are a separate, unproven concern and are not fixed
  here). Verify: `openspec validate --change fix-modern-tier-non-uniform-distortion`
  reports the delta clean.

## 4. Full suite

- [x] 4.1 Built `modernrenderertest` (12 passed, 0 failed, 8 skipped — the
  skips are GL-requiring slots with no software GL on this runner) and ran the
  full `ctest` suite: the only failures are 11 GL targets that ctest runs on
  the `xcb` platform with no `DISPLAY` (`qt.qpa.xcb: could not connect to
  display`, pre-existing env issue); under the correct `offscreen` platform
  every one of those either SKIPs or PASSES (glselectoverflow 7, threadedparity
  3, renderbench 2, hidpiviewport 3 passed). No regression from this change.
