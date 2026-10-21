## 1. Backend plumbing (modelview into the shaders)

- [x] 1.1 In `libavogadro/src/rendering/modernbackend.h`: add
      `void setModelView(const QMatrix4x4 &mv) { m_modelView = mv; m_haveModelView = true; }`
      next to `setModelViewProjection` (line 143), and members
      `QMatrix4x4 m_modelView; bool m_haveModelView = false;` near
      `m_mvp`/`m_haveMvp` (lines 206-207). Verify: compiles;
      `grep -n "setModelView" libavogadro/src/rendering/modernbackend.h`
      shows the setter and it does not collide with
      `setModelViewProjection`.
- [x] 1.2 In `libavogadro/src/rendering/shaders.h`: add
      `uniform mat4 uModelView;` to the sphere vertex shader AND the cylinder
      vertex shader ONLY (the two lit VS). Verify with
      `grep -c "uniform mat4 uModelView" libavogadro/src/rendering/shaders.h`
      → exactly 2, and
      `grep -n "uModelView" libavogadro/src/rendering/shaders.h | grep -i pick`
      → no hits (pick shaders untouched).
- [x] 1.3 Same file: sphere VS — replace `vNormal = aNormal; // uniform scale
      => normal unchanged` with
      `vNormal = mat3(uModelView) * aNormal;` plus a corrected comment
      (object-space → view-space rotation; magnitude renormalized in the
      fragment stage). Cylinder VS — replace
      `vNormal = normalize(aPos.x * u + aPos.y * v);` with
      `vNormal = mat3(uModelView) * (aPos.x * u + aPos.y * v);` (no
      pre-normalize needed; the FS renormalizes; the `u`/`v` basis and
      `worldPos` math stay object space). Verify:
      `grep -n "vNormal = aNormal" libavogadro/src/rendering/shaders.h`
      → no hits; `grep -n "mat3(uModelView)" .../shaders.h` → exactly 2.
- [x] 1.4 In `modernbackend.cpp`: add `uMVloc` to the per-primitive program
      struct (wherever `uMVPloc` lives) and resolve it next to the existing
      `uniformLocation("uMVP")` calls in `buildGeometry()` for both
      geometries. Verify: build compiles; both locations are resolved at
      build time (no per-frame `uniformLocation` string lookups).
- [x] 1.5 In `drawInstances()` (`modernbackend.cpp` ~689): extend the early
      return gate to `if (!m_glInitialized || !m_haveMvp || !m_haveModelView)
      return;`, and at both existing `uMVP` uniform sets (sphere ~733,
      cylinder ~774) add the companion
      `program.setUniformValue(<geom>.uMVloc, m_modelView);`. The pick
      passes (`pick()`, `buildPickPrograms()`) are NOT touched. Verify:
      `grep -n "uMVloc" libavogadro/src/rendering/modernbackend.cpp` shows
      resolution + exactly 2 per-frame sets, none in the pick path.

## 2. Widget-side plumbing

- [x] 2.1 In `libavogadro/src/glwidget.cpp`: add
      `QMatrix4x4 GLWidget::currentModelView()` next to
      `currentModelViewProjection()` (~line 1076): one
      `glGetFloatv(GL_MODELVIEW_MATRIX, mvMat)` read, return
      `QMatrix4x4(mvMat).transposed()` — same transpose convention as
      `composeModelViewProjection` (declared in the header too). Verify:
      compiles; declaration added to `glwidget.h` next to
      `currentModelViewProjection`.
- [x] 2.2 At BOTH `d->backend->setModelViewProjection(mvp)` call sites in
      `renderModern()` (worker-adopt branch ~1027, synchronous path ~1118),
      add `d->backend->setModelView(currentModelView());` immediately
      before/after. Verify:
      `grep -n "setModelViewProjection\|setModelView(" libavogadro/src/glwidget.cpp`
      — the two modern-tier sites each have both calls; `renderCompat` is
      untouched (it has a no-op MVP setter).

## 3. Correctness verification (spec scenarios)

- [x] 3.1 Rotation stability (spec: Highlight is stable under rotation):
      run the app, load a small organic molecule, rotate continuously —
      highlight stays in the SAME screen direction; if it rotates with the
      scene, the matrix transpose in 2.1 is wrong (most likely failure mode
      — recheck `.transposed()`).
- [x] 3.2 Black-disc sweep (spec: No atom renders fully black at any angle):
      full 360° sweep on both axes; no atom ever presents as a near-black
      disc (the original user-visible defect).
- [x] 3.3 Parity (spec: Parity with the fixed-function tiers): same
      scene/camera under `AVOGADRO_RENDERER=legacy` vs default modern — lit
      hemisphere direction, highlight placement, and ambient regions
      correspond.
- [x] 3.4 Overlay consistency: with AutoOpt running, click-drag an atom —
      the bright halo and the atom under it shade consistently; HUD text and
      halos unchanged.
- [x] 3.5 Picking regression: click-select atoms and rubber-band select —
      results identical to pre-change (pick programs untouched).
- [x] 3.6 Full suite: `ctest --test-dir build --output-on-failure` passes;
      build with no new warnings from the touched files.

## 4. Documentation & capture

- [x] 4.1 Correct the stale comment in the sphere VS (done in 1.3) and check
      `modernbackend.h`'s file-header comment block for any lighting claims
      (none expected — it describes upload/fences). No further doc changes.
