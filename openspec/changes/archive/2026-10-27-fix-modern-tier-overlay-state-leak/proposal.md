# Why

On the modern tier, the frame's fixed-function overlays — the axes navigation
glyph, the debug/extension text HUD, and the active tool's overlay paint — are
drawn **after** `ModernRendererBackend::endFrame()`, but `endFrame` leaves GL
state behind that makes those draws render wrongly:

- The sphere and cylinder shader programs bound in `endFrame`
  (modernbackend.cpp:761 and :803) are **never released** — unlike every other
  bind site in the file (348/351, 622/648, 686/715), the endFrame draws leak the
  program to the rest of the frame.
- The per-instance generic vertex attributes (2–5) are left **enabled with
  instance divisors set**, pointing at the instance VBOs.

Everything drawn afterwards is therefore rendered *through the leaked cylinder
program*: its `uMVP`/`uMV` uniforms still hold the molecule's
projection·modelview, and the overlay's per-vertex colors never reach the
shader's (per-instance) color attribute. Observed on the reporting machine
(Quadro P3200, commit `ac487391e`, white background set to make it visible):

- the axes glyph disappears from its corner and reappears as a **black
  ring/chain of small triangles at the screen center, atom-sized, moving with
  the molecule** (stale molecule MVP + unbound color attribute → black);
- the debug HUD text **disappears after the first atom exists** (with an empty
  scene `endFrame` binds nothing — `m_sphereCount > 0` gates the leak).

This is a state-restoration defect, not an engine or content bug; the artifact
is invisible on the default black background, which is why it went unnoticed.

# What Changes

- **`endFrame` restores the GL state it binds.** After the sphere draw and
  after the cylinder/line draw, the bound program is released and the instance
  attributes (2–5) are disabled with their divisors reset to 0 — matching the
  release discipline the file's other bind sites already follow.
- No changes to shaders, geometry, instance layout, engines, or overlays.

# Capabilities

### Modified Capabilities

- `gpu-rendering-pipeline`: new requirement — the modern tier's per-frame draws
  SHALL restore program and vertex-attribute state, so same-frame fixed-function
  overlays and QPainter text render at their intended positions and colors.

# Impact

- `libavogadro/src/rendering/modernbackend.cpp` — `endFrame` only.
- Fixes the displaced black axes blob, the vanishing axes glyph, and the
  disappearing debug text on the modern tier in one state-restoration pass.

# Verification

- Local: build + full ctest green (Linux, `-j8`, default platform).
- Windows: both CI legs green on the fix commit.
- User-machine acceptance: with a white background — axes glyph back in its
  corner (red/green/blue), debug HUD text persists after the first atom, and
  the black triangle ring at the center is gone.
