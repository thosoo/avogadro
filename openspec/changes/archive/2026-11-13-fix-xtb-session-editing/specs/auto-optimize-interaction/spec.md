# auto-optimize-interaction Specification

## MODIFIED Requirements

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

## ADDED Requirements

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
