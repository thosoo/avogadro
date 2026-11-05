# surface-visualization Delta

## MODIFIED Requirements

### Requirement: Rotation stays interactive with surfaces displayed
While a camera motion is in progress (rotate, pan, zoom drag), a displayed
surface SHALL remain visible on every presented frame: it SHALL render
simplified — as a wireframe rendering of the displayed surface — while the
motion lasts, and SHALL resume full-quality rendering when the motion ends. A
camera interaction SHALL NOT cause the displayed surface to disappear from
the presentation. The only permitted absences are a mesh that is actively
being regenerated (skipped until it stabilizes) and a mesh whose read lock is
unavailable for a frame (skipped for that frame); neither may persist beyond
the condition that caused it. The mesh draw path SHALL NOT re-upload mesh
geometry from CPU memory every frame when hardware buffers are available and
the mesh is unchanged.

#### Scenario: Twisting a molecule with a large orbital surface
- **WHEN** the user rotates a molecule displaying a large orbital surface
- **THEN** the surface remains visible in every frame of the drag — rendered
  as a wireframe net while the camera moves — and returns to the full-quality
  rendering when the motion ends, with no hidden interval after release

#### Scenario: Surface appearance is unchanged when still
- **WHEN** the camera is not moving
- **THEN** the displayed surface matches the pre-change visual output (same
  geometry, color, translucency), except that backfaces of closed surfaces
  may be culled

#### Scenario: Surface never blinks out during rotation
- **WHEN** the user tilts a molecule with an orbital surface displayed
- **THEN** no frame during the tilt presents the molecule without the
  surface: the surface appears either full-quality or wireframe in every
  frame, and no hide-then-reappear interval follows the release

#### Scenario: Simplified rendering is transient
- **WHEN** the camera motion stops
- **THEN** the surface returns to the user's selected render mode (for
  example solid translucent) within a short, bounded interval, without any
  action from the user
