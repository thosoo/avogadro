## Why

On the modern rendering tier the Phong lighting is computed against surface
normals that were never rotated out of object space, while the two light
directions are fixed view-space constants. The resulting shading error
produces two user-visible defects: the specular highlight visibly slides
across the molecule when the camera rotates (instead of staying fixed relative
to the viewer, as the legacy/compat tiers do), and at certain camera angles an
atom presents its fully-unlit hemisphere to the viewer and reads as a
"roundish black artifact" — a near-black disc where an atom should be. Both
defects have one root cause and one fix.

## What Changes

- The instanced sphere and cylinder vertex shaders transform their normals
  from object space into view space using the model-view matrix (a new
  `uModelView` uniform), so lighting math operates in a single consistent
  space.
- `ModernRendererBackend` gains a `setModelView()` entry point fed from the
  live GL `GL_MODELVIEW` stack alongside the existing composed MVP; the
  lighting constants themselves are unchanged.
- The lighting parity target is made explicit: eye-space fixed-function
  behavior ("headlamp" — the highlight stays put on screen while the molecule
  rotates under it), which is what the legacy tier has always done since its
  lights are fixed at initialize time with an identity modelview.
- No change to tier selection, geometry, picking, or the fixed-function
  lighting state used by the compat/legacy tiers.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `gpu-rendering-pipeline`: the "Visual parity for ported engines" requirement
  gains a sharpened, testable lighting-space requirement: fragment lighting
  SHALL be evaluated with normals and light directions in the same coordinate
  space (view space), so the lit appearance of a fragment is a function of the
  live camera only in the fixed-function way (headlamp parity), and no
  camera angle exists at which an atom renders as a fully unlit (near-black)
  disc.

## Impact

- `libavogadro/src/rendering/shaders.h` — sphere/cylinder vertex shaders gain
  the normal transform; the "uniform scale => normal unchanged" comment is
  corrected.
- `libavogadro/src/rendering/modernbackend.h/.cpp` — new `setModelView()`
  uniform plumbing per program draw (sphere, cylinder); pick programs are
  untouched (no lighting).
- `libavogadro/src/glwidget.cpp` — `renderModern()` call sites that already
  call `setModelViewProjection()` add the companion `setModelView()` call.
- No workflow/build changes; no submodule changes; no compat/legacy tier
  changes.
