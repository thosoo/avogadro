# auto-optimize-interaction Delta

## MODIFIED Requirements

### Requirement: Optimizer computation is thread-confined
While an optimization session is running — on any backend (OpenBabel force field or
xTB) — only the worker thread SHALL call into the backend's computational state (the
live force-field instance, or the xTB library objects: environment, molecule,
calculator, results). Any state the GUI thread needs from the optimization (energy,
constrained atoms, drag state, coordinates) SHALL be published by the worker as an
immutable snapshot and read by the GUI thread only through that snapshot, under a lock
whose hold time is independent of the optimization work. Tool shutdown or document
teardown SHALL quiesce the worker (stop requesting ticks and wait for the in-flight
tick to finish) before mutating or clearing shared backend state. Worker-side reads of
the live molecule (snapshot copy, topology scan, coordinate read) SHALL hold the
molecule read lock for the duration of the read. No camera-frame or paint path SHALL
block on the worker's per-tick work. An xTB singlepoint evaluation SHALL NOT run on
the GUI thread and SHALL NOT be interrupted mid-evaluation by teardown.

#### Scenario: Idle-running HUD does not crash
- **WHEN** an optimization session runs unattended (HUD and per-atom overlay
  visible, regular repaints) for a prolonged period
- **THEN** the process does not crash and no heap corruption is reported;
  the GUI reads only worker-published snapshots

#### Scenario: Repaint during an in-flight optimization tick
- **WHEN** a repaint reads the published snapshot while the worker is inside backend
  work (force-field `Setup`/stepper, or an xTB singlepoint SCF)
- **THEN** the reader either sees the previous consistent snapshot or the new one, never torn data, and the repaint does not wait for the tick to finish

#### Scenario: Tool switch or document close while running
- **WHEN** the tool is disabled or the molecule is destroyed while an optimization tick is in flight
- **THEN** teardown waits for the in-flight tick before touching backend state, and no mutation of shared backend state overlaps worker execution

#### Scenario: Drawing or deleting during a run
- **WHEN** the user draws a bond or deletes an atom while the optimizer runs
- **THEN** the worker's molecule snapshot and topology scan hold the
  molecule read lock, so no concurrent read/write on live molecule storage
  occurs

#### Scenario: xTB singlepoint never runs on the GUI thread
- **WHEN** an xTB session ticks while the user rotates the camera and drags atoms
- **THEN** every singlepoint evaluation executes on the worker thread, the GUI thread
  only publishes drags and reads snapshots, and the camera stays interactive

### Requirement: Failed setup ends the session deterministically
When the optimizer backend cannot be set up for the current molecule — a force-field
typing failure, an xTB method that does not support the molecule's elements or
structure, or missing parameter data for the selected method — the optimization
session SHALL stop requesting ticks and SHALL surface the failure to the on-screen
status. The application SHALL NOT retry setup on subsequent ticks of the same session,
for any backend.

#### Scenario: Molecule the force field cannot type
- **WHEN** a force-field session's first setup fails for the current molecule and the molecule is left unchanged
- **THEN** the session performs exactly one setup attempt (no unbounded per-tick retries), the HUD reports the failure state, and CPU use returns to idle until the user intervenes

#### Scenario: xTB method does not support the molecule's elements
- **WHEN** an xTB session is started on a molecule containing an element the selected
  xTB method does not support (for example a heavy element under GFN2-xTB)
- **THEN** setup fails exactly once with the library's diagnostic surfaced, no ticks
  are requested, and the session stops deterministically

#### Scenario: GFN-FF selected without parameter data
- **WHEN** the user selects GFN-FF on a system where the GFN-FF parameter file cannot
  be located
- **THEN** setup fails deterministically with a diagnostic naming the missing
  parameter data; GFN1-xTB and GFN2-xTB sessions on the same molecule still start

## ADDED Requirements

### Requirement: One optimization tool with selectable backends
The application SHALL provide a single optimization tool whose computational backend
the user selects: the existing OpenBabel force fields, and the xTB methods GFN1-xTB,
GFN2-xTB, and GFN-FF. Selecting a backend SHALL change only the engine: every
interaction requirement of this capability — drag authority, grab feedback, HUD,
quiesce on teardown, undo serialization, render responsiveness — SHALL hold identically
for every backend. A standalone xTB-only optimization tool SHALL NOT exist; its
function is subsumed by the unified tool. The backend selection SHALL persist across
restarts like the other tool settings.

#### Scenario: Force-field methods remain available
- **WHEN** the unified optimization tool is activated and the user selects a force
  field (for example MMFF94) and starts a session
- **THEN** behavior is unchanged from the pre-merge force-field tool: same setup,
  drag fix markers, HUD counts, and convergence behavior

#### Scenario: GFN2-xTB selected runs a quantum optimization
- **WHEN** the user selects GFN2-xTB and starts a session on a small organic molecule
- **THEN** the molecule's geometry is optimized by quantum singlepoint evaluations
  (energies in the Hartree-derived kJ/mol range displayed in the HUD), with the same
  start/stop/drag interaction as the force-field backend

#### Scenario: Drag semantics identical across backends
- **WHEN** the user grabs and drags an atom, first during a force-field session and
  then during an xTB session
- **THEN** the grabbed atom follows the pointer in both cases, the surrounding
  molecule relaxes around it, and on release the optimizer resumes moving that atom
  from where the drag left it — with no backend-specific behavioral difference

#### Scenario: Backend selection persists across restart
- **WHEN** the user selects GFN2-xTB, closes the application, and starts it again
- **THEN** the optimization tool still has GFN2-xTB selected

### Requirement: xTB availability is discovered at runtime
The application SHALL resolve the xTB library at runtime rather than build time: the
optimization tool SHALL build and ship on every supported platform, and the xTB
backends SHALL be usable exactly when the library is resolvable when the session
starts. When the library is absent, xTB backends SHALL be unavailable with a visible,
logged diagnostic; force-field backends SHALL keep working; and no tool interaction
SHALL crash. Tests that exercise xTB physics SHALL skip — not fail — when the library
is absent from the machine.

#### Scenario: Windows machine without the xTB library
- **WHEN** the application runs on a machine where the bundled or system xTB library
  cannot be resolved and the user opens the optimization tool
- **THEN** the xTB backends are visibly unavailable with a diagnostic, force fields
  still run, and start/stop/drag interactions cause no crash

#### Scenario: Linux machine with the distribution xTB package
- **WHEN** the application runs on a system where the distribution's xTB runtime
  library is installed (for example Ubuntu's `xtb` package providing `libxtb.so.6`)
- **THEN** GFN1-xTB and GFN2-xTB sessions run without any additional installation or
  configuration

#### Scenario: Physics tests skip without the library
- **WHEN** the test suite runs on a machine without the xTB runtime library
- **THEN** the xTB physics tests report skip and the suite is green; the same suite on
  a machine with the library runs the physics assertions

### Requirement: xTB optimization converges deterministically
An xTB optimization session SHALL drive the library's singlepoint evaluations with a
quasi-Newton (L-BFGS) geometry optimizer. The selected convergence level SHALL map to
a fixed, documented gradient-norm threshold and a maximum evaluation count. A session
SHALL stop requesting ticks when the threshold is reached or the maximum count is hit,
and SHALL present the final energy. On a reference molecule (water) converged at the
normal level from a distorted start, the O–H bond lengths and H–O–H angle SHALL land
within documented tolerances of the known gas-phase geometry.

#### Scenario: Water converges to its reference geometry
- **WHEN** a water molecule distorted to a 1.3 Å O–H length is optimized at the
  normal convergence level
- **THEN** the session reaches the convergence threshold within the maximum
  evaluation count and the O–H bond lengths are within 0.1 Å of 0.96 Å and the
  H–O–H angle within 10° of 104.5°

#### Scenario: Converged session goes idle
- **WHEN** an xTB session reaches its convergence threshold
- **THEN** the worker stops evaluating, the displayed energy is stable, and CPU use
  returns to idle while the session stays open

#### Scenario: Maximum evaluation count caps a stubborn session
- **WHEN** a session cannot reach the selected threshold within the maximum
  evaluation count (for example extreme level on a floppy system)
- **THEN** the session stops evaluating at the cap and reports the last energy rather
  than evaluating forever

### Requirement: Interactive performance adapts to interaction state
During an xTB session, evaluation quality SHALL adapt to the user's interaction: while
an atom is being dragged, the backend SHALL evaluate singlepoints at reduced accuracy
with a bounded self-consistent-field iteration count so the pulled structure relaxes
responsively; when the drag ends, the backend SHALL restore its full-quality accuracy
and iteration settings before subsequent ticks. The number of xTB worker threads SHALL
be configurable, SHALL default to an automatic choice derived from the machine's
hardware, and SHALL take effect when a session starts. The GUI thread SHALL remain
responsive — camera rotation and dragging stay fluid — throughout an xTB session,
including sessions on molecules large enough that a single evaluation is slow.

#### Scenario: Drag relaxes responsively, release restores quality
- **WHEN** the user drags an atom in an xTB session on a mid-size molecule and then
  releases it
- **THEN** during the drag the surrounding structure visibly relaxes around the
  pulled atom without multi-second stalls, and after release the session continues
  evaluating at full quality (energies converge per the convergence requirement)

#### Scenario: Thread setting takes effect at session start
- **WHEN** the user configures a specific xTB thread count (or the automatic default
  picks the machine's core count) and starts a session
- **THEN** the session's evaluations use that thread budget, observable in the
  worker's CPU use, without any restart of the application

#### Scenario: Camera stays fluid during a slow evaluation
- **WHEN** an xTB session runs on a molecule whose singlepoint evaluation takes on
  the order of a second and the user rotates the camera during it
- **THEN** rotation stays interactive and the display continues presenting (no blank
  frames, no input freeze), per the thread-confinement and rendering requirements
