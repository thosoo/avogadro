# gpu-rendering-pipeline Delta

## ADDED Requirements

### Requirement: Tool overlay paint renders on every tier
The active tool's overlay paint — screen-space text (HUD readouts, measurement
and alignment labels), world-space annotations, and per-atom overlay halos —
SHALL render on the modern and compat tiers with the same content, positions,
and colors as on the legacy tier. The renderer SHALL invoke the tool's paint
pass under a painter state in which its draw calls take effect, and SHALL NOT
leave the tool's overlay output dependent on painter state left over from
another frame. The tool overlay SHALL composite above the molecule geometry,
and the renderer SHALL NOT emit per-frame "painter not active" diagnostics as
a result of the tool paint pass.

#### Scenario: AutoOpt energy readout on the modern tier
- **WHEN** an auto-optimization session is running on the modern tier
- **THEN** the energy line and the fixed/ignored counts render at their legacy
  screen positions, and the per-atom fixed/ignored halos render around the
  constrained atoms

#### Scenario: Bond-centric labels on the modern tier
- **WHEN** the bond-centric tool measures a length or angle on the modern tier
- **THEN** the length and angle labels render at the same positions and colors
  as on the legacy tier

#### Scenario: Overlay composites above the molecule
- **WHEN** a tool overlay (halo or label) occupies screen space overlapping
  molecule geometry on the modern tier
- **THEN** the overlay is visible over the geometry, not hidden beneath it

#### Scenario: Compat tier parity
- **WHEN** the compat tier is forced via the renderer override and a tool with
  an overlay is active
- **THEN** the overlay renders as on the legacy tier

### Requirement: Captured meshes render on the modern tier
A mesh accepted by the scene emitter (`drawMesh` or `drawColorMesh`) SHALL
render on the modern tier: its geometry, orientation mode, and color SHALL
match the legacy tier's output for the same mesh. A mesh that is not yet
stable SHALL be skipped until it becomes stable, and a stable mesh's GPU
upload SHALL happen once per stability/revision change rather than per frame.
Per-vertex colors of a color mesh SHALL be preserved. Transparent meshes SHALL
composite over the opaque instanced geometry with blending, matching the
legacy tier's transparent-pass layering. The renderer's backend interface
SHALL NOT contain an accepted-but-unimplemented mesh entry point: every mesh
the emitter captures is either drawn or reported (once) as omitted.

#### Scenario: ORCA orbital surfaces are visible
- **WHEN** the user selects a calculated molecular orbital on the modern tier
- **THEN** the positive and negative isosurface lobes render with the colors
  and alpha of the legacy tier

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

### Requirement: Unit cells render on the modern tier
A molecule with a unit cell SHALL render its cell box edges and cell axes on
the modern tier with the same geometry as the legacy tier. A molecule without
a unit cell SHALL render without any cell overlay. The unit-cell pass SHALL
NOT alter molecule geometry, tools, or overlays of the frame.

#### Scenario: Crystal file shows the cell on the modern tier
- **WHEN** a file with a unit cell is displayed on the modern tier
- **THEN** the cell edges and cell axes render as on the legacy tier

#### Scenario: Molecular file unaffected
- **WHEN** a molecule without a unit cell is displayed on the modern tier
- **THEN** no cell geometry is drawn and the frame matches the legacy tier's

### Requirement: Render settings are effective or honestly gated
Every render setting the settings dialog exposes — the quality slider and the
fog slider — SHALL, on each compiled-in renderer tier, either demonstrably
affect that tier's rendered output or be disabled in the UI with the reason
visible to the user. A setting SHALL NOT silently apply to one tier and
no-op on another while appearing active in both. The legacy tier's behavior
for these settings SHALL be unchanged.

#### Scenario: Quality slider on the modern tier
- **WHEN** the user changes the quality slider while the modern tier is active
- **THEN** either the tessellation detail of the presented scene changes
  accordingly, or the control is disabled with a visible per-tier reason —
  never a silent no-op

#### Scenario: Fog slider on the modern tier
- **WHEN** the user changes the fog slider while the modern tier is active
- **THEN** either scene depth cueing changes accordingly, or the control is
  disabled with a visible per-tier reason — never a silent no-op

#### Scenario: Legacy tier unchanged
- **WHEN** the legacy tier is active and the user changes the quality or fog
  settings
- **THEN** both take effect exactly as before this change
