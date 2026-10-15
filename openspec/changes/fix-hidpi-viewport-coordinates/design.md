## Context

The gpu-rendering-pipeline change (complete, un-archived) introduced the
modern instanced tier. On Windows HiDPI displays it renders squeezed into
the bottom-left corner and mis-places interactively added geometry. See
proposal.md — Why.

Codebase coordinate audit (the facts the design rests on):

| Component | Units today | Anchor |
|---|---|---|
| `GLWidget::resizeGL` | device px (`w*dpr`) — the framebuffer truth | `glwidget.cpp` resizeGL |
| Mouse events handed to tools | pre-scaled by `devicePixelRatioF()` → device px | `GLWidget::mousePressEvent` |
| `Camera::project()/unProject()` | device px (builds viewport `w*dpr × h*dpr`) | `camera.cpp` |
| Legacy tier frame | device px (inherits resizeGL viewport) | `glwidget.cpp render()` |
| Modern tier `beginFrame()` viewport | **logical px — the bug** | callers pass `QRect(0,0,width(),height())` at `glwidget.cpp:1009` (worker adoption), `:1046` (sync path), `:1214` (`drawLastReady`), `:1244` (`renderCompat`) |
| Modern backend applies it | `glViewport(viewport…)` every frame, clobbering resizeGL | `modernbackend.cpp:136-140`; re-applied at `:467` for picks |
| Modern pick pass | FBO + scissor sized `m_frameWidth/Height` (logical) but receives **device-px** mouse coords from `GLWidget::hits()` callers | `modernbackend.cpp:444+`, scissor math `y-flip` at `:467+` |

Secondary findings in the same file that this change also addresses:

- `renderModern()` (`glwidget.cpp:958`) and `renderCompat()`
  (`:1232`) acquire `tryLockForRead()` and never unlock. The only
  `molecule->lock()->unlock()` (`:1406`) belongs to the legacy `render()`.
  Writers exist: `animation.cpp:124,214,230` and
  `crystallographyextension.cpp:1098` take `lockForWrite` — with leaked
  read locks they block forever (GUI hang).
- `updateGeometry()` (`glwidget.cpp:2630`) silently returns when
  `tryLockForRead()` fails, leaving stale `center`/`radius` — the inputs to
  `Camera::initializeViewPoint()` and `applyProjection()`.

Why validation missed it: every test environment (offscreen, xvfb, CI,
llvmpipe) runs at dpr=1, where logical == device. The visual-parity tests
render into FBOs with their own explicit viewports and never exercise
QOpenGLWidget's HiDPI framebuffer path.

## Goals / Non-Goals

**Goals:**
- One canonical coordinate space — device pixels — across frame viewport,
  pick pass, and the already-device-px camera/tool conventions.
- Close the molecule read-lock leak in the modern/compat render paths.
- Make the dpr!=1 case automatically testable in the offscreen CI
  environment.

**Non-Goals:**
- Changing tier selection, emission, chunk stores, or shader logic.
- Refactoring `Camera` or the mouse-event scaling (they are already
  correct and set the convention).
- Fixing `updateGeometry()`'s silent early return beyond noting it (its
  failure mode does not occur under the lock fix; a diagnostic logging
  tweak may ride along but is not required).
- Rendering-at-logical-resolution alternatives (reduces sharpness, and
  the rest of the codebase already speaks device px).

## Decisions

### D1: Device pixels are the canonical render/pick coordinate space
Compute the viewport rectangle once per widget and use it everywhere:
`QRect(0, 0, qRound(width()*dpr), qRound(height()*dpr))` with
`dpr = devicePixelRatioF()`. Rationale: three of the four existing
conventions (framebuffer, mouse events, camera projection) already use
device pixels; converting the fourth (modern frame + pick) is a smaller,
safer change than converting the other three to logical pixels, and it
preserves HiDPI sharpness. Alternative (render everything logical)
rejected: loses retina sharpness, touches more call sites, diverges from
the frozen legacy tier's behaviour.

Add a single private helper (e.g. `QRect deviceViewportRect() const` on
GLWidget) and use it at all four `beginFrame` call sites — the three in
`renderModern()` (worker-adoption, sync, `drawLastReady`) plus
`renderCompat()` — so the units can never drift apart again.

### D2: Pick pass needs no size logic of its own
With D1, `m_frameWidth/Height` become device pixels. The pick call chain
then lines up: tools pass device-px event coords → `GLWidget::hits()`
forwards them → `pick(x,y,w,h)` scissors in device px within a device-px
frame. `ensurePickFbo()` already re-creates the FBO when dimensions change
(`modernbackend.cpp`), so the pick framebuffer resizes automatically.
Ornith must **verify** two things rather than rewrite:
1. No remaining consumer of `m_frameWidth/Height` assumes logical px
   (grep the header comment "Frame dimensions from beginFrame" — pick is
   the only stated consumer).
2. The pick FBO at device resolution (4× pixels at dpr=2) keeps the
   scissored readback cheap — it is already scissored, so cost scales with
   the pointer rectangle, not the frame. No change expected; note the
   numbers in the test output.

### D3: Read-lock leak fixed with RAII, not by hand-placed unlocks
`renderModern()` and `renderCompat()` have several early-return paths; a
small scoped guard (`struct { QReadWriteLock* l; ~X(){ l->unlock(); } }
lockGuard{d->molecule->lock()};` or equivalent) placed immediately after
the successful `tryLockForRead()` guarantees release on every exit.
Alternative (manual `unlock()` at each return) rejected: the legacy
`render()`'s manual unlock at `:1406` is exactly the pattern that got
lost when the modern paths were written.

### D4: dpr=2 regression test via QT_SCALE_FACTOR
`QT_SCALE_FACTOR=2` makes `devicePixelRatioF()` return 2 under the
offscreen platform, so the HiDPI path is testable headlessly (this is the
test gap that let the bug ship). Test shape:

- Create the GLWidget under the offscreen platform with
  `QT_SCALE_FACTOR=2` forced via `qputenv` **before** QGuiApplication
  construction; render a small molecule covering the view centre.
- `grabFramebuffer()` returns the device-sized image. Assert: image size
  equals `width()*2 × height()*2`; non-background pixels exist in the
  top-right quadrant (a squeezed frame leaves pure background there —
  this is the crisp failure signal for the pre-fix code).
- Pick round-trip: project an atom centre with `Camera::project()`, call
  the widget pick path (`computeClickedAtom` at the projected point),
  assert the returned atom id matches. Pre-fix, the device-px coordinate
  lands outside the logical-px scissor and returns null.
- Skip cleanly when no GL context is available (follow the existing
  guard pattern in `parityspheretest.cpp` initTestCase — QSKIP when
  `QOpenGLContext::create()` fails), and skip when
  `devicePixelRatioF()` does not report 2 after forcing the env var
  (platform may clamp), so the test degrades to a skip rather than a
  false failure on runners that ignore scale factors.

### D5: Compat tier gets the same rect for free
`CompatRendererBackend` stores `m_viewport` but never reads it
(`compatbackend.cpp:63` is the only reference) — fixed function inherits
resizeGL's correct viewport. Passing the device rect to its `beginFrame`
keeps the stored value truthful for the future and changes nothing
observable at dpr=1. No behaviour change to the frozen compat tier.

## Risks / Trade-offs

- [Pick FBO memory at device resolution] → 4× pixels at dpr=2, but the
  FBO is reused across picks and the readback is scissored; at 4K×dpr=2
  worst case ~66 MB RGBA+depth, acceptable for a desktop app. Revisit
  only if a embedded target appears (out of scope).
- [qRound rounding at fractional dpr (1.25, 1.5)] → the framebuffer is
  QManaged-layout rounded the same way; verify the test also runs at
  `QT_SCALE_FACTOR=1.5` once, or assert against
  `qRound(width*dpr)` rather than `width*dpr` exactly.
- [Other logical-px consumers discovered mid-implementation] → if Ornith
  finds another consumer of widget `width()/height()` inside the render
  or pick path (e.g. text overlay scissor), stop and fold it into this
  change with a note in tasks.md rather than leaving a mixed-unit path.
- [Lock guard changes renderModern's lock hold time] → the lock is held
  for the same scope as today (the frame), just actually released at the
  end; no new contention because mutators never took the write lock, and
  the writers that do exist (animation/crystal) were previously deadlocked
  by the leak — strictly better.

## Migration Plan

Single commit series, no data or settings migration. Rollback = revert.
Deployment order: code fix → dpr=2 test → full local suite (xvfb +
`QT_SCALE_FACTOR=2` variant of the new test) → CI. The dpr=1 regression
surface is the compat/legacy tiers' unchanged paths plus modern at
dpr=1, covered by the existing 31-test suite.

## Open Questions

None blocking. Deferred: whether `updateGeometry()`'s silent early return
deserves a qWarning (cosmetic; does not affect specs or tasks).
