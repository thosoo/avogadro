# Proposal: Surface wireframe during camera drags (restore legacy behavior)

## Why

On the modern and compat render tiers, orbital/surface meshes vanish entirely
while the camera is being dragged (`m_cameraInteracting` early-return in
`GLWidget::flushCapturedMeshes`), then reappear ~250 ms after the last move
event. Users experience this as the surface blinking out during every tilt or
rotation of a molecule with a displayed ORCA orbital. The frozen legacy tier
never hid surfaces: its quick-render path drew them as a **wireframe net**
(`SurfaceEngine::renderQuick` forces `drawMesh(mesh, 1)` = `GL_LINE`) while the
camera moved, restoring the solid surface at rest. The canonical
`surface-visualization` requirement ("render at interactive frame rates or
render simplified") describes exactly that net behavior; the modern tier's
hide-then-reappear is the degenerate case that satisfies neither branch.

## What Changes

- **No more hidden surfaces during camera motion.** The modern/compat mesh
  flush keeps the surface visible on every frame of a drag by rendering it
  simplified — wireframe (`drawMesh` mode 1), the same semantics the legacy
  tier's quick-render path has always used — and restores the user's chosen
  render mode when the interaction ends.
- The existing `m_cameraInteracting` flag and 250 ms timer are repurposed:
  their job changes from "skip the mesh draw" to "choose the wireframe draw".
  The flag still clears ~250 ms after the last move event; the solid surface
  returns the moment a frame renders with the flag cleared.
- The stability gate (skip a mesh that is actively being regenerated) and the
  try-lock skip (defer one frame when a worker holds the mesh read lock) are
  unchanged — those are correctness guards, not cost guards.
- Spec-level: the rotation requirement in `surface-visualization` is
  sharpened so surfaces SHALL remain visible during camera motion (simplified
  while moving, full quality when still) — the hide-then-reappear behavior is
  no longer a permitted reading.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `surface-visualization`: "Rotation stays interactive with surfaces
  displayed" is rewritten to require visibility during camera motion —
  displayed surfaces render simplified (wireframe) while the camera moves and
  full-quality when it does not, and SHALL NOT be hidden during the motion.
  The no-per-frame-re-upload clause is unchanged (the VBO cache already
  satisfies it).

## Impact

- **Modified:** `libavogadro/src/glwidget.cpp` — the early return in
  `flushCapturedMeshes()` (~line 1641) becomes a render-mode substitution
  (`m_cameraInteracting ? 1 : inst.mode`); the D10/task-4.3 comments in
  `glwidget.cpp` and `glwidget.h` (~927) are reworded. `drawColorMesh` supports
  mode 1, so ESP-colored surfaces get the same treatment. The compat tier
  inherits the behavior through the shared overlay pass.
- **No changes:** picking, the instanced molecule path, AutoOpt, legacy-tier
  rendering (`renderQuick` already draws the net), the mesh VBO cache, shaders,
  or the OpenBabel pin.
- The one behavioral trade-off: wireframe line rendering during drags is
  cheaper than the skipped draw's alternatives only in fill cost — it replaces
  blended full-surface overdraw with 1-px lines and lighting off, which is
  strictly lighter than the solid draw and infinitely lighter than hiding for
  the user's mental model.
