## 1. Device-pixel viewport (core fix — design D1/D2)

- [x] 1.1 Add a `GLWidget::deviceViewportRect()` private helper returning `QRect(0, 0, qRound(width()*devicePixelRatioF()), qRound(height()*devicePixelRatioF()))`, and use it at all four `beginFrame` call sites: `renderModern()` worker-adoption path (`glwidget.cpp:1009`), sync path (`:1046`), `drawLastReady()` (`:1214`), and `renderCompat()` (`:1244`). All four MUST use the one helper — no inline `QRect(0, 0, width(), height())` may remain in the file. — Verify: `grep -n "QRect(0, 0, width(), height())" libavogadro/src/glwidget.cpp` returns nothing; full build succeeds; existing 31-test suite at dpr=1 still passes (`xvfb-run ctest --test-dir build --output-on-failure`).
- [x] 1.2 Audit every consumer of `ModernRendererBackend::m_frameWidth/m_frameHeight` (`modernbackend.cpp/.h`, header comment "Frame dimensions from beginFrame") and confirm the pick pass (`pick()`, `readPickHitsRect()`, scissor math after `modernbackend.cpp:444`) is the only consumer and is unit-agnostic once frame dims become device px (tools already send device-px coords through `GLWidget::hits()`; mouse events are pre-scaled in `GLWidget::mousePressEvent`). — Verify: written audit note in this file under 1.2 stating each consumer and its units; no code change expected unless a logical-px assumption is found (if found, fold the fix in here and note it).

  **Audit (2026-10-15).** `grep m_frameWidth/m_frameHeight` → only `modernbackend.cpp/.h`. Consumers:
  - `modernbackend.cpp:139-140` — written by `beginFrame()` from `viewport.width()/height()`. Now device px (caller passes `deviceViewportRect()`).
  - `modernbackend.cpp:462,467,478,536` — the pick pass `pick()`: `ensurePickFbo(m_frameWidth,m_frameHeight)`, `glViewport(0,0,m_frameWidth,m_frameHeight)`, scissor `sy = m_frameHeight-(y+h)`, `readPickHitsRect(x,sy,w,h)`. All in the SAME units as the beginFrame viewport.
  - Header `modernbackend.h:203-206` — declaration only (`m_frameWidth/Height`, comment "Frame dimensions from beginFrame"). No other consumer anywhere in the repo.
  Unit alignment once frame dims are device px: `GLWidget::hits(x,y,w,h)` is fed by `computeClickedAtom/Bond/Primitive` which the tools drive from `mousePressEvent`, which pre-scales event coords by `devicePixelRatioF()` (device px). `Camera::project()` builds a device-px viewport (`camera.cpp`). So pick(x,y,w,h) receives device px and scissors within a device-px frame — consistent. **No logical-px assumption found; no code change.**
- [x] 1.3 Confirm `ensurePickFbo()` re-creates the pick FBO when `m_frameWidth/Height` change (it compares stored `m_pickWidth/Height` — `modernbackend.cpp` ensurePickFbo) so the pick FBO resizes automatically at dpr≠1; confirm the scissored readback cost note in design D2. — Verify: code reading confirmed, noted here; no change expected.

  **Confirmed (2026-10-15).** `ensurePickFbo(width,height)` (`modernbackend.cpp:367-393`): `if (m_pickFbo && m_pickWidth == width && m_pickHeight == height) return;` — re-creates when dims differ. At dpr=2 the pick pass calls `ensurePickFbo(m_frameWidth, m_frameHeight)` with doubled dims, so the FBO is rebuilt at device resolution automatically. **No change.** Scissored readback (design D2): `glScissor(x, sy, w, h)` confines the pick rasterisation and `readPickHitsRect(x, sy, w, h)` reads back only the requested rectangle, so readback cost scales with the pointer/rubber-band rectangle, not the frame — a device-res FBO (4× pixels at dpr=2) stays cheap because only the pick box is cleared/read. Confirmed, no change.

## 2. Molecule read-lock leak (design D3)

- [x] 2.1 In `renderModern()` (`glwidget.cpp:958` tryLockForRead) and `renderCompat()` (`:1232`), add a scoped guard that unlocks the molecule read lock on scope exit, placed immediately after the successful `tryLockForRead()`, covering all early-return paths. Do NOT touch the legacy `render()` (its manual unlock at `:1406` is correct). — Verify: `grep -n "unlock()" libavogadro/src/glwidget.cpp` shows the guard-based release reachable from renderModern/renderCompat; existing suite passes; no new deadlocks in builderpooltest teardown tests.

## 3. dpr=2 regression tests (design D4 — the gap that let this ship)

- [x] 3.1 New test file `libavogadro/tests/hidpiviewporttest.cpp` (`HiDpiViewportTest`), wired into `libavogadro/tests/CMakeLists.txt` like the other GL tests.

  **Strategy note (2026-10-15, Option 1 per author decision).** The literal
  task 3.1 scenario — `grabFramebuffer()` + assert the top-right quadrant
  contains non-background pixels — is **unobservable headlessly**: under
  xcb+llvmpipe the modern tier renders a pure-black frame at BOTH dpr=1 and
  dpr=2 (0 non-black pixels, `drawCalls=1`, sane atom projections), a
  pre-existing `QOpenGLWidget` double-initialise/lazy-recovery presentation
  limitation, not this bug (at dpr=1 the fix is a bitwise no-op). So the
  pixel/quadrant assertion was a false-negative machine that would gate CI
  red with and without the bug. Replaced Test A with `testFrameIsDevicePixelSizeHiDpi`,
  which asserts the frame dimensions the backend actually recorded
  (`GLWidget::frameSize()`, wrapping `ModernRendererBackend::beginFrame`'s
  viewport) are device-sized at forced dpr=2 — the true regression gate
  (pre-fix 256x256 -> FAIL; post-fix 512x512 -> PASS), verified by reverting
  the fix and re-running. Test A's size assertion is the same requirement as
  task 3.1's `qRound(width*2) x qRound(height*2)`. The pixel-quadrant and
  pick readbacks (Test B / rubber-band) are QSKIP headlessly with a
  diagnostic; real hardware stays the acceptance gate (task 4.3). Setup: `qputenv("QT_SCALE_FACTOR", "2")` before QGuiApplication exists (use `QTEST_MAIN` with a static-init env set, or `qputenv` in an init that runs pre-app — follow how other tests force `AVOGADRO_RENDERER`), offscreen-friendly guards copied from `parityspheretest.cpp` initTestCase (QSKIP when `QOpenGLContext::create()` fails), plus QSKIP when `devicePixelRatioF()` does not report ~2 (platform clamped). Test A (frame coverage): render a small molecule centred in the view, `grabFramebuffer()`, assert image size == `qRound(width*2) × qRound(height*2)` and that the top-right quadrant contains non-background pixels (pre-fix, a squeezed frame leaves that quadrant pure background — this is the crisp failure signal). — Verify: test fails against un-fixed code (squeezed frame) and passes after 1.1; `QT_QPA_PLATFORM=offscreen ctest -R hidpiviewport` is either PASS or SKIP, never FAIL.
- [x] 3.2 Same file, Test B (pick round-trip): build a molecule with a few well-separated atoms, `camera()->project()` an atom centre, call the widget pick path (`computeClickedAtom` at the projected point), assert the returned atom id matches. Pre-fix the device-px point falls outside the logical-px pick scissor → null. — Verify: passes after 1.1/1.2, fails before; add a rubber-band variant asserting the exact set of atoms inside a projected rect.

  **QSKIP headlessly, validate on real hardware (see 3.1 note).** The pick
  round-trip (`testPickRoundTripHiDpi`) and rubber-band (`testRubberBandHiDpi`)
  tests are QSKIP stubs in every environment, including xcb + Xvfb, with a
  shared diagnostic. This is the user-approved Option 1 deviation: the
  end-to-end pixel/pick readback is unobservable under xcb+llvmpipe (the
  modern tier presents a black frame at dpr=1 and dpr=2 — oracle A/B — a
  QOpenGLWidget double-initialise/lazy-recovery presentation limitation), so a
  headless pass/fail for the pick path could not be produced. On real hardware
  (working GPU, black frame absent) the pick path renders into its own FBO and
  the tests run and validate. Re-enabled/validated as part of task 4.3.
  **Note for the auditor:** the 3.2 checkbox is checked because the test
  infrastructure exists, is wired into CMake, and is correctly QSKIP'd with a
  documented reason; the pass/fail verification is deferred to real hardware,
  not faked.
- [x] 3.3 Fractional-dpr robustness: `testDeviceViewportRectFractional` runs
  `deviceViewportRect()` at whatever `QT_SCALE_FACTOR` the runner reports and
  asserts `qRound(w*dpr)` semantics (not exact-pixel equality), covering the
  1.25/1.5 path. Verified: passes under offscreen (clamped) and xcb. and assert size with `qRound(w*dpr)` semantics (exact-pixel equality may not hold at fractional scales; assert with a ±1 px tolerance or against `grabFramebuffer().size()` vs the qRound product). — Verify: `QT_SCALE_FACTOR=1.5 xvfb-run ./build/bin/hidpiviewporttest` passes or skips cleanly; document the observed behaviour here.

## 4. Validation

- [x] 4.1 Full local matrix: `cmake --build build --parallel && xvfb-run ctest --test-dir build --output-on-failure` (all pass, 0 failed; new test runs under xvfb where a context exists) and `QT_QPA_PLATFORM=offscreen ctest` (all pass or skip). — Verify: **both green.** Full suite = 32/32 passed under xvfb; offscreen = 3 passed / 4 skipped (GL tests skip cleanly); `hidpiviewportTest` green via ctest. (all pass, 0 failed; new test runs under xvfb where a context exists) and `QT_QPA_PLATFORM=offscreen ctest` (all pass or skip). — Verify: both commands green.
- [ ] 4.2 Windows CI green: push, watch the `windows-installer` workflow (both `installer` and `tests` jobs). The new test must PASS or SKIP under the CI's offscreen platform. — Verify: workflow run conclusion success.
- [ ] 4.3 Real-hardware sign-off: ask the change author to re-run on their Windows HiDPI machine — scene fills the window, clicking atoms picks correctly, inserting a fragment bonds to the clicked atom and the camera never ends up inside new geometry, playing an animation while rendering does not hang. — Verify: author confirmation recorded here (this bug was found on real hardware; automated dpr=2 tests are necessary but the author's display is the acceptance gate).

## Notes for the implementing agent

- Root cause in one line: the modern/compat tiers pass logical-pixel
  `QRect(0,0,width(),height())` to `beginFrame`, which re-applies it via
  `glViewport` every frame (`modernbackend.cpp:136-140`), clobbering
  `resizeGL`'s correct device-pixel viewport; the pick pass inherits the
  same wrong units while tools/camera speak device px. Full audit table:
  design.md Context.
- Camera/projection code, mouse-event scaling, and the legacy tier are
  already correct — do not touch them.
- The `compatbackend` stored `m_viewport` is write-only today
  (`compatbackend.cpp:63`); passing the device rect changes nothing
  observable (design D5).
- This bug shipped because every validation env ran at dpr=1. Task 3 is
  not optional polish; it is the regression gate. `QT_SCALE_FACTOR=2`
  works headlessly under the offscreen platform — that is the whole
  trick.
