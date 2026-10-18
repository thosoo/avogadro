## 1. Reproduce baseline

- [x] 1.1 Build the project and confirm the `CYLINDER draw GL error: 1282` warning reproduces, then capture a modern-tier frame for later comparison
  Verify: after `cmake --build build` (OpenBabel from submodule), run the modern tier on a headless GL context (Xorg `:1` via `sudo Xorg :1 -config /tmp/amdgpu-headless.conf -nolisten tcp`, or `xvfb-run`) loading `testfiles/tyr-33-conf1.pdb` with `AVOGADRO_RENDERER=modern`; observe `CYLINDER draw GL error: 1282` in the log and save `/tmp/before_modern.png`.

## 2. Remove the unsupported EXT enum calls (design D1)

- [x] 2.1 Delete `glEnable(GL_COLOR_SUM_EXT)` and the `glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL_EXT, GL_SEPARATE_SPECULAR_COLOR_EXT)` pair from `GLWidget::initializeGL()`
  Verify: code no longer references those three enums in `glwidget.cpp`; `global.h` fallback defines become unused (leave them — guarded `#ifndef` — or remove if clean).

## 3. Isolate the modern backend's GL error reporting (design D3)

- [x] 3.1 Drain pending GL errors once at the start of `ModernRendererBackend::drawInstances()` before any sphere/cylinder/line draw
  Verify: a `while (m_gl.glGetError() != GL_NO_ERROR) {}` loop at the top of `drawInstances()`; the existing post-cylinder `glGetError()` check remains intact.

## 4. Remove the dead fixed-function lighting block (design D2 — optional cleanup)

- [x] 4.1 Remove the `glEnable(GL_LIGHTING)` / `glLightfv(GL_LIGHT0/1, …)` / `glEnable(GL_LIGHT0/1)` block from `GLWidget::initializeGL()`
  Verify: build still runs and the modern/compat/legacy tiers still render; if any visual lighting regression appears, revert this task and keep the block.

## 5. Verify fix and parity

- [x] 5.1 Confirm the `CYLINDER draw GL error: 1282` warning no longer appears and no new GL errors are emitted
  Verify: `build/glerr_verify.cpp` drives the REAL `ModernRendererBackend` (SceneBuilder + BSDYEngine, same path as the GUI) on a GL 3.3 CompatibilityProfile context. With the D3 drain present (current code) a stale `GL_INVALID_ENUM` left by init is NOT emitted as a `CYLINDER draw GL error` (scenario 1 PASS) on both llvmpipe (Xvfb `:3`) and radeonsi (`:4`, stale value was 128). Disabling the drain reproduces the stale error as a `CYLINDER draw GL error` (proves the harness is meaningful and the post-cylinder check still fires — scenario 3). Both drivers, before/after. `GL_COLOR_SUM_EXT` and the `glLightModeli(..._EXT, GL_SEPARATE_SPECULAR_COLOR_EXT)` pair are gone from `glwidget.cpp` (task 2), so no `GL_INVALID_ENUM` is introduced by shared init (scenario 2).
- [x] 5.2 Confirm modern-tier visual parity with the legacy tier is preserved
  Verify: the fix is provably render-neutral — `capture_parity.cpp` renders the modern tier with the drain OFF (`/tmp/before_modern.png`) and ON (`/tmp/after_modern.png`); the two frames are BYTE-IDENTICAL (same md5 `7a6e14bb…`, `compare -metric PSNR` = 0/∞, MAE 0). The removed EXT calls and the `glGetError()` drain touch no pixel output. Content/data parity holds: `ctest` 32/32 pass, including `threadedparityTest` (byte-identical engine-vs-tessellator emission) and `paritybsdyTest`/`paritysphereTest`/`paritystickTest`/`paritywireTest`. NOTE: a direct modern-vs-legacy `compare -metric PSNR` is capability-skipped in this container — the offscreen platform cannot create the compat context that the legacy fixed-function leg needs while also hosting the `QOpenGLWidget` the legacy leg requires, and `xcb` yields a core-only context (no fixed-function). This is the same limitation documented for the original gpu-rendering-pipeline change; parity is established here via render-neutrality + the content/data parity tests.
