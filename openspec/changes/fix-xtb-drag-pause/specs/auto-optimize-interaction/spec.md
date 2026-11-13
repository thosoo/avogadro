## MODIFIED Requirements

### Requirement: Interactive performance adapts to interaction state
During an xTB session, evaluation quality SHALL adapt to the user's interaction: while
an atom is being dragged, the backend SHALL evaluate singlepoints at reduced accuracy
with a bounded self-consistent-field iteration count so the pulled structure relaxes
responsively; when the drag ends, the backend SHALL restore its full-quality accuracy
and iteration settings before subsequent ticks. While a drag is in flight, the session
SHALL NOT enter an idle state: neither the convergence idle (the convergence threshold
being reached on the drag-masked gradient) nor the evaluation-cap idle SHALL stop
per-tick evaluation while the pointer holds or moves the atom. The converged idle
state SHALL be deferred until the drag ends, and the evaluation counter SHALL NOT
accumulate across the session's drags in a way that idles an active drag. The number
of xTB worker threads SHALL be configurable, SHALL default to an automatic choice
derived from the machine's hardware, and SHALL take effect when a session starts. The
GUI thread SHALL remain responsive — camera rotation and dragging stay fluid —
throughout an xTB session, including sessions on molecules large enough that a single
evaluation is slow.

#### Scenario: Drag relaxes responsively, release restores quality
- **WHEN** the user drags an atom in an xTB session on a mid-size molecule and then
  releases it
- **THEN** during the drag the surrounding structure visibly relaxes around the
  pulled atom without multi-second stalls, and after release the session continues
  evaluating at full quality (energies converge per the convergence requirement)

#### Scenario: Holding a stretched pull does not idle the session
- **WHEN** the user grabs an atom in a running xTB session, pulls it to a stretched
  position, and holds it there for longer than the surrounding structure needs to
  relax (the convergence threshold is reached on the free atoms)
- **THEN** the session does not stop evaluating while the drag is still active: the
  free atoms keep responding to subsequent pointer movement within a bounded number
  of ticks, and no converged idle is declared until the drag ends

#### Scenario: A long continuous drag past the evaluation cap keeps evaluating
- **WHEN** a single continuous drag lasts longer than the session's evaluation-cap
  budget would cover
- **THEN** the drag does not freeze partway through: evaluation continues for the
  remainder of the drag, and the cap idles the session only after the drag ends

#### Scenario: Release converges normally after an active drag
- **WHEN** the user releases a drag on a geometry that is genuinely at equilibrium
- **THEN** the session reaches the converged idle state as usual after release, so
  the deferred idle does not change where the session ends up

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

## ADDED Requirements

### Requirement: Drag release re-baselines with an elevated first evaluation
When a drag is released and the session starts a fresh optimization phase from the
released geometry, the first evaluation of that phase SHALL be attempted once with an
elevated self-consistent-field iteration budget relative to the steady-state tick
budget, so a strained but recoverable released geometry converges into the session
instead of latching an evaluation failure. The elevated budget applies to the release
evaluation only; it is bounded and deterministic. If the release evaluation fails even
at the elevated budget, the failed-evaluation containment requirement applies
unchanged: the phase latches deterministically, publishes nothing, and the on-screen
failure diagnostic SHALL name the recovery path (starting a new session, or an edit
that re-establishes the session).

#### Scenario: Release at a strained geometry recovers into the session
- **WHEN** the user releases a drag that left a stretched-but-bonded geometry
- **THEN** the fresh phase's first evaluation converges under the elevated budget and
  the session continues optimizing from the released geometry

#### Scenario: Release at a dissociated geometry is contained with a named recovery
- **WHEN** the user releases a drag that left a dissociated geometry whose
  singlepoint evaluation cannot converge even at the elevated budget
- **THEN** the phase latches deterministically exactly once, no garbage coordinates
  reach the molecule, and the failure diagnostic names the recovery path instead of
  leaving a silent frozen session

### Requirement: Drag-time bond strain is visible
While an atom is being dragged during an xTB session, the on-screen status SHALL
report when the dragged atom's distance to every atom it is bonded to in the molecule
exceeds the range within which that bond is chemically meaningful, so the divergence
between the molecule's persistent bond records and the optimizer's distance-based
geometry is visible to the user while it develops. The report SHALL be scoped to the
active drag and SHALL clear when the drag ends or when the distance returns within
range.

#### Scenario: Pulling a bond beyond bonding distance is named while dragging
- **WHEN** the user drags an atom so that its distance to a bonded partner exceeds
  bonding range while the drag is in flight
- **THEN** the on-screen status reports the strained bond (naming the dragged atom)
  during the drag

#### Scenario: The strain report clears with the drag
- **WHEN** the drag ends (or the dragged atom is returned within bonding range while
  still dragging)
- **THEN** the strain report is no longer shown
