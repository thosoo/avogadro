## 1. Camera authors the projection matrix CPU-side

- [x] 1.1 Add a read-only `const Eigen::Projective3d &projection() const` accessor to `Camera` (camera.h) so consumers and tests can read the authored projection matrix. Verify the project configures and compiles (`cmake --build build --parallel`).
- [x] 1.2 Rewrite `Camera::applyProjection()` to compute `d->projection` CPU-side for both the perspective and orthographic branches using the standard `gluPerspective`/`glOrtho` formulas, with no GL matrix-stack calls; delete the `glGetDoublev(GL_PROJECTION_MATRIX)` read-back and the `gluPerspective`/`glOrtho` stack writes. Verify the build compiles and `applyProjection()` contains no GL matrix-stack calls (grep `applyProjection` in camera.cpp for `gluPerspective`/`glOrtho`/`glGetDoublev`/`glMatrixMode`).

## 2. Upload the authored matrix on the fixed-function stack (compat tiers and pick path)

- [x] 2.1 In `GLWidget::paintGL` (glwidget.cpp:858-869), call `d->camera->applyProjection()` unconditionally outside the Core guard so `d->projection` is authored on every profile; in the non-Core branch, after `glMatrixMode(GL_PROJECTION); glLoadIdentity();`, upload the authored matrix with `glMultMatrixd(d->camera->projection().data())` (replacing the current in-guard `applyProjection()` call). Verify the build compiles; on a non-Core path the fixed-function stack still carries the projection; on Core no GL matrix-stack call is made but `d->projection` is authored.
- [x] 2.2 In the legacy `glRenderMode(GL_SELECT)` pick path (glwidget.cpp:3439), after `gluPickMatrix` and `d->camera->applyProjection()`, upload the authored matrix with `glMultMatrixd(d->camera->projection().data())` so the pick·perspective product is preserved. Verify the build compiles and the existing selection/pick tests still resolve the same primitives.

## 3. Modern tier reads the Camera, not the stack

- [x] 3.1 Rewrite `GLWidget::currentModelViewProjection()` (glwidget.cpp:1290) to return `composeModelViewProjection(QMatrix4x4(camera()->projection().matrix().cast<float>().data()), QMatrix4x4(camera()->modelview().matrix().cast<float>().data()))`, removing the two `glGetFloatv` calls. Verify `modernrenderertest::testProjectionMatrixComposition` and `testProjectionMatchesNativeStack` still pass.
- [x] 3.2 Rewrite `GLWidget::currentModelView()` (glwidget.cpp:1301) to return `QMatrix4x4(camera()->modelview().matrix().cast<float>().data()).transposed()`, removing the `glGetFloatv` call. Verify the build compiles and the modern tier's lit sphere/cylinder shading still uses the correct view-space modelview.

## 4. Narrow the Core diagnostic

- [x] 4.1 In `GLWidget::initializeGL` (glwidget.cpp:706-716), drop the clause claiming the camera matrices still come from the dead GL matrix stack so geometry may render displaced, keeping the degraded fixed-function consumer list (axes glyph, text overlays, tool overlay, legacy mesh flush). Verify the Core diagnostic text no longer mentions camera matrices or displaced geometry.

## 5. Tests

- [x] 5.1 Add a headless unit test (in the existing `modernrenderertest.cpp` style) that constructs a `Camera` with a compat-context parent, calls `applyProjection()` in both perspective and orthographic modes, and asserts the authored `projection()` equals the matrix `gluPerspective` (respectively `glOrtho`) produces for the same `angleOfViewY`/aspect/zNear/zFar. Two comparisons: a closed-form double-vs-formula check at 1e-12, and a live compat-context probe that compares the float-rounded authored matrix to `glGetDoublev` of the stack at 1e-6 (the stack stores GLfloat, so a double-vs-float 1e-12 compare falsely fails by ~1e-7 float rounding — see design.md D3 note). The live probe QSKIPs when the resolved context is Core (fixed-function stack dead) or when no GL 3.3 context is available. Verify the test passes on a runner with a GL 3.3 Compatibility context (QSKIP otherwise); on a Core-only driver it QSKIPs, not fails.
- [x] 5.2 Run the full test suite for the affected areas and confirm no regression: `ctest --test-dir build --output-on-failure` (at minimum the modern-render, projection, pick, and camera-adjacent tests pass).
