# bundled-openbabel delta spec

## ADDED Requirements

### Requirement: Per-instance force-field constraint state
Force-field constraint state — the constraint set (fixed atoms, ignored atoms,
coordinate and geometric constraints), the temporary fix-atom marker, and the
temporary ignore-atom marker — SHALL be owned per force-field instance rather
than shared process-wide. Two force-field instances in the same process SHALL
NOT be able to change each other's constraint, fix, or ignore state, and
mutation of this state by one thread SHALL NOT affect an optimization running
concurrently on another instance. Per-instance behavior otherwise SHALL match
the historical single-session behavior (fixing an atom during interaction
still prevents the optimizer from moving that atom within that instance).

#### Scenario: Drawing tool stroke does not freeze a concurrent optimization
- **WHEN** an optimization is running on a force-field instance in one thread,
  and an editing tool on another thread temporarily fixes or ignores an atom
  on a different force-field instance during its interaction
- **THEN** the running optimization is unaffected: no atom freezes that the
  editing interaction did not intend to freeze

#### Scenario: Two force-field sessions have independent state
- **WHEN** two force-field instances are used concurrently in one process and
  each sets a temporary fix atom (or one sets a fix atom while the other
  optimizes)
- **THEN** each instance's optimizer honors only its own fix/ignore state

#### Scenario: Single-session drag behavior is preserved
- **WHEN** a user grabs an atom during an optimization on one instance
- **THEN** that instance's optimizer does not move the grabbed atom while the
  grab lasts, and resumes moving it after release

#### Scenario: Pin bump carries the fork change
- **WHEN** the per-instance constraint state change lands in the pinned fork
- **THEN** it reaches this repository as a submodule gitlink bump only, with
  no duplicated commit constants in build or workflow files, per the existing
  pin requirements
