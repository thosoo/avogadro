## Context

The modern tier's two lit shaders compute `dot(N, L)` with `N` in **object
space** and `L` in **view space**:

- `shaders.h` sphere vertex shader (~line 110): `vNormal = aNormal;` with the
  comment "uniform scale => normal unchanged" — true for scale, false for the
  camera rotation that also sits in the modelview. The normal is never
  transformed into view space.
- `shaders.h` cylinder vertex shader (~line 185): `vNormal =
  normalize(aPos.x * u + aPos.y * v)` — the radial basis `u/v` is derived
  from the object-space bond axis, so this normal is also object space.
- The fragment shaders (`kSphereFragmentShader`, `kCylinderFragmentShader`)
  light against `uLight0Dir`/`uLight1Dir`, set once as constants
  (`setLightingUniforms`, `modernbackend.cpp` ~line 693-702) from
  `LIGHT0_POSITION`/`LIGHT1_POSITION` (`global.h:127,131`).

The parity target is the fixed-function path: `glwidget.cpp:648,655` sets
`GL_POSITION` in `initializeGL` with an **identity modelview**, which per the
fixed-function spec freezes those lights in **eye space** (headlamp). The
legacy painter only sets materials per draw (`glpainter_p.cpp` never touches
`GL_POSITION`), so this init-time eye-space light is the only legacy light.
Nothing else in the shaders space is wrong: the lighting preamble constants,
material derivation, and Blinn-Phong math match `glpainter_p.cpp` and stay.

The only matrix the backend currently receives is the composed MVP
(`modernbackend.h:143` `setModelViewProjection`; `GLWidget::
currentModelViewProjection()` at `glwidget.cpp:~1076` composes
`projMat.transposed() * mvMat.transposed()` per the transpose-convention
comment). The view-space normal transform needs the modelview separately.

See proposal.md — Why — for the two visible defects this produces (sliding
highlight; fully-unlit near-black discs at certain camera angles).

## Goals / Non-Goals

**Goals:**
- Normals in the sphere and cylinder lit vertex shaders are transformed into
  view space with the model-view matrix.
- The backend can receive the modelview alongside the MVP with minimal API
  surface.
- Exact headlamp parity with compat/legacy (same light constants, same
  material derivation — only the space error is fixed).

**Non-Goals:**
- No change to the light constants, material model, or preamble math.
- No change to the pick programs (they carry no normals and no lighting).
- No change to the compat/legacy tiers or the fixed-function light state in
  `initializeGL` (it is the parity target and is correct).
- No new locking, threading, or upload changes (that is
  `fix-modern-tier-frame-presentation`'s territory).

## Decisions

1. **Add `uModelView`, transform normals in the vertex shader.**
   - `modernbackend.h`: next to `setModelViewProjection` (line 143) add
     `void setModelView(const QMatrix4x4 &mv) { m_modelView = mv; m_haveModelView = true; }`
     and members `QMatrix4x4 m_modelView; bool m_haveModelView = false;`
     near `m_mvp`/`m_haveMvp` (lines 206-207).
   - The per-primitive program struct that holds `program`/`uMVPloc` (the
     `m_sphereGeom`/`m_cylinderGeom` objects) gains a `uMVloc` int, resolved
     once at build time next to `uMVPloc` (`buildGeometry()`, after the
     existing `uniformLocation("uMVP")` calls).
   - `drawInstances()`: wherever `uMVP` is set (sphere ~line 733, cylinder
     ~line 774), also set `uMVloc` from `m_modelView`. Gate: extend the
     existing early return (`!m_haveMvp`) to also require
     `m_haveModelView` — a frame without both matrices must not draw (the
     retained-frame path from the sibling change sets both).
   - `GLWidget::renderModern()`: at both `setModelViewProjection(mvp)` call
     sites (worker-adopt branch ~line 1027, synchronous path ~line 1118), add
     `d->backend->setModelView(currentModelView())` where
     `currentModelView()` is a new one-liner next to
     `currentModelViewProjection()` that returns
     `QMatrix4x4(mvMat).transposed()` from a fresh
     `glGetFloatv(GL_MODELVIEW_MATRIX, ...)` read.
   - **Transpose convention (critical):** `QMatrix4x4`'s raw-array
     constructor reads GL's column-major array as row-major, so — exactly as
     `composeModelViewProjection` does — the GL modelview array must be
     `.transposed()`ed before it is a correctly-oriented `QMatrix4x4`. Qt's
     `setUniformValue(QMatrix4x4)` then uploads it correctly for GLSL
     `mat4`. Getting this wrong transposes the rotation and reproduces the
     bug in a new guise.
   - Vertex shader changes:
     - Sphere: `vNormal = mat3(uModelView) * aNormal;` — update the stale
       "uniform scale => normal unchanged" comment to say the modelview
       rotation is applied and the fragment stage renormalizes.
     - Cylinder: `vNormal = mat3(uModelView) * (aPos.x * u + aPos.y * v);`
       (the `u`/`v` basis derivation itself stays object space — it must,
       because `aPos` is object space; only the resulting normal is rotated).
     - Add `uniform mat4 uModelView;` to both lit vertex shaders (NOT the
       pick shaders — they have no `vNormal` at all and must stay untouched).
   - Fragment shaders: unchanged — they already `normalize(vNormal)`, which
     absorbs the uniform-scale magnitude of the modelview.
   - *Alternatives considered:* (a) rotate the light constants into object
     space per frame on the CPU (inverse modelview) — rejected: per-frame
     matrix inverse, makes the shader constants meaningless, and breaks the
     "constants mirror global.h" documentation property; (b) derive view-space
     normals via `dFdx/dFdy` — rejected: flat-shading artifacts, overkill.
2. **Uniform scale assumption, documented.** Avogadro's modelview contains
   rotation + translation + at most uniform zoom, so `mat3(uModelView)` is a
   valid (orthogonal × uniform) normal transform; fragment renormalization
   finishes it. A non-uniform modelview scale would need an inverse-transpose;
   assert none exists via a debug comment rather than code. Verified during
   implementation by checking `Camera`/`Navigate` transformations.
3. **Keep the preamble/constants byte-identical.** The bug is the space
   mismatch, not the values. After the fix, `L0 ≈ (0.55, 0.48, 0.69)` is a
   genuine upper-right-front view-space light — matching where fixed-function
   eye-space lights have always put the highlight.

## Risks / Trade-offs

- [Transpose mistake silently reproduces the bug with rotated lighting] → the
  verification matrix includes the rotation-direction check: rotate the
  camera clockwise; the highlight must stay screen-fixed (if it rotates
  *with* the scene, the matrix orientation is wrong).
- [Shared file with `fix-modern-tier-frame-presentation`] → that change edits
  `beginFrame()`/`initialize()` count lifecycle and `drawLastReady()`; this
  change edits shader sources, `drawInstances()` uniform sets, and
  `setModelView` plumbing. Different regions; land in either order, rebase is
  trivial. If both land the same day, land lighting first and rebase
  presentation on it (presentation's `drawLastReady` MVP call then naturally
  pairs with `setModelView`).
- [Overlays drawn through the legacy painter (tool `paint()`, clicked-atom
  halo, HUD text) already use fixed-function eye-space lighting correctly] →
  no parity gap is introduced between instanced geometry and overlays after
  this fix; verify visually that the halo and the atom under it shade
  consistently.
- [GL_CORE contexts] → `glGetFloatv(GL_MODELVIEW_MATRIX)` in
  `currentModelView()` mirrors what `currentModelViewProjection()` already
  does on the same paths; no new compatibility surface.

## Verification (maps to spec scenarios)

Manual, on the built app (`AVOGADRO_RENDERER` unset for modern; compare with
`=legacy` for parity):

1. Rotation stability: rotate continuously; highlight stays in the same
   screen direction; no sliding across the molecule (spec scenario 1).
2. Black-disc sweep: full 360° rotation on both axes on a small organic
   molecule; no atom ever reads as a near-black disc (spec scenario 2). This
   is the original user-visible defect — confirm it is gone.
3. Parity: same scene, same camera, `AVOGADRO_RENDERER=legacy` vs modern —
   lit hemisphere direction and highlight placement correspond (spec
   scenario 3).
4. Overlay consistency: with AutoOpt running, the clicked-atom halo and the
   atom under it shade consistently.
5. Automated: `ctest --test-dir build --output-on-failure` full pass; pick
   regression check (click-select still returns correct atoms — pick programs
   untouched, but run the interaction anyway).

## Migration Plan

Single commit; no data, config, or pin changes. Rollback = revert the commit.

## Open Questions

- None blocking.
