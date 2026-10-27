# gpu-rendering-pipeline Delta

## ADDED Requirements

### Requirement: Modern tier restores GL program and attribute state after drawing
The modern tier's per-frame draws SHALL restore the GL context state they bind:
shader programs SHALL be released after their draw calls, and the generic vertex
attributes used for instancing SHALL be disabled with their instance divisors
reset after their last use in the frame. Fixed-function draws that run later in
the same frame — the axes navigation glyph, the debug and extension text
overlays, and the active tool's overlay paint — SHALL render at their intended
positions and colors, unaffected by the modern tier's programs or instance
attributes.

#### Scenario: Axes glyph renders in its corner after scene content exists
- **WHEN** the scene contains atoms (so the modern tier's instanced draws run)
  and the axes overlay is enabled
- **THEN** the axes glyph renders in its designated corner with its red, green,
  and blue arrows — not displaced to the scene center and not black

#### Scenario: Debug HUD text persists once the scene is non-empty
- **WHEN** the first atom is added to the previously empty scene
- **THEN** the debug/extension text overlays remain visible in their intended
  positions on subsequent frames

#### Scenario: Tool overlay unaffected by instanced draws
- **WHEN** the active tool paints an overlay in the same frame as modern-tier
  instanced draws
- **THEN** the overlay renders at its intended position and colors, not
  transformed by the modern tier's uniforms or shaded by the modern tier's
  program
