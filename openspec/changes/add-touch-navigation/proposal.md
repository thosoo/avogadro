# Proposal: add-touch-navigation

## Why

Avogadro is unusable on touch screens today for anything beyond trivial scenes.
The input pipeline (`GLWidget`) consumes only synthesized mouse, wheel, and key
events; a touch user gets single-finger gestures for free through Qt's
touch-to-mouse synthesis (rotate, lasso, draw, tap-to-erase all work), but
**zoom and pan are unreachable**: they need the wheel, right-drag, middle-drag,
or a keyboard modifier, none of which exist on touch. Two-finger gestures are
ignored entirely — Qt synthesizes mouse events from the primary touch point
only, so the second finger is discarded or destabilizes the synthetic stream.
Additionally, no synthesized-parity behavior has ever been verified on real
touch hardware, and the palm/extra-finger hazard (a resting palm is a fresh
press to any tool; a resting finger after a stroke can place a stray atom) is
unhandled.

This change is the second half of a planned two-change split from the
touch-exploration session (2026-11-06): `add-erase-tool` (shipped) removed the
touch dependency for deletion; this change adds the missing input layer. Touch
devices are inherently HiDPI (150–250%), so the scaled-event pipeline this
change builds on is the same one established by `fix-hidpi-viewport-coordinates`.

## What Changes

- `GLWidget` accepts touch input (`WA_AcceptTouchEvents`) and routes it through
  a small gesture state machine — tools never see `QTouchEvent`.
- **One finger** → deferred mouse synthesis: a touch stays PENDING until it
  either moves past a slop radius (→ synthesized mouse press + moves = stroke)
  or releases quickly (→ synthesized press+release = click). Finger jitter
  below the slop never reaches any tool. A stroke's press is held for a short
  arming window so a second finger arriving there forms a pinch instead of
  drawing (the confirmed P52 fix; widened to ~250ms after the fresh-installer
  re-test still drew atoms at 50ms).
- **Two fingers** → camera navigation at the widget level: pinch distance
  change zooms, centroid movement pans — never rotates, so two-finger
  horizontal movement pans rather than twisting. Consumed before tool
  dispatch; tools see nothing. One-finger rotate (tool ignores → navigate
  fallback) is preserved through synthesis.
- **Three fingers** → camera orbit at the widget level: the three-point
  centroid movement rotates the view with the navigate tool's drag mapping,
  regardless of the active tool (user decision pulling rotation into this
  change, 2026-11-10 — the common touch-3D convention: 1 finger = tool
  gesture / rotate fallback, 2 fingers = zoom + pan, 3 fingers = orbit). A
  third finger arriving during a two-finger gesture promotes the pinch to an
  orbit.
- **Extra fingers are second-class**: any touch point that arrives while a
  stroke is in flight is ignored until it lifts, never promoted — this is
  also palm rejection. The exceptions preserve camera gestures: the second
  finger arriving while a touch is still pending (or within the stroke-arming
  window) forms a two-finger gesture with the first, and a committed stroke
  whose partial effect is recovered when the second finger lands late still
  yields the pinch; a third finger during a two-finger gesture promotes it to
  the three-finger orbit.
- No tool is modified: synthesized events carry ordinary mouse semantics, so
  the draw, erase, select, and navigate behavior contracts are preserved
  unchanged.

## Capabilities

### New Capabilities
- `touch-navigation`: the touch input contract — single-finger synthesis
  parity with mouse, two-finger pinch zoom / pan camera navigation, the
  extra-finger (second-class / palm rejection) rule, hover independence, and
  no-regression guarantees for existing mouse and one-finger behavior.

### Modified Capabilities

(none — all existing tool and rendering requirements are preserved by design;
the synthesis layer must satisfy them as-is)

## Impact

- **Code:** `libavogadro/src/glwidget.{h,cpp}` (touch event entry, state
  machine, synthesized-event dispatch, camera navigation), plus a new focused
  test binary (`touchnavigationtest`) in `libavogadro/tests/`.
- **No changes** to any tool (`drawtool`, `erasetool`, `selectrotatetool`,
  …), to the camera/matrix pipeline, or to the rendering tiers.
- **Verification constraint:** CI runners (xvfb Linux, Windows MSVC) have no
  touch stack; the state machine is unit-tested with constructed touch event
  sequences, and real-device behavior is verified in a manual smoke pass on
  the author's HiDPI touch hardware (same deferred-to-hardware pattern the
  HiDPI change used). The same session also resolves the deferred HiDPI
  interaction questions (picking/drawing on that device).
- **MSVC:** new widget code must compile under `-std:c++17 -permissive-`
  (Linux CI does not prove Windows compatibility; the Windows installer
  workflow must be triggered per repo policy).
