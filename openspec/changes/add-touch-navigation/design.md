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
- Twist-to-rotate as a TWO-finger gesture — out; two-finger horizontal
  movement pans (the P52 re-test's perceived "twist" was gesture
  non-formation degrading to the one-finger rotate fallback, not a desired
  behavior). ROTATION itself is in scope as the THREE-finger orbit gesture
  (D2, task 5.7) — an explicit user decision pulling it into this change
  (2026-11-10): pinch zoom and pan stay two-finger, orbit moves to three
  fingers.
- Trackpad support via `QNativeGestureEvent` (Windows precision touchpads /
  macOS trackpads) — future change; unverifiable locally. Re-confirmed by the
  P52 device pass: the reported failing pinch was on the touchscreen (task
  5.2), so trackpad pinch stays out of scope for this change.
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
- **Stroke arming (task 5.5, P52 pinch-draws-atom fix; widened by task 5.6)**:
  when a single finger crosses the slop it enters an ARMED STROKE — the press
  is HELD for `kTouchStrokeArmMs` (a named constant, ~250ms after task 5.6)
  rather than emitted immediately. A second finger arriving during the window
  forms a two-finger GESTURE with the held press never emitted, so an editing
  tool (draw/erase) draws no atom; a genuine one-finger stroke proceeds once
  the window elapses (the press is then emitted at the slop-crossing
  position). This is the policy decision selected by the P52 evidence (task
  5.3, cause 2): with the draw tool active a pinch drew an atom because
  finger 1 crossed the slop and the immediate press reached the draw tool
  before finger 2 arrived. Delivery-independence holds inside the window
  (second finger as its own touch-begin, an update carrying both points, or
  both-fingers-begin); a release or cancel during arming cleans up without
  emitting a phantom or orphaned stroke.
  **Task 5.6 widening (device evidence, fresh installer run 42656894181 =
  c5e33bf39)**: with the 50ms window the P52 still drew atoms on pinch — the
  window elapsed while finger 1 was still moving toward the pinch (human
  finger-planting commonly takes 150–400ms, so the press was emitted before
  finger 2 landed, and finger 2 was then blacklisted). The window is raised
  to ~250ms: it covers typical pinch planting, and the cost is a touch-only
  stroke-start ink delay (the mouse path is unaffected; a bond-draw stroke
  tolerates a 250ms press delay; taps are unchanged — a release below slop is
  still a click regardless of the window). If the on-device re-record still
  shows atoms drawn, the fallback is a recovery policy — on a second finger
  arriving during a committed stroke, synthesize the release so the tool
  finalizes and pushes its command, then undo that command so the molecule is
  unchanged, and form the gesture. Verified mechanics the recovery design
  must respect: the DrawTool press mutates the live molecule directly
  (drawtool.cpp:291–296 addAtom) and its AddAtomDrawCommand exists only at
  release (drawtool.cpp:607–615), so recovery is a release-synthesis + undo
  design that must first resolve the cancel-mid-stroke residue conflict (see
  Risks: Cancel-mid-stroke).
- **Three-finger orbit (task 5.7)**: a third live point arriving while the
  two-finger GESTURE is in flight promotes the gesture to a THREE-FINGER
  ORBIT — the centroid delta of all three points drives `Navigate::rotate`
  with the same mapping the navigate tool's left-drag uses
  (navigatetool.cpp:241), so three fingers rotate the molecule regardless of
  the active tool. A touch event that begins with three or more points goes
  straight to the orbit gesture (no pending state, no synthesized tool
  events). The gesture ends when any of the three original point ids lifts,
  leaving the camera where the gesture left it; after promotion the two
  fingers' zoom/pan deltas stop (the orbit replaces the pinch, it does not
  compose with it). This follows the common touch-3D convention (one finger
  = tool gesture / rotate fallback, two fingers = zoom + pan, three fingers
  = orbit); convention research may refine the mapping later. A third point
  arriving during a single-finger STROKE remains second-class (palm
  rejection is unchanged on the destructive path); promotion applies only to
  the camera path, where an accidental palm-induced rotation is recoverable
  and never mutates the molecule.
- **GESTURE** (entered when a second point is observed while PENDING,
  however the platform groups the two fingers into events — a touch event
  already carrying two points, an update carrying both, or a single-point
  touch-begin arriving while the first finger is still pending): tracks the
  two original point ids; distance delta between the pair drives zoom;
  centroid delta drives pan; ends when either of the two original ids lifts;
  camera stays where the gesture left it. Delivery-independence is kept as
  hardening only: no known Qt Windows delivery sends the second finger as
  its own single-point touch-begin (Qt 6.6 WM_POINTER aggregates all live
  fingers into every delivery and derives TouchBegin only when all points
  are Pressed), but the machine must not depend on grouping. The P52 pinch
  failure has a separate, undiscriminated cause — see task 5.3.
- **Second-class rule (palm rejection)**: any point arriving while STROKE /
  GESTURE is in flight is blacklisted until it lifts; the one exception is
  the second point arriving while PENDING, which forms a gesture with the
  pending point (delivery-independent, per the GESTURE bullet above) rather
  than being blacklisted; the blacklist survives stroke end, so a finger
  resting nearby when a stroke finishes never wakes up as a fresh touch; a
  third finger never substitutes into a broken pinch pair.

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
path — two-finger horizontal movement pans, it never rotates (the P52
"twist" was gesture non-formation degrading to the rotate fallback, not a
desired behavior). Orbit (task 5.7): `Navigate::rotate(widget, center,
dx, dy)` with the three-point centroid delta, the same mapping the navigate
tool's left-drag uses (navigatetool.cpp:241). v1 zooms about the view-center
reference point (the wheel behavior when no atom is under the cursor);
zoom-toward-fingers is a possible refinement after device testing, not v1
scope.

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

**D9 — Widget-local coordinates and explicit cancel handling.** The state
machine reads scenePosition() in posOf(). In Qt 6 widget delivery
scenePosition() equals globalPosition() (global logical screen coords):
qguiapplication.cpp sets `setScenePosition(ep, tempPt.globalPosition())`,
and the widget path (QApplicationPrivate::updateTouchPointsForWidget,
qapplication.cpp) rewrites ONLY position() =
widget->mapFromGlobal(globalPosition()), never scenePosition() — so
using scenePosition() naively on the embedded GLWidget would offset every
synthesized event by the widget's GLOBAL origin (toolbar/dock chrome plus
the window's on-screen position), not just the window chrome (the unit
tests' top-level fixture masked this, and a (0,0)-positioned parent makes
mapTo(window) and mapToGlobal coincide). The widget-local correction is
therefore applied at the GLWidget::touchEvent dispatch boundary, NOT in
posOf(): each synthesized mouse event position has
this->mapToGlobal(QPoint(0,0)) subtracted before the QMouseEvent is
constructed. This keeps posOf() unit-testable with constructed events —
QEventPoint::position() cannot be set on constructed points
(QMutableEventPoint is private-header-only in qeventpoint_p.h, absent on
the Linux CI build), so reading position() in posOf() would break every
state-machine test. The machine's internal math (slop, pinch zoom delta,
pan centroid delta) is translation-invariant, so global is safe internally;
only the absolute synthesized position needs the boundary correction.
QEvent::TouchCancel ends the touch session:
PENDING → Idle with no events (never a phantom click); STROKE → the
synthesized release is emitted so the tool's stroke ends cleanly, then
Idle; GESTURE → Idle with the camera holding; the blacklist is cleared.

A cleaner end-state — a coordinate-agnostic machine that converts each
delivered QEventPoint into a plain {id, state, QPointF} before TouchState
so the machine never touches QEventPoint — was identified as a follow-up
refactor (it changes the A_EXPORT-exported class signature) but is not
required for this change.

## Risks / Trade-offs

- **Platform variance in touch delivery** (libinput/X11 vs Windows digitizers):
  edge cases like both-fingers-in-one-event and pair breaks are covered by
  the state machine design, but real-device smoke is the only honest check of
  stream timing and slop feel. The slop constant may need tuning from device
  evidence; it is a single named constant. **P52 device pass**: pinch zoom
  failed on the Windows touchscreen (task 5.2) — with the draw tool active a
  pinch drew an atom immediately. The per-finger-grouping hypothesis was
  refuted against Qt 6.6 primary sources (WM_POINTER aggregation; TouchBegin
  only when all points Pressed); the device evidence discriminated ranked
  cause (2) — finger 1 crossed the slop and the immediate press reached the
  draw tool before finger 2 arrived — and the stroke-arming grace window
  (task 5.5, `kTouchStrokeArmMs`) fixes it (the press is held so a second
  finger forms a pinch). **P52 re-test with the widened-scope fix pending**
  (fresh installer run 42656894181 = c5e33bf39, author-confirmed): atoms
  were STILL drawn on pinch with the draw tool active, and two-finger
  horizontal movement appeared to twist the molecule — both are the
  gesture-non-formation signature (the pan wiring is verified correct:
  glwidget.cpp:3509 maps the centroid delta to `Navigate::translate`), so
  the 50ms arming window elapsed while finger 1 was still moving before
  finger 2 landed; task 5.6 widens the window. The twist's exact path (a
  declined press falling through to the navigate-rotate fallback vs. the
  committed stroke's atom-drag perceived as rotation — the draw tool was
  active) is recorded for the re-record to discriminate. The analysis also
  surfaced two evidence-backed defects independent of that outcome —
  scenePosition (global, = globalPosition()) vs position (widget-local)
  coordinate space on a child widget, and unhandled QEvent::TouchCancel —
  filed as task 5.4.
- **Stroke-arming adds a short ink delay (~250ms after task 5.6) before a
  stroke's press is emitted.** This is the deliberate price of the
  pinch-draws-atom fix: the press is held so a second finger arriving in the
  window can form a pinch
  instead of drawing. It is imperceptible for a drawing stroke and only
  affects the single-finger stroke path; the tap/click path is unchanged (a
  quick release below slop is still a click, never held). The constant is
  named and tunable from further device evidence.
- **Cancel-mid-stroke completes the tool gesture (open design conflict, P1).**
  Task 5.4(b) and D9 direct that a QEvent::TouchCancel during STROKE emit the
  synthesized release "so the tool's stroke ends cleanly". But for the erase
  tool a release mid-lasso applies the deletion (erasetool.cpp:134–190), and
  for the draw tool it finishes the bond — which conflicts with the canonical
  erase-tool-interaction / draw-tool-interaction "interrupted gesture leaves
  no residue" contracts ("the pointer interaction is otherwise cut short —
  SHALL leave the molecule unchanged"). The alternative — suppressing the
  release so the tools' abandonment contracts clean up — was assessed and
  rejected for task 16: resetStrokeState() does not release the draw tool's
  force-field markers, and a lingering m_beginAtomId makes the next press
  *continue* the cancelled stroke rather than start fresh (drawtool.cpp:196,
  press handler early-returns on a live stroke id), so suppressing introduces
  a dangling-stroke and marker-lingering regression. Resolution is a design
  decision with real trade-offs on both sides and is deferred to the 5.3
  on-device discrimination (which confirms whether cancels actually occur
  mid-stroke on real hardware) before the change is archived; it is NOT a
  mechanics defect in the diff (reviewer: "OK with notes").
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
