# gpu-rendering-pipeline Delta

## ADDED Requirements

### Requirement: Pick hits identify the clicked primitive
A pick hit returned by the modern tier SHALL carry the rendered primitive's own
index as its name, so tools resolve exactly the primitive the user clicked. The
name SHALL NOT depend on how many instances or segments the primitive was drawn
as: a bond drawn as several cylinder half-segments SHALL resolve to that bond's
index from a click on any of its segments, and this SHALL hold regardless of
which path produced the frame (synchronous engine emission or worker snapshot
tessellation). Atoms SHALL keep their current click behavior (an atom hit
resolves the clicked atom).

#### Scenario: Clicking the middle bond of a multi-bond molecule
- **WHEN** a molecule with at least three bonds is rendered on the modern tier
  and the user clicks the middle of the second bond
- **THEN** the pick hit's name is that bond's index (not 0)

#### Scenario: Clicking the first and last bonds
- **WHEN** the same multi-bond molecule is rendered and the user clicks the
  first and then the last bond
- **THEN** the pick hits' names are respectively the first and the last
  bond's indices

#### Scenario: Every segment of a split bond resolves to its bond
- **WHEN** a double bond is rendered as two offset half-segments per end and
  the user clicks each visible segment in turn
- **THEN** every hit resolves to the same bond's index

#### Scenario: Worker-rendered frames pick the same bonds
- **WHEN** the scene was produced by the worker snapshot tessellation path and
  the user clicks a non-first bond
- **THEN** the pick hit's name is that bond's index, matching the
  synchronous path for the same scene

## MODIFIED Requirements

### Requirement: Modern tier restores GL program and attribute state after drawing
The modern tier's per-frame draws SHALL restore the GL context state they bind:
shader programs SHALL be released after their draw calls, the generic vertex
attributes used for instancing SHALL be disabled with their instance divisors
reset after their last use in the frame, and the GL buffer bindings the tier
binds (including `GL_ARRAY_BUFFER`) SHALL be restored to their prior values —
in particular, no modern-tier instance buffer SHALL remain bound after the
frame's draw work or after a pick pass. Fixed-function draws that run later in
the same frame — the axes navigation glyph, the debug and extension text
overlays, and the active tool's overlay paint — SHALL render at their intended
positions and colors, unaffected by the modern tier's programs, instance
attributes, or buffer bindings.

#### Scenario: Axes glyph renders in its corner after scene content exists
- **WHEN** the scene contains atoms (so the modern tier's instanced draws run)
  and the axes overlay is enabled
- **THEN** the axes glyph renders in its designated corner with its red, green,
  and blue arrows — not displaced to the scene center and not black — and the
  axes' cylinder shafts are visible, not only the sphere and cone tips

#### Scenario: Debug HUD text persists once the scene is non-empty
- **WHEN** the first atom is added to the previously empty scene
- **THEN** the debug/extension text overlays remain visible in their intended
  positions on subsequent frames

#### Scenario: Tool overlay unaffected by instanced draws
- **WHEN** the active tool paints an overlay in the same frame as modern-tier
  instanced draws
- **THEN** the overlay renders at its intended position and colors, not
  transformed by the modern tier's uniforms or shaded by the modern tier's program

#### Scenario: No instance buffer stays bound after a frame or a pick
- **WHEN** a modern-tier frame has been drawn (including instance uploads) or a
  pick pass has completed
- **THEN** `GL_ARRAY_BUFFER_BINDING` is 0 on the current context, so later
  fixed-function or display-list draws cannot misinterpret client pointers as
  buffer offsets
