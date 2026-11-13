# auto-optimize-interaction Specification

## Purpose

Defines the interaction contract of the AutoOpt (continuous geometry optimization)
tool: which atoms the optimizer may move, which atoms the user may drag, how the
optimization session behaves under lifecycle changes and concurrent editing, and
how non-moving atoms are made visible — so that atoms never become stuck for
reasons the user did not ask for.

## Requirements

### Requirement: Only intentional constraints freeze atoms
During a running AutoOpt session, every atom SHALL be movable by the optimizer
and draggable by the user unless it is constrained by an intentional,
user-visible constraint of the current session (an explicitly fixed atom, an
explicitly ignored atom, or the atom currently being dragged). The tool SHALL
NOT freeze atoms as a side effect of past sessions, other tools, previous
molecules, or internal bookkeeping.

#### Scenario: Fresh session on a previously contaminated molecule
- **WHEN** a session of the optimization tool is started on a molecule whose
  earlier editing or optimization history left fixed/ignored state behind
- **THEN** all atoms respond to the optimizer and can be dragged, except atoms
  of an intentional constraint of this session

#### Scenario: Session restart clears frozen atoms
- **WHEN** the optimization tool is stopped and started again on the same
  molecule
- **THEN** no atom remains frozen that was not intentionally constrained in the
  new session

### Requirement: Optimizer exclusion reflects current topology
The set of atoms excluded from force-field evaluation SHALL be derived from the
current molecule state. An atom that qualifies for exclusion (for example, an
atom without a real element) SHALL be excluded only while it qualifies, and
exclusion state SHALL NOT persist after the qualifying condition ends.

#### Scenario: Atom gains a real element
- **WHEN** an atom that was excluded from force-field evaluation is assigned a
  real element while optimization is running
- **THEN** the optimizer includes that atom in subsequent optimization steps

#### Scenario: Exclusion never accumulates across edits
- **WHEN** atoms are added and removed while an optimization session runs for a
  prolonged period
- **THEN** the internal exclusion state remains bounded and no atom other than
  currently-qualifying atoms is excluded

### Requirement: Concurrent editing does not poison the optimization
Editing the molecule with another tool while an optimization session runs
SHALL NOT leave the optimization in a state where atoms are frozen outside of
intentional constraints. Any temporary freeze applied by an editing tool for
the duration of one interaction (for example, anchoring an atom while a bond is
being drawn from it) SHALL be scoped to that interaction and released when the
interaction ends or the tool is deactivated, whichever comes first. This SHALL
hold identically for every optimization backend: on the xTB backends, a
structural or positional edit SHALL NOT permanently silence the per-tick
coordinate write-back, and previously optimized atoms SHALL NOT stop moving
because an edit changed the molecule the session was built from.

#### Scenario: Draw a bond while optimization runs
- **WHEN** the user draws a new bond from an existing atom while optimization
  is running, then completes the stroke
- **THEN** the anchored atom and the newly created atom move freely again once
  the stroke ends, in the same optimization session

#### Scenario: Tool switched away mid-stroke
- **WHEN** the user switches the active tool in the middle of a drawing stroke
- **THEN** any temporary anchor/exclusion from that stroke is released and the
  optimizer is not left with stale state

#### Scenario: Atom deleted while optimization runs
- **WHEN** the user deletes an atom while optimization is running, then
  continues editing
- **THEN** no atom that survives the deletion becomes frozen as a result of the
  deletion

#### Scenario: Draw an atom during a running xTB session
- **WHEN** an xTB session is running and the user adds an atom (by drawing, by
  click-extend, or by clicking empty space) with the draw tool
- **THEN** the write-back keeps updating the molecule after the gesture, the
  new atom participates in the optimization within a bounded number of ticks,
  and atoms that were moving before the gesture keep moving

#### Scenario: Undo during a running xTB session
- **WHEN** an xTB session is running and the user undoes or redoes an edit
  that restores a previously deleted atom or removes a drawn one
- **THEN** the molecule does not snap back to pre-edit coordinates and the
  session continues optimizing from the restored geometry

### Requirement: Grab feedback distinguishes blocked grabs
When the user presses on an atom during a running optimization session, the
tool SHALL provide visible feedback when the grab is accepted. If the grab is
rejected because of an intentional constraint, the rejection SHALL be visually
distinguishable from an accepted grab.

#### Scenario: Grab of a freely movable atom
- **WHEN** the user presses on an atom that the optimizer is allowed to move
- **THEN** the atom is highlighted while pressed and follows the pointer

#### Scenario: Grab of an intentionally constrained atom
- **WHEN** the user presses on an atom of an intentional constraint that is
  configured as non-draggable
- **THEN** the press does not grab the atom and the feedback differs from the
  accepted-grab feedback

### Requirement: Non-moving atoms are visible
During a running optimization session, the tool SHALL visibly distinguish
atoms the optimizer will not move (fixed or ignored), and the on-screen
status SHALL report the number of fixed and ignored atoms separately. The
reported counts SHALL reflect the current constraint state and SHALL NOT grow
over time while molecule and constraints are unchanged.

#### Scenario: HUD counts are accurate and stable
- **WHEN** an optimization session runs with no intentional constraints and no
  currently-qualifying excluded atoms
- **THEN** the on-screen status reports zero fixed and zero ignored atoms, and
  the counts remain stable over time

#### Scenario: Fixed atoms are highlighted
- **WHEN** an atom is intentionally fixed during an optimization session
- **THEN** the atom is rendered with a distinguishing overlay so the user can
  see why it does not move

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

### Requirement: GUI mutations serialize with worker reads
Every mutation of the live molecule performed on the GUI thread SHALL hold the molecule
write lock for the duration of the mutation — including mutations performed by undo/redo
command bodies when a command is applied to the undo stack. This SHALL hold regardless of
which surface initiates the command (canvas mouse/keyboard handlers, main-window menu
actions, or extension code). The AutoOpt worker's read lock SHALL be sufficient to exclude
every GUI-side mutation, so the worker's per-tick molecule read (snapshot copy, topology
scan) never observes a torn or half-applied structural mutation and never dereferences an
atom freed by a concurrent deletion.

#### Scenario: Stroke release with hydrogen churn during a run
- **WHEN** an AutoOpt session is running and the user completes a draw stroke that adjusts
  hydrogens (removing and re-adding atoms) at release
- **THEN** the release's structural mutation is serialized against the worker's tick reads:
  the process does not crash and no atom freed by the stroke is dereferenced by the worker

#### Scenario: Command applied from a non-canvas surface during a run
- **WHEN** an AutoOpt session is running and a command is applied to the undo stack from a
  menu action or extension path (not a canvas mouse handler)
- **THEN** the mutation is equally serialized with the worker's reads

#### Scenario: Quiesce while a mutation lock is held
- **WHEN** the session is stopped or aborted while the GUI holds the write lock inside a
  command application
- **THEN** the quiesce (stop requesting ticks and wait for the in-flight tick) completes
  without deadlock, because the worker does not block indefinitely on a read acquisition
  that the stop request has superseded

### Requirement: Drag fix/ignore markers reference the intended atom
The temporary fix and ignore markers that editing tools place on the force field during a
stroke SHALL identify the atom the user actually grabbed or created, under the same
indexing convention the force field uses for its own fix/ignore state (1-based). A marker
placed on the first atom of the molecule SHALL be distinguishable from "no marker".

#### Scenario: Stroke lands on the first atom
- **WHEN** the user completes a draw stroke onto the atom with index 0 of the molecule
- **THEN** the marker references that atom, not the "no marker" sentinel

#### Scenario: Stroke lands on any other atom
- **WHEN** the user completes a draw stroke onto the atom with index N (N > 0)
- **THEN** the marker references atom N, not atom N−1

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

### Requirement: Failed evaluations are contained deterministically
When an xTB singlepoint evaluation fails — the library reports an error for the
evaluation, or it returns non-finite energy, gradient, or coordinates — the
session SHALL NOT publish that tick's coordinates and the write-back SHALL NOT
apply them to the live molecule. The session SHALL stop requesting further
evaluations for the current phase and SHALL surface the failure in the on-screen
status with the library's diagnostic (or a non-finite-result diagnostic). The
application SHALL NOT crash, present corrupt geometry, or retry the failed
evaluation on subsequent ticks of the same phase; starting a new session (Stop
and Start, or a live-molecule edit that re-establishes the session) is the
recovery path.

#### Scenario: SCF failure mid-session
- **WHEN** a self-consistent-field evaluation fails during a running session
  (for example, after dragging a bond apart into a dissociated geometry)
- **THEN** the molecule does not receive garbage coordinates, the failure is
  named in the on-screen status, and CPU use returns to idle for the phase
  instead of evaluating forever

#### Scenario: Non-finite result never reaches the molecule
- **WHEN** an evaluation returns non-finite energy, gradient, or coordinates
- **THEN** no published snapshot carries them and no write-back applies them to
  the live molecule

#### Scenario: Session restarts cleanly after a contained failure
- **WHEN** the user stops and starts a session after a contained evaluation
  failure (or an edit re-establishes the session)
- **THEN** the new phase evaluates normally from the current molecule

### Requirement: Coordinate write-back respects the active drag
When an optimization tick publishes optimized coordinates back to the live molecule, the
atom under an active user drag (the grabbed atom of an in-flight pointer interaction)
SHALL NOT have its position overwritten from the tick's data. The published coordinates
for that atom reflect a snapshot taken before the drag moved it, so writing them back
regresses the atom's position mid-drag; the drag path is the authority for the grabbed
atom's position while the drag lasts. All non-dragged atoms SHALL continue to receive the
optimized coordinates every tick. A drag marker recorded before the pointer has authored
a position is an incomplete drag: the session SHALL treat it as safe state, not as a
position source — pressing on an atom during a running session SHALL NOT crash or
corrupt memory whether or not the pointer subsequently moves.

#### Scenario: Pull an atom while the optimization runs
- **WHEN** the user grabs an atom and moves the pointer while the optimization session
  ticks
- **THEN** the atom tracks the pointer (following the drag path only), and its position is
  never reset toward a pre-drag snapshot position

#### Scenario: Drag ends
- **WHEN** the user releases the atom
- **THEN** the tick write-back resumes covering that atom (subject to the session's
  fix/constraint state), starting from the position the drag left it at

#### Scenario: Other atoms keep updating during a drag
- **WHEN** the user drags one atom while the optimization moves the rest of the molecule
- **THEN** the non-dragged atoms continue receiving optimized coordinates at the regular
  tick rate

#### Scenario: Press lands before the first pointer move
- **WHEN** the user presses on an atom during a running optimization session and one or
  more session ticks fire before the pointer produces its first movement (including a
  press-and-release click with no movement at all)
- **THEN** the process does not crash and no out-of-bounds or null memory access occurs;
  ticks that fire in this window evaluate the molecule without pinning the grabbed atom
  to a position that does not exist yet, and once the pointer moves the grabbed atom
  tracks it normally

### Requirement: xTB sessions integrate live molecule edits
While an xTB optimization session is running, the session's working geometry
SHALL track the current live molecule: on every tick the worker SHALL read the
live molecule under the same bounded, quiesce-safe read-lock discipline the
force-field backend uses, and SHALL NOT rely on a coordinate snapshot captured
before the session started. A topology change during the session (atom added,
removed, or its element changed) SHALL cause exactly one deterministic
worker-side re-establishment of the xTB session state — bounded work on the
worker thread, not unbounded per-tick retries — after which the new topology
participates in the optimization. A position-only change authored outside the
session (another tool moving atoms, an undo, a drag by another surface) SHALL
be adopted as the starting geometry of a fresh optimization phase and SHALL
NOT be reverted by subsequent write-backs. Sessions SHALL NOT evaluate a
molecule whose topology no longer matches the live document.

#### Scenario: Atom added mid-session joins the optimization
- **WHEN** the user draws a new atom while an xTB session runs and the session
  continues
- **THEN** within a bounded number of ticks the session evaluates the molecule
  with the new atom included, and the write-back moves the new atom together
  with the rest

#### Scenario: Atom deleted mid-session
- **WHEN** the user erases an atom while an xTB session runs
- **THEN** the session re-establishes on the reduced topology exactly once and
  the surviving atoms keep receiving optimized coordinates

#### Scenario: Element change mid-session
- **WHEN** the user changes an atom's element while an xTB session runs
- **THEN** subsequent evaluations use the new element, and the session does not
  keep optimizing the old element's Hamiltonian invisibly

#### Scenario: External position edit starts a fresh phase
- **WHEN** another tool moves atoms while an xTB session runs (including after
  the session had converged)
- **THEN** the moved positions are adopted, the optimizer starts a fresh phase
  from them, and the next write-back does not restore pre-edit coordinates

#### Scenario: Edit storm during a stroke stays bounded
- **WHEN** the user performs a continuous draw stroke (or a sequence of quick
  edits) while an xTB session runs
- **THEN** the session performs bounded re-establishment work per actual
  topology change, the GUI thread does not block on the worker's
  re-establishment, and the display does not present cleared frames

### Requirement: AutoOpt tool interactions are safe in every tool-lifetime state
The AutoOpt tool SHALL NOT dereference view or force-field state that has not been
established yet. Starting, stopping, painting, and mouse interaction SHALL behave as
defined below regardless of the order in which the tool is activated, the canvas is
touched, the first frame is presented, and a force field becomes available. On
renderers that degrade honestly by skipping the tool's overlay pass, the tool SHALL
remain fully operable: the overlay pass is a presentation detail and SHALL NOT be a
prerequisite for starting or running a session. When no force field implementation is
available, the tool SHALL be inert rather than crashing: canvas interaction and
repaints SHALL NOT dereference absent force-field state.

#### Scenario: Start pressed before the first painted frame
- **WHEN** the user activates the AutoOpt tool and presses Start without ever moving
  or clicking the pointer over the molecule view
- **THEN** the process does not crash: the tool either starts the session normally or
  declines with a visible/logged diagnostic, and a subsequent attempt after touching
  the view behaves as a normal session

#### Scenario: Tool works on renderers that skip the tool overlay
- **WHEN** the application runs on a context where the renderer degrades honestly by
  skipping the active tool's overlay pass (for example a Core-profile-only GL stack)
- **THEN** activating AutoOpt and starting, running, and stopping a session work with
  no crash; only the tool's on-screen overlay (energy readout, halos) is absent

#### Scenario: Mouse events over the view with no force field available
- **WHEN** no force field implementation can be loaded and the user activates the
  AutoOpt tool and presses or releases the mouse over the molecule view
- **THEN** the process does not crash and the tool performs no force-field bookkeeping

#### Scenario: Repaint with no force field available
- **WHEN** no force field implementation can be loaded and the view repaints while the
  AutoOpt tool is active
- **THEN** the frame presents without a crash and without force-field reporting

#### Scenario: Stop is safe when no session ever started
- **WHEN** the user stops (or the tool is deactivated) without a session having been
  started
- **THEN** the tool tears down without a crash and leaves no force-field markers behind

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
