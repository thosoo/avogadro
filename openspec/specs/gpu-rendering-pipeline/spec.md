# gpu-rendering-pipeline Specification

## Purpose

Renders molecules through a tiered GPU pipeline whose default tier targets 60 fps on
protein-scale scenes while keeping the GUI thread responsive, with defined compatibility
tiers for old hardware and a frozen legacy tier, so rendering performance and GUI
responsiveness no longer degrade with scene size.

## Requirements

### Requirement: Tiered rendering backends
The application SHALL provide three rendering tiers — `modern` (shader/instanced, GL 3.3+
compatibility profile), `compat` (batched fixed-function, GL 2.0+), and `legacy` (the
pre-existing immediate-mode renderer) — and SHALL select the highest tier that is compiled
in and supported by the runtime GL context, unless overridden. The selected tier SHALL be
logged at startup with the resolved GL renderer string.

#### Scenario: Capable hardware defaults to modern
- **WHEN** the application starts on a system whose GL context supports GL 3.3 or better
- **THEN** the modern tier is selected and logged without any user action

#### Scenario: Below-floor context falls back
- **WHEN** the runtime context cannot provide GL 3.3 but provides GL 2.0
- **THEN** the compat tier is selected; if GL 2.0 is also unavailable, the legacy tier is
  selected, and rendering still succeeds

#### Scenario: User override
- **WHEN** the environment override `AVOGADRO_RENDERER` is set to `modern`, `compat`, or
  `legacy`
- **THEN** the requested tier is used (or startup fails with a clear diagnostic if it is
  not compiled in or not supported), and the override takes precedence over probing

#### Scenario: macOS uses legacy
- **WHEN** the application is built for macOS
- **THEN** only the compat and legacy tiers are compiled in and the legacy tier is
  selected by default

### Requirement: Modern-tier frame performance
On the modern tier, rendering SHALL sustain the following frame rates during continuous
camera rotation: at least 60 fps for a ~4,000-atom molecule on integrated-graphics-class
hardware; at least 60 fps on discrete and 30 fps on integrated hardware for a ~50,000-atom
assembly; and at least 30 fps on discrete hardware for a ~1,000,000-atom scene viewed
statically or rotated. Submission of an unchanged scene SHALL not issue per-primitive work
proportional to scene size.

#### Scenario: Monomer on integrated GPU
- **WHEN** a ~4,000-atom protein is rotated continuously on integrated-graphics-class
  hardware with the modern tier
- **THEN** the average frame rate is at least 60 fps

#### Scenario: Large assembly
- **WHEN** a ~50,000-atom assembly is rotated continuously
- **THEN** the average frame rate is at least 60 fps on discrete and 30 fps on integrated
  hardware

#### Scenario: Capsid-scale scene
- **WHEN** a ~1,000,000-atom scene is displayed or rotated on discrete hardware
- **THEN** the average frame rate is at least 30 fps

#### Scenario: Static scene submission is bounded
- **WHEN** the scene is unchanged and frames are repeatedly rendered
- **THEN** the number of GL draw calls per frame is bounded by a small constant
  independent of scene size

### Requirement: GUI thread is not blocked by the viewport
On the modern tier, scene tessellation, cache rebuilds, and buffer preparation SHALL run
off the GUI thread; the GUI thread's per-frame work SHALL be limited to submitting ready
GPU buffers plus time-sliced uploads bounded to a small per-frame budget. Scene edits and
rebuilds SHALL NOT block input processing beyond that budget.

#### Scenario: Rotation during heavy scene change
- **WHEN** a large scene is being rebuilt (loaded, edited, or LOD-band changed) while the
  user rotates the view
- **THEN** the GUI keeps responding to input and frames continue to be presented from the
  last ready state during the rebuild

#### Scenario: Editing a large molecule
- **WHEN** the user adds or moves atoms in a large scene with the modern tier
- **THEN** only the affected chunks are rebuilt and interaction stays responsive

### Requirement: Picking correctness at scale
Picking SHALL return the primitive nearest the cursor (or the set within a rubber band)
without silently dropping hits on large scenes. The legacy GL_SELECT path, where still
used (compat and legacy tiers), SHALL detect selection-buffer overflow instead of
truncating silently.

#### Scenario: Click in a large scene
- **WHEN** the user clicks on an atom in a scene far larger than the legacy selection
  buffer capacity
- **THEN** the topmost primitive under the cursor is selected

#### Scenario: Rubber band exceeding legacy capacity
- **WHEN** a rubber-band selection covers more hits than the legacy selection buffer holds
  (on tiers that use GL_SELECT)
- **THEN** the overflow is detected and reported rather than silently mis-selecting

### Requirement: Selection state scalability
Testing whether a primitive is selected SHALL take constant time independent of the
selection size, and rendering SHALL early-out when nothing is selected, so that selecting
the entire scene does not change per-frame rendering cost asymptotically.

#### Scenario: Select-all on a large scene
- **WHEN** the user selects all atoms of a large scene with the modern tier
- **THEN** the frame rate does not degrade relative to having nothing selected

### Requirement: Level-of-detail stability
LOD selection SHALL be tied to discrete zoom bands with hysteresis and SHALL NOT
re-tessellate geometry per frame; at typical viewing distances the rendered detail SHALL
be at least the quality of the pre-existing dynamic-scaling minimum, and LOD band changes
SHOULD be prepared off the GUI thread.

#### Scenario: No LOD churn while rotating
- **WHEN** the user rotates the view without zooming across the LOD hysteresis threshold
- **THEN** no geometry is re-tessellated and the frame rate is unaffected by LOD logic

### Requirement: Visual parity for ported engines
An engine ported to the new emitter API SHALL produce visually equivalent output to its
legacy rendering (same primitives, colors including per-atom custom colors and
selection highlighting, and transparency behavior), and engines that are not yet ported
SHALL continue to render through the compatibility bridge while the modern tier is active.

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

### Requirement: Frozen low tiers
The compat and legacy tiers SHALL provide functional parity for the basics — molecules
render, tools operate, picking works — and SHALL receive bug fixes only; new rendering
features SHALL NOT be added to them, and the legacy tier's pre-existing performance
pathologies are accepted as documented rather than fixed.

#### Scenario: Compat tier basics
- **WHEN** the compat tier is forced on capable hardware via the override
- **THEN** molecules render correctly, tools work, and picking returns correct results

#### Scenario: Legacy tier unchanged
- **WHEN** the legacy tier is active
- **THEN** behavior matches the pre-pipeline renderer, including its known limitations
