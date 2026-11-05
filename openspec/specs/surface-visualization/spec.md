# surface-visualization Specification

## Purpose

Defines the interaction contract of surface (orbital / density / VdW / ESP)
calculation and display: the GUI stays responsive while surfaces are
calculated, memory stays bounded by the visible result, rotating a molecule
with displayed surfaces remains interactive, results are deterministic and
numerically faithful, and background orbital precalculation cannot monopolize
the machine regardless of user quality settings.

## Requirements

### Requirement: GUI stays responsive during surface calculation
While any surface phase runs (grid evaluation, mesh generation, coloring,
data handoff between phases), the application's event loop SHALL remain
responsive: user input SHALL be processed with no deterministic multi-second
freeze caused by that work. Work proportional to the number of grid points or
mesh vertices SHALL NOT execute on the GUI thread. Progress indication SHALL
update at a bounded rate and SHALL NOT flood the event loop.

#### Scenario: Orbital cube calculation runs on a large ORCA output
- **WHEN** an orbital cube calculation runs for a large molecule at high
  resolution and the user interacts with any dialog or menu during it
- **THEN** the UI responds to the interaction within a fraction of a second
  at every point of the calculation, including its start and phase
  transitions

#### Scenario: Phase transitions do not stall the GUI
- **WHEN** a cube calculation finishes and its data is handed to the next
  phase (mesh generation), then when the mesh finishes
- **THEN** neither transition freezes the GUI proportionally to the data size

#### Scenario: ESP coloring does not freeze the GUI
- **WHEN** a surface is colored by electrostatic potential at high vertex
  counts
- **THEN** the coloring runs off the GUI thread and the UI stays responsive
  until it completes

### Requirement: Surface calculation has bounded memory
Peak transient memory of surface generation SHALL be bounded by a small
multiple of the produced result (cube + mesh) size, independent of grid
point count: buffers SHALL NOT be pre-reserved proportional to the whole
grid, and the positive/negative lobes of an orbital surface SHALL NOT
generate concurrently. Grid data SHALL be stored single precision.

#### Scenario: Very-high-resolution orbital surface on a mid-size molecule
- **WHEN** an MO surface is calculated at 0.05 Å spacing on a molecule whose
  padded bounding box exceeds 15 Å
- **THEN** peak process memory stays within a small multiple of the final
  cube + mesh size (observed: no multi-gigabyte transient beyond the result)

#### Scenario: Both lobes of an orbital
- **WHEN** the positive and negative isosurfaces of one orbital are generated
- **THEN** their mesh generation runs one after the other, not at the same
  time

### Requirement: Rotation stays interactive with surfaces displayed
While a camera motion is in progress (rotate, pan, zoom drag), displayed
surfaces SHALL either render at interactive frame rates or render
simplified, and SHALL resume full-quality rendering shortly after motion
ends. The mesh draw path SHALL NOT re-upload mesh geometry from CPU memory
every frame when hardware buffers are available.

#### Scenario: Twisting a molecule with a large orbital surface
- **WHEN** the user rotates a molecule displaying a multi-million-vertex
  surface
- **THEN** frames continue at interactive rates during the drag (full surface
  or simplified surface), and the full surface returns within a short,
  bounded interval after the drag ends

#### Scenario: Surface appearance is unchanged when still
- **WHEN** the camera is not moving
- **THEN** the displayed surface matches the pre-change visual output (same
  geometry, color, translucency), except that backfaces of closed surfaces
  may be culled

### Requirement: Surfaces appear without extended stalls
When a surface becomes ready, presenting it SHALL NOT stall rendering for a
duration proportional to the mesh size in a single unbounded step: geometry
upload SHALL be bounded per presentation cycle or made small enough (via
compaction) that its single-cycle upload cost is negligible.

#### Scenario: Arrival of a very-high-resolution surface
- **WHEN** a surface mesh finishes generating and becomes visible
- **THEN** the application does not freeze for a proportional multi-second
  period at the moment of first display

### Requirement: Screened evaluation preserves results
Distance-based screening of negligible contributions SHALL change computed
surface geometry only within a tight numerical tolerance; the screen SHALL
skip only contributions that are mathematically negligible, and screening
SHALL be deterministic for identical inputs.

#### Scenario: Screened vs unscreened comparison
- **WHEN** the same orbital cube is computed with screening enabled and
  disabled on a small test system
- **THEN** the resulting grids agree within the documented tolerance and the
  generated meshes are equivalent

### Requirement: Surface generation is deterministic and finite
For identical molecule state and calculation settings, surface generation
SHALL produce the same result on every invocation: the same triangle count
and equivalent vertex positions regardless of internal threading, with all
coordinates finite.

#### Scenario: Parallel mesh generation reproduces serial output
- **WHEN** a mesh is generated twice, once with internal threading active and
  once serially (or twice threaded)
- **THEN** triangle counts match, every vertex position matches within
  floating-point equality after ordering, and no NaN/Inf coordinates exist

### Requirement: Background orbital precalculation is policy-bounded
Automatic precalculation of orbitals on document load SHALL run at most at
Medium resolution regardless of any persisted per-user quality preference;
the requested higher quality SHALL apply only when the user explicitly
calculates or recalculates a specific orbital. The precalculation queue
SHALL remain cancellable and SHALL preserve existing stale-callback
protections on document switches.

#### Scenario: Opening an output with Very High persisted as default quality
- **WHEN** a quantum-output file is opened and the persisted default orbital
  quality is Very High
- **THEN** precalculated orbitals are computed at Medium resolution or finer
  than Medium only after an explicit per-orbital request, and the load
  completes in bounded time

#### Scenario: Explicit high-quality recalculation still available
- **WHEN** the user selects a specific orbital and requests Very High
  resolution for it
- **THEN** that orbital is calculated at Very High and displayed

#### Scenario: Document switch during precalculation
- **WHEN** the active document changes while precalculation runs
- **THEN** the existing stale-callback guarantees hold (no cross-document
  writes, no crash), unchanged from current behavior

### Requirement: Orbital surfaces complete regardless of parse threading
When a document whose file carries orbital information is opened and an
orbital surface is requested, the surface SHALL be calculated and become
displayable regardless of whether the file's orbital-data parse runs on the
GUI thread or a background thread. A background parse SHALL NOT prevent
completion notifications, progress indication, or the surface-calculation
queue from operating for the lifetime of the document: once the parse for the
active document has finished, subsequent surface requests SHALL complete
without requiring the document to be closed and reopened, and the GUI SHALL
remain responsive during the parse itself (the responsiveness behavior
established for large orbital outputs is preserved, not traded away).

#### Scenario: ORCA output renders end to end
- **WHEN** an ORCA output file containing orbital data is opened and an
  orbital is selected (or precalculated and selected)
- **THEN** its surface calculation progresses through its stages, reaches
  completion, and the surface is rendered

#### Scenario: Queue drains without a document switch
- **WHEN** an orbital surface calculation completes and the user then
  requests a different orbital from the same document
- **THEN** the second request is calculated and rendered like a fresh
  request; no reopen, tool switch, or document change is needed

#### Scenario: Progress indication keeps flowing
- **WHEN** an orbital surface calculation runs after a background parse
- **THEN** its per-entry progress indication advances during the calculation
  and reaches its final state at completion

#### Scenario: Large orbital output stays responsive
- **WHEN** an orbital output file with a large basis is opened
- **THEN** the interface remains responsive during the file's orbital-data
  parse, as established by the responsiveness behavior this change builds on

#### Scenario: Gaussian fchk renders end to end
- **WHEN** a Gaussian formatted-checkpoint file containing orbital data is
  opened and an orbital is selected
- **THEN** its surface is calculated and rendered exactly as when the parse
  runs on the GUI thread

#### Scenario: Molden file renders end to end
- **WHEN** a molden file containing orbital data is opened and an orbital is
  selected
- **THEN** its surface is calculated and rendered exactly as when the parse
  runs on the GUI thread
