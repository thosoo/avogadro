# auto-optimize-interaction delta spec

## Purpose

Defines the interaction contract of the AutoOpt (continuous geometry optimization)
tool: which atoms the optimizer may move, which atoms the user may drag, how the
optimization session behaves under lifecycle changes and concurrent editing, and
how non-moving atoms are made visible — so that atoms never become stuck for
reasons the user did not ask for.

## ADDED Requirements

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
