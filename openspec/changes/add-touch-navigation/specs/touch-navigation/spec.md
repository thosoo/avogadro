## Purpose

Defines the touch-input contract of the molecule view: how touchscreen
interaction reaches tools and the camera, so that every gesture a mouse user
has is available to a touch user (through synthesized mouse events for tools,
through two-finger gestures for zoom and pan), and no touch artifact — palm,
resting finger, stray tap — can mutate the molecule or the camera outside a
deliberate gesture.

## ADDED Requirements

### Requirement: Single-finger touch reaches tools as synthesized mouse events
The application SHALL accept single-finger touch input and deliver it to the
active tool as ordinary scaled mouse events, preserving each tool's existing
press/move/release semantics. A touch that has not yet moved beyond a small
slop radius SHALL be held in a pending state in which no tool receives any
event; pointer jitter below the slop radius SHALL NOT reach any tool. A quick
release below the slop radius SHALL be delivered to tools as a synthesized
press-and-release pair (a click) at the touch position. Travel at or above the
slop radius SHALL be delivered as a synthesized press at the traveled-to
position followed by move events and a final release. Synthesized event
positions SHALL be expressed in the canvas widget's local coordinates, so
the pipeline is correct wherever the canvas is embedded in a window.

#### Scenario: Tap on an atom behaves like a mouse click
- **WHEN** the user taps (touch and quick release without significant travel)
  on an atom with the draw tool active
- **THEN** the tool performs its click semantics exactly as for a mouse click
  (element change or click-extend), with no duplicate or phantom events

#### Scenario: One-finger stroke draws a bond
- **WHEN** the user touches an atom and drags with one finger, with the draw
  tool active
- **THEN** the tool receives a press followed by move events and draws a bond
  following the finger, exactly as for a mouse drag

#### Scenario: One-finger lasso erases
- **WHEN** the user draws a freehand loop with one finger with the erase tool
  active
- **THEN** the erase tool receives ordinary mouse events and the lasso deletes
  the enclosed atoms at release, exactly as for a mouse lasso

#### Scenario: Resting-finger jitter is swallowed
- **WHEN** the user rests a finger on the canvas without intending a gesture
  and the contact point wobbles by less than the slop radius
- **THEN** no tool receives any event and the molecule is unchanged

#### Scenario: Touch session cancelled while pending
- **WHEN** the platform cancels the touch session (a TouchCancel event)
  while a single touch is pending
- **THEN** no tool event is synthesized (no phantom click) and the molecule
  is unchanged

#### Scenario: Touch coordinates are consistent with the rendered frame
- **WHEN** any single-finger gesture is performed on a display with
  `devicePixelRatio != 1`
- **THEN** the synthesized events land on the rendered geometry the finger is
  over (picking, stroke placement, and lasso containment agree with what is
  visually under the finger), per the hidpi-render-coordinates invariant

#### Scenario: Canvas embedded in a window with chrome
- **WHEN** the canvas is a child widget offset from the window origin
  (toolbars or docks above it) and the user taps or drags on the canvas
- **THEN** the synthesized events use the canvas widget's local coordinates:
  picking, stroke placement, and lasso containment agree with what is
  visually under the finger, not shifted by the window chrome

### Requirement: One-finger rotate is preserved when no tool claims the gesture
When the active tool does not claim a single-finger gesture, the fallback
camera navigation SHALL remain available through the synthesized events: a
one-finger drag rotates the molecule exactly as a mouse drag does today, on
every rendering tier.

#### Scenario: One-finger rotate with the navigate tool active
- **WHEN** the user drags with one finger while the navigation tool is active
- **THEN** the camera orbits following the finger, as with a mouse drag

#### Scenario: One-finger rotate with an editing tool active on empty behavior
- **WHEN** the active tool's press handler declines a synthesized press and
  the user continues dragging
- **THEN** the drag rotates the molecule via the navigation fallback, matching
  the existing mouse-decline behavior

### Requirement: Two fingers zoom and pan the camera
A two-finger gesture SHALL zoom and pan the camera at the widget level,
before any tool receives events: changing the distance between the two
touch points SHALL zoom (matching the wheel zoom direction and magnitude
convention), and moving the two-point centroid SHALL pan. Neither the zoom
nor the pan SHALL reach any tool as input, and the gesture SHALL work
regardless of which tool is active. The two fingers may arrive grouped into
touch events in any way the platform chooses — a touch event already
carrying both points, updates carrying both points, or the second finger's
arrival delivered as its own touch-begin carrying only that point — and the
gesture SHALL form in every grouping. While a two-finger gesture is in
flight, the touch points that compose it SHALL be tracked by identity — the
gesture ends when either of the two original points lifts, and the camera
stays where the gesture left it. A single finger that has just crossed the
slop radius (so a stroke would otherwise begin) SHALL hold its synthesized
press for a short arming window: a second finger arriving within that window
SHALL still form the two-finger gesture, and no tool event SHALL have been
delivered for the abandoned stroke — so a pinch never draws an atom or
creates a bond through an editing tool.

#### Scenario: Pinch to zoom
- **WHEN** the user spreads two fingers apart on the canvas
- **THEN** the camera zooms in proportionally to the spread, in the same
  direction as mouse-wheel zoom-out-to-in, and no tool receives events

#### Scenario: Pinch with an editing tool active
- **WHEN** the user pinch-zooms or two-finger-pans while a molecule-editing
  tool (for example the draw tool) is active
- **THEN** the camera zooms/pans and the molecule is unchanged: the gesture
  reaches no tool regardless of the active tool

#### Scenario: Second finger arrives in its own touch-begin
- **WHEN** one finger is pending on the canvas and the platform delivers the
  second finger's arrival as a separate touch-begin carrying only that new
  point, and the user then spreads or pans the two fingers
- **THEN** the interaction is a two-finger gesture: the camera responds to
  the spread/pan, no tool event is synthesized, and the originally pending
  finger is not dropped or treated as a fresh touch

#### Scenario: Early stroke replaced by a pinch (editing tool active)
- **WHEN** the user places one finger on the canvas and it crosses the small
  slop radius (so a stroke would begin), and a second finger arrives within a
  short arming window, and the user then spreads or pans the two fingers
  while an editing tool (for example the draw tool) is active
- **THEN** the interaction is a two-finger gesture: the camera responds to
  the spread/pan, no tool event is synthesized for the abandoned stroke (no
  atom is drawn, no bond is created), and the molecule is unchanged

#### Scenario: Two-finger pan
- **WHEN** the user moves two fingers together across the canvas without
  changing their separation
- **THEN** the camera pans following the centroid movement, and no tool
  receives events

#### Scenario: Mixed pinch-and-drag
- **WHEN** the user spreads two fingers while moving them in one continuous
  gesture
- **THEN** zoom and pan respond together smoothly, without camera jumps

#### Scenario: Gesture end leaves the camera
- **WHEN** either finger of an in-flight two-finger gesture lifts
- **THEN** the gesture ends, the camera stays at the pose the gesture left,
  and lifting does not produce any tool event

#### Scenario: Touch session cancelled mid-gesture
- **WHEN** the platform cancels the touch session (a TouchCancel event)
  while a two-finger gesture is in flight
- **THEN** the gesture ends, the camera stays where the gesture left it, and
  no tool receives any event

#### Scenario: Camera navigation during an auto-optimization session
- **WHEN** a two-finger gesture drives the camera while an auto-optimization
  session ticks
- **THEN** the display remains continuously visible (no cleared frames), per
  the existing no-blank-frame requirement

### Requirement: Extra fingers are second-class
Only a fresh touch may start a single-finger interaction. Any touch point
that arrives while a stroke or a two-finger gesture is already in flight
SHALL be ignored until it lifts, and SHALL NEVER be promoted to a new
interaction, including after the in-flight interaction ends. The one
exception is the second touch point arriving while a single touch is merely
pending (no tool event sent yet): that point SHALL form a two-finger gesture
with the pending point — however the platform groups the two points into
touch events — rather than be treated as second-class; every further point
beyond the gesture pair SHALL be ignored until it lifts. A touch event that
begins with more than one point SHALL go directly to the two-finger gesture.
A broken pinch pair SHALL NOT be replenished: a third finger present when
one of the pair lifts SHALL NOT substitute into the gesture or start a new
one.

#### Scenario: Palm lands mid-stroke
- **WHEN** the user's palm (or any extra finger) touches the canvas while a
  drawing stroke is in flight
- **THEN** the stroke continues unaffected and the extra contact produces no
  event in any tool

#### Scenario: Resting finger never wakes up as a new touch
- **WHEN** a finger rests on the canvas while the user finishes a stroke with
  another finger, and then the resting finger lifts
- **THEN** nothing happens: no atom is placed, no click is synthesized, no
  camera motion occurs

#### Scenario: Both fingers land together
- **WHEN** a touch event begins with two contact points already present
- **THEN** the interaction is a two-finger gesture from the start, with no
  pending single-touch state and no synthesized tool events

#### Scenario: Third finger does not substitute into a pinch
- **WHEN** one finger of a two-finger gesture lifts while a third finger is
  already resting on the canvas
- **THEN** the gesture ends and the third finger remains ignored until it
  lifts

### Requirement: Touch interaction does not depend on hover
No touch gesture SHALL require a prior hover state: every tool gesture and
camera navigation reachable with a mouse press SHALL be reachable with a
fresh touch on a display where no pointer hover precedes the touch, with
identical results.

#### Scenario: Fresh tap without hover
- **WHEN** the user taps an atom without any prior pointer position over it
  (no hover has ever occurred at that location)
- **THEN** the gesture behaves identically to the same gesture performed
  after hovering

### Requirement: Mouse, wheel, and keyboard input are unchanged
Adding touch input SHALL NOT alter the behavior of mouse, wheel, or keyboard
input for any tool or camera navigation: all existing interaction
requirements (draw-tool, erase-tool, molecule-sketching, auto-optimize
interaction) SHALL hold verbatim when the input arrives from a mouse.

#### Scenario: Mouse regression guard
- **WHEN** the same molecule operations are performed with a mouse before and
  after the touch pipeline is added
- **THEN** tool behavior, undo grouping, picking, and camera navigation are
  unchanged

#### Scenario: Existing tool test suites still pass
- **WHEN** the full ctest suite (draw, erase, select, autoopt, hidpi viewport
  tests) runs on the touch-enabled build
- **THEN** every previously passing test still passes
