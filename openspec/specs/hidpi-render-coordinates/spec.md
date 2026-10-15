# hidpi-render-coordinates Specification

## Purpose

Defines the coordinate-space conventions that the rendering and picking
pipeline must honour on displays whose `devicePixelRatio` is not 1, so that
the rendered frame fills the widget's framebuffer and every consumer that
maps between screen and world coordinates (picking, projection, tools)
agrees with the frame that was actually drawn.

## Requirements

### Requirement: Device-pixel frame viewport
The render submit stage SHALL set the GL viewport to the widget's
framebuffer size in device pixels (`widget size × devicePixelRatioF`),
matching what the resize path establishes, so the rendered scene spans the
full visible widget on displays with `devicePixelRatio != 1`.

#### Scenario: HiDPI frame fills the widget
- **WHEN** a scene is rendered on a display with `devicePixelRatio = 2`
- **THEN** non-background pixels appear in every quadrant of the captured
  framebuffer, including the top-right quadrant

#### Scenario: Standard-density display is unchanged
- **WHEN** a scene is rendered on a display with `devicePixelRatio = 1`
- **THEN** the rendered output is pixel-identical to the pre-change renderer
  output for the same scene and camera

### Requirement: Picking agrees with the rendered frame
The picking path SHALL interpret pointer coordinates in the same
coordinate space as the rendered frame, so that the primitive returned for
a pointer position is the primitive drawn at that position on displays
with `devicePixelRatio != 1`.

#### Scenario: Click selects the drawn atom on HiDPI
- **WHEN** an atom is projected to a framebuffer position and that position
  is picked on a display with `devicePixelRatio = 2`
- **THEN** the pick returns that atom

#### Scenario: Rubber band selects drawn geometry on HiDPI
- **WHEN** a rubber-band rectangle covering a known set of drawn primitives
  is picked on a display with `devicePixelRatio = 2`
- **THEN** the pick returns exactly the primitives drawn inside the band

### Requirement: Projection consumers agree with the rendered frame
The projection helpers that tools use to map between screen and world
coordinates (`project`/`unProject`) and the frame's viewport SHALL use the
same coordinate space, so geometry placed by tools appears where the
pointer interaction indicated, independently of `devicePixelRatio`.

#### Scenario: Fragment insertion lands at the indicated position
- **WHEN** a fragment is inserted bonded to an atom under the pointer on a
  display with `devicePixelRatio = 2`
- **THEN** the new fragment renders bonded to that atom at the indicated
  position, and the camera does not end up inside the new geometry

### Requirement: Render loop releases the molecule read lock
The render entry paths that acquire the molecule read lock SHALL release it
on every exit path, so that writers of the molecule lock are not blocked
indefinitely while frames are being presented.

#### Scenario: Molecule writers proceed during continuous rendering
- **WHEN** frames are rendered continuously on the modern tier and a
  component attempts to acquire the molecule write lock
- **THEN** the writer acquires the lock without hanging
