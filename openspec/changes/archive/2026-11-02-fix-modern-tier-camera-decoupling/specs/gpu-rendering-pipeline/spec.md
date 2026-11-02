# gpu-rendering-pipeline Delta

## MODIFIED Requirements

### Requirement: Modern-tier projection transform correctness
The modern tier's model-view-projection matrix (`GLWidget::currentModelViewProjection()`)
SHALL equal the standard `projection · modelview` product as authored by the Camera,
which is the single authority for both matrices. The Camera SHALL author its projection
matrix CPU-side (perspective and orthographic) from its own parameters, without reading
the GL matrix stack; the stack-based upload performed for the fixed-function (compat /
legacy) tiers and the legacy pick path SHALL reproduce exactly the matrices the Camera
authored, so on contexts where the fixed-function stack is live the Camera-authored
product and the stack product coincide. The matrix SHALL NOT be assembled in a way that
transposes or reorders the projection and modelview — in particular, feeding GL
column-major matrices into Qt's `QMatrix4x4` raw-array constructor without correcting
for its row-major read / column-major store convention, which maps the scene through
`(modelview · projection)ᵀ` and distorts it non-uniformly (oval atoms, over-stretched
bonds). The modern tier SHALL render the same scene identically to the compat / legacy
tiers, with correct aspect (round atoms, correct bond lengths), at every
`devicePixelRatio`, including `devicePixelRatio = 1`. On contexts where the
fixed-function stack is absent (GL 3.3 Core profile), the modern tier SHALL still apply
the full camera transform — geometry SHALL NOT render with an identity transform.

#### Scenario: Modern tier projects like the native stack
- **WHEN** a scene is rendered on the modern tier on a context where the fixed-function
  stack is live
- **THEN** every world point projects to the same clip-space position as `gluProject`
  reports for the same point on the live `GL_PROJECTION · GL_MODELVIEW` stack

#### Scenario: Modern tier projects correctly on a Core-only driver
- **WHEN** a scene with an active camera transform is rendered on the modern tier on a
  context where the fixed-function stack is absent (GL 3.3 Core profile)
- **THEN** the presented geometry reflects the camera transform (rotating the camera
  rotates the presented scene), and no scene renders as if the transform were identity

#### Scenario: Camera projection matrix is authored without the stack
- **WHEN** the Camera computes its projection matrix (perspective or orthographic) on
  any context, including a Core-profile context where `glGetDoublev(GL_PROJECTION_MATRIX)`
  is invalid
- **THEN** the computation performs no GL matrix-stack calls, and the resulting matrix
  equals the matrix `gluPerspective` (respectively `glOrtho`) would produce for the same
  parameters

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
