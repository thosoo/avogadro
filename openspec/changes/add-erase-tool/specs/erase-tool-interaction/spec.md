## Purpose

Defines the interaction contract of the Erase tool: deletion by lasso (a freehand
loop drawn on the canvas that removes the atoms it encloses) and by single click
(a press-and-release that removes the single atom under the pointer), with one
undo step per gesture, managed-hydrogen parity with the Draw tool's delete, clean
behavior during a running optimization, and a loop overlay that renders on every
rendering tier.

## ADDED Requirements

### Requirement: Lasso deletes enclosed atoms
With the Erase tool active, the user SHALL be able to delete atoms by pressing on
the canvas, dragging a freehand loop, and releasing. At release, every atom whose
position lies inside the closed loop SHALL be deleted, and bonds of deleted atoms
SHALL be removed with them. Bonds with exactly one endpoint inside the loop SHALL
survive. Containment SHALL be evaluated against atom positions at release time.
The loop SHALL NOT be required to close on itself: an unclosed path SHALL be
auto-closed with a straight segment from its end to its start.

#### Scenario: Encircle a methyl group
- **WHEN** the user draws a loop around a methyl group (one carbon, three
  hydrogens) and releases
- **THEN** all four atoms are gone and every bond that referenced them is gone,
  while the rest of the molecule is unchanged

#### Scenario: Bond with one endpoint outside
- **WHEN** the user draws a loop that encloses one endpoint atom of a bond but
  not the other
- **THEN** the enclosed atom is deleted, the bond is removed, and the outside
  atom survives

#### Scenario: Unclosed loop auto-closes
- **WHEN** the user releases the pointer while the loop's end has not returned to
  its start
- **THEN** the path is closed with a straight segment and containment is
  evaluated as if the loop had been closed

#### Scenario: Rest of the molecule is untouched
- **WHEN** a lasso encloses atoms in one region of a larger molecule
- **THEN** atoms and bonds outside the loop are unchanged, including their
  positions, elements, and bond orders

### Requirement: A single click deletes one atom
With the Erase tool active, a press-and-release without significant pointer
travel (a click) SHALL delete the single atom whose projected position lies
nearest the click point and within the atom pick radius, and bonds of the
deleted atom SHALL be removed with it. That atom's attached hydrogens SHALL be
removed according to the managed-hydrogen policy below. A click whose pointer
lies outside the pick radius of every atom (a click on empty space) SHALL
delete nothing. A press-and-release whose travel is at or above the minimum
lasso threshold is a lasso and is governed by the lasso requirement, not this
one: the minimum-path guard therefore distinguishes a click from a lasso, and a
click is never a no-op.

#### Scenario: Click on an atom
- **WHEN** the user presses and releases without significant movement while the
  pointer is over an atom
- **THEN** that atom is deleted, its bonds are removed, and the undo stack gains
  one entry

#### Scenario: Click on empty space
- **WHEN** the user presses and releases without significant movement on empty
  canvas space
- **THEN** the molecule is unchanged and no atom is created

#### Scenario: Click resolves the nearest atom
- **WHEN** the click point lies within the pick radius of one atom but is nearer
  a different atom
- **THEN** the nearer atom is deleted

#### Scenario: Click is not a lasso
- **WHEN** the user presses and releases with travel below the minimum lasso
  threshold over an atom
- **THEN** the gesture is a click and deletes that single atom (plus its
  attached hydrogens), not a lasso

#### Scenario: Click respects managed hydrogens
- **WHEN** adjust-hydrogens is enabled and the user clicks a carbon whose
  hydrogens are managed
- **THEN** the carbon is deleted and its managed hydrogens are removed by the
  hydrogen adjustment, not as independent click victims

#### Scenario: Click with adjust-hydrogens off
- **WHEN** adjust-hydrogens is disabled and the user clicks an atom that has
  explicit bonded hydrogens
- **THEN** the atom and its bonded hydrogens are deleted

### Requirement: One lasso is one undo step
A single completed lasso that deletes N atoms SHALL appear as a single undo step:
one undo restores all N atoms (and their bonds, orders, and hydrogens) exactly as
before the gesture, one redo deletes them again. No molecule mutation SHALL be
applied before the loop is released.

#### Scenario: Undo restores the whole lasso
- **WHEN** a lasso deletes five atoms and the user activates undo once
- **THEN** all five atoms, their bonds, and their hydrogen counts are restored

#### Scenario: Redo re-deletes
- **WHEN** the user redoes the undone lasso
- **THEN** the same five atoms and their bonds are removed again

#### Scenario: Nothing mutates during the drag
- **WHEN** the user is mid-drag drawing a lasso around atoms, before release
- **THEN** the molecule contains the same atoms and bonds it contained at press

### Requirement: Managed-hydrogen policy matches the Draw tool delete
With adjust-hydrogens enabled, the Erase tool SHALL treat managed hydrogens the
same way the Draw tool's delete treats them: managed hydrogens inside the loop
SHALL NOT be deleted as lasso victims, while heavy atoms inside the loop SHALL be
deleted and their hydrogen counts adjusted by the delete. With adjust-hydrogens
disabled, every atom inside the loop is a deletion candidate, hydrogens included.

#### Scenario: Loop around a carbon with managed hydrogens
- **WHEN** adjust-hydrogens is enabled and the user loops a carbon whose
  hydrogens are managed
- **THEN** the carbon is deleted and its managed hydrogens are removed by the
  hydrogen adjustment, not as independent lasso victims

#### Scenario: Adjust-hydrogens disabled
- **WHEN** adjust-hydrogens is disabled and the user loops several atoms
- **THEN** every enclosed atom, hydrogen or not, is deleted

### Requirement: Interrupted lasso leaves no residue
An Erase gesture that does not reach a normal release — the user switches tools
mid-loop, the document changes, or the pointer interaction is otherwise cut
short — SHALL leave the molecule unchanged and the tool free of transient state:
the abandoned loop SHALL be discarded entirely, and the next Erase gesture SHALL
behave as a fresh gesture.

#### Scenario: Tool switch mid-loop
- **WHEN** the user has drawn part of a loop and activates another tool before
  releasing
- **THEN** the molecule is unchanged, no partial deletion is applied, and the
  loop overlay disappears

#### Scenario: Next gesture starts clean
- **WHEN** an Erase gesture was abandoned and the user immediately draws a new
  loop
- **THEN** the new loop starts empty (it contains no points from the abandoned
  path) and deletes only what it itself encloses

### Requirement: Erase serializes with a running optimization
While an auto-optimization session is running, a lasso deletion SHALL be
serialized against the optimizer's molecule access: the erase takes the molecule
write lock when assembling its deletions at release. If the lock cannot be
acquired at release, the gesture SHALL abort with no residue (the same clean
abort the Draw tool's release performs), and atoms deleted by a successful lasso
SHALL NOT leave frozen or ignored atoms behind in the optimization session.

#### Scenario: Lasso during a running AutoOpt session
- **WHEN** an optimization session is running and the user completes a lasso
- **THEN** the deletion is applied safely, the surviving atoms continue to move
  under the optimizer, and no surviving atom becomes frozen or ignored by the
  gesture

#### Scenario: Lock held at release
- **WHEN** the molecule write lock is unavailable at lasso release (for example
  held by the optimization worker for a tick)
- **THEN** the gesture aborts cleanly: nothing is deleted, no transient state
  survives, and the next Erase gesture behaves as a fresh gesture

### Requirement: Loop overlay renders on every tier
While a lasso is being drawn, the path SHALL be visible as a screen-space overlay
on the active rendering tier, on modern, compat, and legacy tiers alike, and the
tool SHALL NOT emit per-frame "painter not active" diagnostics as a result of its
overlay pass.

#### Scenario: Loop visible while dragging
- **WHEN** the user drags a lasso on any rendering tier
- **THEN** the drawn path is visible in the frames presented during the drag

#### Scenario: Overlay gone after release
- **WHEN** a lasso completes and its deletions are applied
- **THEN** the loop overlay no longer renders
