# auto-optimize-interaction Delta

## ADDED Requirements

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
