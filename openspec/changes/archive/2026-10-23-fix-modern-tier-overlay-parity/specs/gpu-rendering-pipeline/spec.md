# gpu-rendering-pipeline Delta

## MODIFIED Requirements

### Requirement: Visual parity for ported engines
An engine ported to the new emitter API SHALL produce visually equivalent output to its
legacy rendering (same primitives, colors including per-atom custom colors and selection
highlighting, and transparency behavior), and engines that are not yet ported SHALL
continue to render through the compatibility bridge while the modern tier is active. Text
primitives emitted by engines (atom labels, measurements, annotations) SHALL render on
every tier with the same content, position (screen-space or projected world-space), and
color as the legacy tier. Tier-exclusive overlay features — the unit-cell axes overlay and
the debug/text overlays — SHALL function on the modern and compat tiers, not only on the
legacy tier. A Painter implementation SHALL NOT accept a primitive type and silently
discard it: every accepted draw call either renders the primitive or reports the
omission (one-time diagnostic), and the engines affected are documented.

#### Scenario: Default ball-and-stick parity
- **WHEN** the same molecule is rendered by the default engine on the modern tier and on
  the legacy tier
- **THEN** atoms, bonds (including multi-bond splitting), and selection halos are visually
  equivalent

#### Scenario: Unported engine still renders
- **WHEN** an engine that has not been ported to the emitter API is enabled while the
  modern tier is active
- **THEN** it renders through the compatibility bridge without crashing and without
  corrupting the modern-tier passes

#### Scenario: Atom labels on the modern tier
- **WHEN** the LabelEngine is enabled and a molecule is displayed on the modern tier
- **THEN** atom labels render with the same text, position, and color as on the legacy
  tier, and follow their atoms during rotation

#### Scenario: Axes overlay on the modern tier
- **WHEN** "Render Axes" is enabled on the modern tier
- **THEN** the axes overlay renders as on the legacy tier

#### Scenario: Debug HUD on the modern tier
- **WHEN** the debug overlay is enabled on the modern tier
- **THEN** the HUD renders — including the modern-tier draw-call and frame counters that
  the legacy-only path could never display

#### Scenario: Extension text overlay on the modern tier
- **WHEN** an extension adds a text overlay label on the modern tier
- **THEN** the label renders

#### Scenario: No silent primitive loss
- **WHEN** an engine emits a primitive type through the modern-tier emitter that the
  emitter does not implement
- **THEN** the omission is reported once (diagnostic log) and the affected engine is
  documented, instead of the primitive vanishing without trace

#### Scenario: Legacy tier unchanged
- **WHEN** the legacy tier is active
- **THEN** text, axes, and overlays render exactly as before, without double-drawing
