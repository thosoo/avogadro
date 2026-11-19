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
accumulate across the session's drags in a way that idles an active drag. A
pointer-authored movement of a dragged atom SHALL register with the worker on the next
loop pass — not merely on the next pacing-timer tick — so the drag latency on a
timer-bound backend is bounded by the evaluation cost rather than gated by the pacing
interval. Tick requests SHALL collapse while a requested evaluation is still in flight,
so a faster tick cadence (or a pointer-driven tick) neither queues unbounded work nor
overruns the worker. The number of xTB worker threads SHALL be configurable, SHALL
default to an automatic choice derived from the machine's hardware, and SHALL take
effect when a session starts. The GUI thread SHALL remain responsive — camera rotation
and dragging stay fluid — throughout an xTB session, including sessions on molecules
large enough that a single evaluation is slow.

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

#### Scenario: Pointer move registers without waiting the pacing interval
- **WHEN** an xTB session runs on a backend whose singlepoint evaluation is much
  cheaper than the pacing interval (a timer-bound backend) and the user moves a
  dragged atom
- **THEN** the movement registers with the worker on the next loop pass rather than on
  the next pacing-timer tick, so the drag latency is bounded by the evaluation cost
  rather than gated by the pacing interval

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
