## ADDED Requirements

### Requirement: View-space lighting correctness
On the modern tier, fragment lighting SHALL be evaluated with the surface
normal and the light directions expressed in the same coordinate space (view
space). The lit appearance of a fragment SHALL depend on the live camera in
exactly the fixed-function way established by the legacy tier's eye-space
lights: the highlight SHALL stay fixed relative to the viewer while the scene
rotates under the camera (headlamp parity), and the unlit region of each
primitive SHALL correspondingly stay on the side facing away from the viewer,
never swinging onto geometry facing the viewer. There SHALL BE NO camera pose
at which an atom or bond renders as a fully unlit (ambient-only, near-black)
disc or band.

#### Scenario: Highlight is stable under rotation
- **WHEN** the camera is rotated continuously around a lit molecule on the
  modern tier
- **THEN** the specular highlight and the bright/dark shading pattern stay
  fixed relative to the viewer (the same screen direction throughout the
  rotation), matching the compat/legacy tiers' behavior for the same camera
  path

#### Scenario: No atom renders fully black at any angle
- **WHEN** the camera is swept through a full rotation (both axes) around any
  molecule of ordinary organic elements on the modern tier
- **THEN** no atom is presented as a near-black disc; every atom's visible
  face carries diffuse shading at all camera poses

#### Scenario: Parity with the fixed-function tiers
- **WHEN** the same molecule is rendered by the modern tier and the legacy
  tier from the same camera pose
- **THEN** the shading is visually equivalent: same lit hemisphere direction,
  same highlight placement, same ambient-only regions
