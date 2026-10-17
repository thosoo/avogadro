## ADDED Requirements

### Requirement: Modern-tier projection transform correctness
The modern tier's model-view-projection matrix (`GLWidget::currentModelViewProjection()`)
SHALL equal the standard `projection · modelview` product of the live GL stack,
so the modern tier projects geometry to exactly the same clip space as the
native fixed-function (compat / legacy) path. The matrix SHALL NOT be
assembled in a way that transposes or reorders the projection and modelview —
in particular, feeding GL column-major matrices into Qt's `QMatrix4x4`
raw-array constructor without correcting for its row-major read / column-major
store convention, which maps the scene through `(modelview · projection)ᵀ`
and distorts it non-uniformly (oval atoms, over-stretched bonds). The modern
tier SHALL render the same scene identically to the compat / legacy tiers,
with correct aspect (round atoms, correct bond lengths), at every
`devicePixelRatio`, including `devicePixelRatio = 1`.

#### Scenario: Modern tier projects like the native stack
- **WHEN** a scene is rendered on the modern tier
- **THEN** every world point projects to the same clip-space position as
  `gluProject` reports for the same point on the live `GL_PROJECTION ·
  GL_MODELVIEW` stack

#### Scenario: Modern tier is not distorted at standard density
- **WHEN** a scene is rendered at `devicePixelRatio = 1`
- **THEN** atoms render as round discs and bond lengths match the compat /
  legacy tier output for the same scene and camera

#### Scenario: Modern tier is not distorted at HiDPI
- **WHEN** a scene is rendered at `devicePixelRatio != 1`
- **THEN** atoms render as round discs and bond lengths match the compat /
  legacy tier output, with no non-uniform distortion

#### Scenario: Modern and compat agree on the same scene
- **WHEN** the same molecule is rendered on the modern tier and on the compat
  tier
- **THEN** the projected atom positions and bond lengths are equivalent (the
  modern tier's `uMVP` matches the compat tier's native `projection · modelview`)
