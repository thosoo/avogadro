# gpu-rendering-pipeline Delta

## MODIFIED Requirements

### Requirement: Captured meshes render on the modern tier
A mesh accepted by the scene emitter (`drawMesh` or `drawColorMesh`) SHALL
render on the modern tier: its geometry, orientation mode, and color SHALL
match the legacy tier's output for the same mesh. A mesh SHALL be considered
renderable whenever its vertex and normal counts agree, regardless of its
per-vertex color count: a mesh with zero per-vertex colors (the shape the
mesh generator produces for orbital, VdW, and density surfaces) SHALL render
with its instance/solid color exactly as the legacy tier renders it, and the
emitter SHALL NOT reject or drop such a mesh on account of its empty color
array. A mesh that is not yet stable SHALL be skipped until it becomes
stable, and a stable mesh's GPU upload SHALL happen once per
stability/revision change rather than per frame. Per-vertex colors of a color
mesh SHALL be preserved. Transparent meshes SHALL composite over the opaque
instanced geometry with blending, matching the legacy tier's transparent-pass
layering. The renderer's backend interface SHALL NOT contain an
accepted-but-unimplemented mesh entry point: every mesh the emitter captures
is either drawn or reported (once) as omitted, and the emitter SHALL NOT
silently discard a mesh whose geometry is internally consistent — an
uncapturable mesh SHALL be reported (once) with a diagnostic naming the
reason.

#### Scenario: ORCA orbital surfaces are visible
- **WHEN** the user selects a calculated molecular orbital on the modern tier
- **THEN** the positive and negative isosurface lobes render with the colors
  and alpha of the legacy tier

#### Scenario: Uncolored mesh renders with its instance color
- **WHEN** a mesh with vertices and normals but zero per-vertex colors (a
  plain `MeshGenerator` product) is displayed on the modern tier
- **THEN** the surface renders with the solid color the engine assigned to
  it, matching the legacy tier's output for the same mesh, and no mesh is
  silently dropped at scene capture

#### Scenario: ESP-colored mesh keeps per-vertex colors
- **WHEN** a mesh with per-vertex colors is displayed on the modern tier
- **THEN** the surface shows the per-vertex color gradient as on the legacy tier

#### Scenario: Isovalue change updates the surface
- **WHEN** the user changes the isovalue and the mesh is regenerated
- **THEN** the modern tier presents the regenerated surface within one scene
  build, without stale geometry persisting

#### Scenario: Unstable mesh does not flash stale or partial geometry
- **WHEN** a mesh is being regenerated (unstable) on the modern tier
- **THEN** the previous stable surface is not redrawn as if current, and no
  partial geometry is presented
