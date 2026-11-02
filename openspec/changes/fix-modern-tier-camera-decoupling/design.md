# Design: fix-modern-tier-camera-decoupling

## Context

`fix-modern-tier-core-profile` made Core-profile containment honest but recorded the
camera coupling as a follow-up non-goal. Current data flow (verified in the tree):

- `Camera` already stores `Eigen::Projective3d modelview` and `projection` CPU-side
  (`camera.cpp:50`). `applyModelview()` pushes the stored modelview onto the stack
  (`glMultMatrixd`, `camera.cpp:274`).
- `applyProjection()` is a **stack round-trip**: `gluPerspective(...)` (or the
  orthographic branch) followed by `glGetDoublev(GL_PROJECTION_MATRIX, d->projection)`
  (`camera.cpp:242-269`). On Core this is invalid twice over.
- `Camera::project()`/`unProject()` already use the **stored** matrices via
  `gluProject`/`gluUnProject` (`camera.cpp:284,305`) — picking and tool placement are
  already stack-free, which is why they behaved correctly on `.12` while rendering did
  not.
- The modern tier's GL-stack dependencies are `GLWidget::currentModelViewProjection()`
  (`glwidget.cpp:1290`) and `GLWidget::currentModelView()` (`glwidget.cpp:1301`): both
  read the stack via `glGetFloatv`. The uMVP reader is consumed by 5 call sites (1149,
  1225, 1271, 1328, 1509) and the modelview reader by 4 (1150, 1229, 1272, 1510); the
  consumers must not change — only the matrix source does.
- Stack writers: `GLWidget::paintGL` (guarded to non-Core by the core-profile change),
  dead `paintGL2` (no callers), and the legacy `glRenderMode(GL_SELECT)` pick path
  (`glwidget.cpp:3439/3445`, unreachable on Core by tier gating). `renderText`'s stack
  reads (`glwidget.cpp:4203`) sit behind the `renderTextOverlay()` deprecation shim on
  the modern tier.

## Goals / Non-Goals

**Goals**

- Camera authors its projection matrix CPU-side (perspective **and** orthographic
  branches) so `d->projection` is correct without any GL call.
- `currentModelViewProjection()` reads `camera->projection · camera->modelview`,
  removing the modern tier's last GL-stack dependency.
- Modern tier renders correctly on Core-only drivers; the Core diagnostic narrows.
- Preserve exact parity with the native fixed-function path on compat contexts.

**Non-Goals**

- No changes to the compat/legacy rendering paths, shaders, picking pipeline, or the
  five uMVP consumers.
- No removal of the fixed-function stack upload in `paintGL` on non-Core contexts —
  the compat/legacy tiers keep their exact path (frozen low tiers).
- No work on the remaining degraded fixed-function consumers (axes glyph, text/tool
  overlays, legacy mesh flush) — separate concern.
- `paintGL2` is not dead at source level (GLGraphicsView::drawBackground calls it,
  though that view is never instantiated), so it is given the same authored-matrix
  upload as the pick path to avoid a latent identity-projection render; no other
  change to it.

## Decisions

### D1: Camera authors the projection matrix in Eigen (replace the round-trip)

`applyProjection()` computes the perspective matrix from its own members
(`angleOfViewY`, aspect from parent widget size, `zNear`/`zFar` already computed
locally) using the standard `gluPerspective` formula, and the orthographic matrix from
the same parameters the `glOrtho` branch uses today, storing into `d->projection` with
**no GL matrix-stack calls at all** (the spec delta's "performs no GL matrix-stack
calls" scenario). `glGetDoublev` read-back and the `gluPerspective`/`glOrtho` stack
writes are deleted.

The stack upload becomes a pure consumer of the authored matrix, performed at the two
live call sites rather than inside `applyProjection()`: the `paintGL` non-Core branch
and the legacy `glRenderMode(GL_SELECT)` pick path both upload the authored matrix with
`glMultMatrixd(d->camera->projection().data())` after `glLoadIdentity()` (respectively
after `gluPickMatrix`), so the stack reproduces the Camera-authored matrix exactly and
the pick·perspective product is preserved. `applyProjection()` is called unconditionally
in `paintGL` (outside the Core guard) so `d->projection` is authored on every profile —
including Core, where the fixed-function stack block is skipped (otherwise `d->projection`
stays identity and the modern tier renders MV-only geometry on Core).

*Why not keep the read-back on compat?* It is the coupling that bit us: any context
where the stack is dead would silently zero the camera. CPU-side authoring makes the
stored matrix valid everywhere; the compat upload is then a pure consumer of it.
*Alternative considered:* dual-path (read-back on compat, Eigen on Core) — rejected:
two code paths for one matrix invites drift; Eigen-only with upload-from-computed keeps
one authority.

### D2: The modern tier reads the Camera, not the stack

Replace the stack reads in `currentModelViewProjection()` and `currentModelView()` with
the Camera's stored matrices. `currentModelViewProjection()` returns
`composeModelViewProjection(QMatrix4x4(camera->projection().matrix().cast<float>().data()),
QMatrix4x4(camera->modelview().matrix().cast<float>().data()))` — reusing the tested
composer so the existing transpose/order regression guards keep covering the real path.
`currentModelView()` returns
`QMatrix4x4(camera->modelview().matrix().cast<float>().data()).transposed()`.
(`.cast<float>()` keeps the raw-array constructor MSVC-safe: `qreal` is `float` on
Windows.) This needs a new read-only `const Eigen::Projective3d &projection() const`
accessor on `Camera`. All five uMVP consumers and the four modelview consumers stay
untouched.

*Why not cache the product?* It is one 4×4 multiply per frame; caching adds
invalidation plumbing for nothing measurable.

### D3: Parity is asserted, not assumed

`applyModelview()` already uploads the stored modelview; D1 makes the compat upload
likewise come from the authored matrix. So "Camera-authored product == stack product on
compat contexts" holds by construction. The existing modernrendertest uMVP-vs-stack
`gluProject` cross-check stays as the regression guard, plus a new headless unit test
asserting the authored perspective/orthographic matrices equal `gluPerspective`/
`glOrtho` output element-wise.

*Test-tolerance note (oracle finding, resolved):* the new test has two comparisons.
(1) Closed-form: the authored matrix vs the analytic `gluPerspective`/`glOrtho`
formula, both double, at a 1e-12 tolerance.
(2) Live compat-context probe: the authored matrix vs `glGetDoublev` of the actual
fixed-function stack. The GL stack stores the matrix as GLfloat, so `glGetDoublev`
returns the float-rounded matrix widened to double; comparing a double-authored
matrix to it at 1e-12 falsely fails by the ~1e-7 float rounding (verified on real
GL: the float-rounded authored matrix is bitwise-identical to the stack on
llvmpipe). The live probe therefore compares the float-rounded authored matrix
(`static_cast<float>`) to the live stack at a 1e-6 tolerance, which is 0 on real GL
and still catches any real matrix error (> 1e-3).

*Float-drift note:* the stack is double internally (`GetDoublev` today), the product
cast to float is unchanged from today's behavior (the stack read was already float for
the uMVP path), so no drift is introduced by this change.

### D4: Core diagnostic narrows

The honest Core diagnostic (`glwidget.cpp` initializeGL) drops the "camera matrices
still come from the GL matrix stack, which is dead on Core, so geometry may render
displaced" clause; it keeps the degraded fixed-function consumer list. The
core-profile change's spec scenario ("render does not present identity-transformed
geometry") continues to hold — now trivially, because geometry is transformed.

## Risks / Trade-offs

- **Matrix-element mismatch vs `gluPerspective`**: mitigated by D3's element-wise unit
  test; the formula is fully specified.
- **Ordering hazard**: `d->projection` must be authored before the first
  `project()`/uMVP in a frame. Today `paintGL` calls `applyProjection()` before
  render; tools call `apply*` via the same paths. No new hazard introduced, but the
  change must keep the call order intact (asserted by existing tests).
- **Deprecated API surface**: `applyPerspective()` (forwards to `applyProjection`) and
  `initializeViewPoint()` — audit callers before conversion; expected: no behavioral
  change needed, they inherit the fix.
- **`renderText` stack reads**: verified unreachable on the modern tier (deprecation
  shim to `renderTextOverlay`); left as-is for the compat tier where the stack is
  alive.

## Migration Plan

Single commit series on one branch: (1) camera.cpp CPU-side authoring + Core-safe
gating, (2) glwidget.cpp reader swap + diagnostic wording, (3) tests. No data or
settings migration; no API changes (public `Camera` interface unchanged —
`applyProjection` signature is const and unchanged).

## Open Questions

- None blocking. If the element-wise test surfaces a real discrepancy in the
  orthographic branch (e.g. asymmetric near/far handling), resolve in favor of
  matching `glOrtho` exactly, since compat parity is the invariant.
