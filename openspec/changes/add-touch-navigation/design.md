# Design: add-touch-navigation

## Context

The input pipeline today: `GLWidget::mousePressEvent/MoveEvent/ReleaseEvent`
(glwidget.cpp:2868+) scale incoming mouse coordinates by
`devicePixelRatioF()` and dispatch the scaled event to the active tool; an
ignored press falls through to `m_navigateTool` (one-finger-equivalent drag
rotate). `wheelEvent` follows the same pattern. Keyboard accelerates rotate /
zoom / pan (navigatetool.cpp:295–333). Navigation primitives already exist as
free functions — `Navigate::zoom`, `Navigate::translate`, `Navigate::rotate`,
`Navigate::tilt` (navigate.cpp) — and the wheel zoom path
(`Navigate::zoom(widget, ref, -MOUSE_WHEEL_SPEED * angleDelta.y())`) defines
the zoom convention.

There is no touch handling anywhere: no `QTouchEvent`, no
`WA_AcceptTouchEvents`. Qt's windowing layer synthesizes mouse events from the
primary touch point only; the second finger is discarded or destabilizes the
stream. Tools consume `QMouseEvent` only, by deliberate "touch-readiness"
design (add-erase-tool design note). The hidpi-render-coordinates spec
establishes that every input path must agree with the rendered frame in
device pixels; the scaled-event dispatch above is the mechanism.

This change is the second half of the two-change split planned in the
2026-11-06 touch-exploration session; the first half (`add-erase-tool`,
shipped) removed the touch dependency for deletion and provided a
touch-friendly erase gesture (lasso).

Verification constraint: CI runners (xvfb Linux, Windows MSVC) have no touch
stack. Real-device verification must be a manual task on the author's HiDPI
touch hardware — the same deferred-to-hardware pattern the
fix-hidpi-viewport-coordinates change used for its interaction scenarios.

## Goals / Non-Goals

**Goals:**
- Single-finger touch reaches tools as ordinary scaled mouse events, with
  jitter swallowed and tap/click semantics preserved (synthesis parity).
- Two-finger pinch zoom + centroid pan drives the existing camera navigation
  primitives; consumed before tool dispatch.
- One-finger rotate preserved via the existing tool-declines → navigate
  fallback.
- Extra fingers are second-class (palm rejection; resting finger never wakes
  up as a new touch).
- The state machine is unit-testable without a real touch stack.

**Non-Goals:**
- Long-press → right-click mapping (superseded by the erase tool; explicit
  user decision in the exploration session).
- Twist-to-rotate gesture (two-finger rotation) — deferred; pinch zoom and
  pan are the v1 camera gestures.
- Trackpad support via `QNativeGestureEvent` (Windows precision touchpads /
  macOS trackpads) — future change; unverifiable locally.
- Stylus pressure, mobile/touch toolbars or UI, multi-window touch policies.
- Tool-side changes: no pick-radius tuning, no threshold changes. Fat-finger
  accuracy is an accepted v1 limitation; the erase tool's lasso already
  covers coarse deletion, and any pick-radius tuning should be evidence-driven
  after the device smoke test (possible follow-up change).
- Re-litigating tap-deletes-with-erase-tool (undo is the accepted mitigation;
  decided in add-erase-tool D1).

## Decisions

**D1 — Own touch state machine in `GLWidget` (Option B), not Qt gesture
recognizers.** The widget sets `WA_AcceptTouchEvents` and overrides
`touchEvent` as the single entry point. Alternatives considered in the
exploration: (A) `grabGesture(Qt::PinchGesture)` — delivers property changes
but none of the finger-identity bookkeeping this design needs (pair tracking,
blacklist, pending state); (C) hybrid with `QNativeGestureEvent` for trackpads
— trackpad streams are out of scope and unverifiable locally. A small explicit
state machine owns the whole contract and is unit-testable by construction.

**D2 — State machine: `IDLE → PENDING → STROKE | GESTURE`, keyed by touch-point
id.** From the exploration session:

- **PENDING** (one fresh point, nothing sent to any tool): moves stay pending
  while travel < slop (~10 logical px, a named constant); travel ≥ slop
  synthesizes a mouse press at the current position, then moves as they come
  (STROKE); quick release < slop synthesizes a press+release pair at the touch
  position (a click).
- **STROKE**: subsequent points synthesize mouse moves; the release
  synthesizes the mouse release (tool-side release semantics — hydrogen
  churn, undo push — run unchanged).
- **GESTURE** (entered when a second point is observed while PENDING,
  including a `TouchBegin` that already carries two points): tracks the two
  original point ids; distance delta between the pair drives zoom; centroid
  delta drives pan; ends when either of the two original ids lifts; camera
  stays where the gesture left it.
- **Second-class rule (palm rejection)**: any point arriving while PENDING /
  STROKE / GESTURE is in flight is blacklisted until it lifts; the blacklist
  survives stroke end, so a finger resting nearby when a stroke finishes never
  wakes up as a fresh touch; a third finger never substitutes into a broken
  pinch pair.

**D3 — Synthesized events reuse the existing scaled-event dispatch exactly.**
Synthesized `QMouseEvent`s are constructed with widget-local logical
positions (what `QEventPoint::position()` / `lastPosition()` already provide)
and then routed through the same `devicePixelRatioF()` scaling constructor the
real mouse handlers use (glwidget.cpp:2872–2877), followed by the same
tool → navigate-fallback dispatch. This guarantees the
hidpi-render-coordinates invariant for touch by construction — no separate
touch coordinate path exists. The synthesized press of a click pair carries
the same timestamp as its release.

**D4 — Camera gestures map onto the existing `Navigate` primitives.** Pinch:
`Navigate::zoom(widget, ref, k * distanceDelta)` with the sign chosen to match
the wheel convention (fingers apart = zoom in, like wheel-up). Pan:
`Navigate::translate` with the centroid delta, matching the middle-drag pan
path. v1 zooms about the view-center reference point (the wheel behavior when
no atom is under the cursor); zoom-toward-fingers is a possible refinement
after device testing, not v1 scope.

**D5 — No tool modifications.** Synthesis preserves every tool's existing
mouse contract, so draw/erase/select behavior specs hold verbatim. Zoom while
stroking is deliberately not supported: the second finger during a stroke is
second-class (D2). Mouse users retain zoom-mid-stroke via the wheel
(drawtool ignores wheel; it falls through to navigate) — the exploration
found this precedent. Mixing gesture and stroke state would force tools to
re-validate stroke state under camera motion; deferred unless the device
smoke test shows demand.

**D6 — State machine is a per-widget member, factored for testability.** A
small internal class (owns the state, point-id tracking, blacklist; consumes
touch point lists; emits synthesized mouse events and camera deltas) lives as
a `GLWidget` member. No global or static state, consistent with per-view
input isolation; two widgets (the app's multi-view startup case) track their
touch interactions independently.

**D7 — Testing strategy: construct touch events in unit tests; real device as
a manual gate.** `touchnavigationtest` drives the state machine with
constructed `QTouchEvent` sequences (QTest `QEventPoint` lists with explicit
ids/positions/timestamps) against an offscreen `GLWidget` fixture (the
hidpiviewporttest / erasetooltest pattern) and asserts: the synthesized mouse
sequence (press/move/release order, positions, click pairing), jitter
swallowing, blacklist survival, two-point-begin → GESTURE, pair-break
behavior, and camera zoom/pan deltas on the fixture's camera. No CI runner
needs a touch stack because events are injected directly. Real-device smoke
(pinch, pan, tap, lasso, palm, plus the deferred HiDPI picking/drawing
checks from fix-hidpi-viewport-coordinates 4.3) is an explicit task on the
author's hardware.

**D8 — MSVC compatibility from the start.** New code compiles under
`-std:c++17 -permissive-`: explicit constructors (no aggregate init of
user-special-member classes), Qt 6.4 `QEventPoint`/`QTouchEvent` APIs only,
no new threading primitives. The Windows installer workflow is triggered
after the Linux CI passes, per repo policy.

## Risks / Trade-offs

- **Platform variance in touch delivery** (libinput/X11 vs Windows digitizers):
  edge cases like both-fingers-in-one-event and pair breaks are covered by
  the state machine design, but real-device smoke is the only honest check of
  stream timing and slop feel. The slop constant may need tuning from device
  evidence; it is a single named constant.
- **Zoom-about-center vs zoom-about-fingers**: v1 trades precision for
  simplicity and wheel parity; noted as a refinement path, not a spec
  requirement.
- **Slop delay on stroke start** (~10 px before synthesis): intentional
  jitter rejection; lasso/draw strokes tolerate it. Mouse input is unaffected
  (the pipeline is touch-only).
- **CI blind spot**: unit tests inject constructed events, so digitizer-level
  surprises (multi-point timing, id reuse by the platform) remain possible;
  the device task exists precisely to close this.
- **No hover synthesis**: hover-dependent feedback (if any tool adds it later)
  will not exist on touch; the spec pins hover-independence of primary
  gestures so tools cannot regress into hover dependence silently.
