# auto-optimize-interaction Delta

## ADDED Requirements

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
