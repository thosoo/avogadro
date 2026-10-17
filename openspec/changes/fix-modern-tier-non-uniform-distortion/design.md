## Context

The modern tier projects world points with `gl_Position = uMVP · vec4(worldPos,
1.0)`, where `uMVP` is set from `GLWidget::currentModelViewProjection()`
(`libavogadro/src/glwidget.cpp:1077`):

```cpp
GLfloat mvMat[16], projMat[16];
glGetFloatv(GL_MODELVIEW_MATRIX, mvMat);
glGetFloatv(GL_PROJECTION_MATRIX, projMat);
return QMatrix4x4(projMat) * QMatrix4x4(mvMat);
```

`glGetFloatv` returns the projection and modelview in **column-major** GL
storage. Qt6's `QMatrix4x4` has three conventions that combine here:

1. The raw-array constructor `QMatrix4x4(const float*)` reads its input as
   **row-major** (verified: an input at flat index 1 lands at
   `operator()(0,1)`). So `QMatrix4x4(projMat)` internally holds `projᵀ`.
2. `operator*` is the **standard written-order** product, i.e.
   `Qt(m1 * m2) = m1 · m2` (verified against the source `qmatrix4x4.h`).
3. `data()` stores **column-major** (element (0,1) at `data()[4]`), so GLSL
   reads the matrix back the same way Qt stores it.

Consequently the current code returns, in GLSL, `projᵀ · mvᵀ = (mv · proj)ᵀ`.
The shader then applies `(mv · proj)ᵀ · worldPos` — an inverse-ish camera
rotation composed with a transposed perspective. That is a shear/transpose of
the correct `proj · mv`, so round atoms map to ellipses and bonds land at
wrong lengths/directions: the non-uniform distortion the user sees.

This bug is **dpr-independent** (it is a pure matrix-order error, present at
`devicePixelRatio = 1`), which is why the earlier viewport hypothesis could not
explain the "broken at 100%" symptom. It is **modern-only**: `CompatRendererBackend`
stores the matrix but its `setModelViewProjection` is a no-op — compat renders
through the native fixed-function `glMultMatrix` stack (`proj · mv`) and never
reads `currentModelViewProjection()`. Legacy uses the same native stack.

Code under inspection: `libavogadro/src/glwidget.cpp:1077`
(`currentModelViewProjection()`), `libavogadro/src/rendering/modernbackend.h`
(`setModelViewProjection` stores `m_mvp`, consumed as `uMVP`),
`libavogadro/src/rendering/compatbackend.h` (`setModelViewProjection` is a
no-op), and `libavogadro/src/rendering/shaders.h` (`gl_Position = uMVP ·
worldPos`).

## Goals / Non-Goals

**Goals:**
- Make the modern tier's `uMVP` equal the standard `projection · modelview`
  product of the live GL stack, so modern projects to exactly the same clip
  space as the native (compat/legacy) path.
- Add a headless test that pins this (a `gluProject` cross-check), failing
  pre-fix and passing post-fix.
- Align the `gpu-rendering-pipeline` spec with this requirement.

**Non-Goals:**
- Changing the shaders, projection, camera, or geometry.
- Touching the compat or legacy tiers' rendering.
- Re-imposing or resizing the GL viewport (that is a separate, unproven
  concern; this change does not touch `modernbackend.cpp` or
  `deviceViewportRect()`).
- Re-opening the molecule read-lock work from the earlier HiDPI change.

## Decisions

### D1: Assemble `uMVP` as the standard `proj · mv` given Qt's conventions
Each `QMatrix4x4(GLarray)` internally holds the GL matrix transposed
(convention 1). Transposing it back and multiplying in written order cancels
both the constructor transpose and keeps the written-order product equal to the
GL standard:

```cpp
return QMatrix4x4(projMat).transposed() * QMatrix4x4(mvMat).transposed();
```

Rationale: `QMatrix4x4(projMat).transposed()` holds `proj`; `QMatrix4x4(mvMat).
transposed()` holds `mv`; `operator*` multiplies in written order, so the
result holds `proj · mv`; `data()` returns it column-major to GLSL. Verified
element-exact against a manual column-major `proj · mv` multiply across 1000
random perspective + rotation + translation cameras (0 mismatch).

Alternatives considered:
- *`(QMatrix4x4(mvMat) * QMatrix4x4(projMat)).transposed()`* — also yields
  `proj · mv` and is functionally equivalent, but the `.transposed()` wraps a
  written-reversed product, which reads as "multiply then undo" and is fragile
  against a future "cleanup" that drops the outer transpose. Prefer D1, whose
  two transposes sit directly on the GL arrays ("put each GL matrix into Qt's
  orientation, multiply in shader order").
- *`(QMatrix4x4(projMat) * QMatrix4x4(mvMat)).transposed()`* — yields
  `mv · proj`, **not** `proj · mv`: would collapse geometry into w-space.
  Rejected.
- *Fix the projection or modelview assembly elsewhere* — the single assembly
  site is `currentModelViewProjection()`; the matrices themselves are correct
  (they come straight from the GL stack). Rejected.

### D2: Verify with a headless `gluProject` cross-check, not a pixel readback
A full pixel readback of "round atoms" is not observable headlessly (the
modern tier renders a black frame under xcb + llvmpipe — a pre-existing
QOpenGLWidget presentation limitation). Instead the test drives a real GL
context with a known projection and modelview and asserts that
`GLWidget::currentModelViewProjection()` maps world points to the same clip
coords as `gluProject` on the live `GL_PROJECTION · GL_MODELVIEW` stack. This
compares the matrix composition directly, so it fails pre-fix (the transposed
`(mv · proj)ᵀ`) and passes post-fix, without needing real rasterization.

### D3: The compat tier is unaffected by construction
`CompatRendererBackend::setModelViewProjection` is a no-op; compat renders
through the native `glMultMatrix` stack (`proj · mv`) and never reads
`currentModelViewProjection()`. So this change alters only the modern tier's
`uMVP`. The pick pass stores the same `m_mvp` and reuses it, so it inherits
the repair (its scissor/FBO sizing is unchanged).

## Risks / Trade-offs

- [Qt's conventions differ on the user's Qt build] → the ctor row-major read
  and `data()` column-major store are documented Qt5/Qt6-stable semantics;
  verified against the installed Qt6 here. The test pins the behavior and will
  catch a platform divergence.
- [`.transposed()` semantics] → confirmed empirically to be a real mathematical
  transpose (a transposed translation restored correct behavior in a
  hand-checkable probe), not assumed from docs.
- [A second, DPR-dependent viewport divergence may also exist] → this change
  does not address it; it is demoted to unproven hardening and out of scope.
- [Pick FBO] → unchanged; the pick pass reuses the corrected `m_mvp` and
  re-sizes its FBO on dimension change. No behavior change beyond the fix.

## Migration Plan

Single commit; rollback is a revert of the `currentModelViewProjection()`
return line. Rebuild and re-run the modern render + new projection cross-check
tests. No config, data, or settings migration.

## Open Questions

None blocking. On-machine confirmation (real Windows box) that the modern tier
now matches compat/legacy is still the ultimate acceptance check; the headless
cross-check is the proxy that runs in CI. If the user observes that even the
surface size itself is wrong, the defect moves up to Qt surface creation and is
out of scope — but the compat-equals-correct evidence and the element-exact
matrix verification indicate the matrix assembly is the defect.
