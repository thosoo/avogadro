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
interaction ends or the tool is deactivated, whichever comes first.

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

### Requirement: Force-field access is thread-confined
While an auto-optimization is running, only the worker thread SHALL call into
the live force-field instance. Any state the GUI thread needs from the
optimization (energy, fixed/ignored atoms, fix/ignore markers) SHALL be
published by the worker as an immutable snapshot and read by the GUI thread
only through that snapshot, under a lock whose hold time is independent of
the optimization work. Tool shutdown or document teardown SHALL quiesce the
worker (stop requesting ticks and wait for the in-flight tick to finish)
before mutating or clearing shared force-field state. Worker-side reads of
the live molecule (snapshot copy, topology scan) SHALL hold the molecule read
lock for the duration of the read. No camera-frame or paint path SHALL block
on the optimizer's per-tick work.

#### Scenario: Idle-running HUD does not crash
- **WHEN** an auto-optimization runs unattended (HUD and per-atom overlay
  visible, regular repaints) for a prolonged period
- **THEN** the process does not crash and no heap corruption is reported;
  the GUI reads only worker-published snapshots

#### Scenario: Repaint during an in-flight optimization tick
- **WHEN** a repaint reads the published snapshot while the worker is inside
  `Setup`/stepper work on the live force field
- **THEN** the reader either sees the previous consistent snapshot or the
  new one, never torn data, and the repaint does not wait for the tick to
  finish

#### Scenario: Tool switch or document close while running
- **WHEN** the tool is disabled or the molecule is destroyed while an
  optimization tick is in flight
- **THEN** teardown waits for the in-flight tick before touching
  force-field state, and no mutation of constraint state overlaps worker
  execution

#### Scenario: Drawing or deleting during a run
- **WHEN** the user draws a bond or deletes an atom while the optimizer
  runs
- **THEN** the worker's molecule snapshot and topology scan hold the
  molecule read lock, so no concurrent read/write on live molecule storage
  occurs

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
When the force field cannot be set up for the current molecule (typing failure, unsupported
structure), the optimization session SHALL stop requesting ticks and SHALL surface the
failure to the on-screen status. The application SHALL NOT retry setup on subsequent ticks
of the same session.

#### Scenario: Molecule the force field cannot type
- **WHEN** an AutoOpt session's first setup fails for the current molecule and the molecule
  is left unchanged
- **THEN** the session performs exactly one setup attempt (no unbounded per-tick retries),
  the HUD reports the failure state, and CPU use returns to idle until the user intervenes

### Requirement: Coordinate write-back respects the active drag
When an optimization tick publishes optimized coordinates back to the live molecule, the
atom under an active user drag (the grabbed atom of an in-flight pointer interaction)
SHALL NOT have its position overwritten from the tick's data. The published coordinates
for that atom reflect a snapshot taken before the drag moved it, so writing them back
regresses the atom's position mid-drag; the drag path is the authority for the grabbed
atom's position while the drag lasts. All non-dragged atoms SHALL continue to receive the
optimized coordinates every tick.

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
