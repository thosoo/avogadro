# draw-tool-interaction Specification

## Purpose

Defines the Draw tool's interaction contract for surviving structural churn
between mouse events (undo/redo of a stroke, hydrogen adjustment at release,
deletion by another tool, or molecule replacement): the tool abandons a stale
interrupted stroke instead of dereferencing atoms or bonds that no longer
exist in the live molecule.

## Requirements

### Requirement: Draw tool tolerates structural churn between interactions
The Draw tool SHALL NOT dereference atoms or bonds that no longer exist in the
live molecule. Structural mutations that occur between two mouse events — undo
or redo of a stroke, hydrogen adjustment at stroke release, deletion via another
tool, or molecule replacement — SHALL leave the tool in a defined state: the
interrupted stroke is treated as abandoned, and the next interaction starts
cleanly. A failed atom or bond lookup SHALL reset the affected tool state, not
crash the application.

#### Scenario: Undo deletes the previous stroke's bond
- **WHEN** the user completes a draw stroke, undoes it (removing the created
  atoms and bonds), and starts a new stroke with the Draw tool still active
- **THEN** no dereference of the removed bond or atoms occurs, and the new
  stroke behaves like a first stroke

#### Scenario: Hydrogen adjustment churns atoms at release
- **WHEN** "adjust hydrogens" is enabled and a stroke's release deletes and
  re-adds hydrogen atoms, then the user immediately starts a new stroke
- **THEN** the tool resolves its recorded references against the live molecule,
  finds any removed ones absent, abandons them, and draws the new stroke
  without crashing

#### Scenario: Molecule replaced mid-stroke
- **WHEN** the active molecule is replaced or cleared while the Draw tool holds
  an interrupted stroke state
- **THEN** the tool abandons the stale state on its next event instead of
  dereferencing atoms of the previous molecule
