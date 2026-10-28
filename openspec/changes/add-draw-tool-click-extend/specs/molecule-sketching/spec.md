# molecule-sketching Delta

## Purpose

Defines the DrawTool's molecule-sketching gestures — click and drag semantics
on atoms, bonds, and empty space — so that clicking an atom of the currently
selected element grows the molecule with chemically plausible geometry,
editing remains a single undoable step, and sketching coexists with a
concurrently running optimization session.

## ADDED Requirements

### Requirement: Clicking a same-element atom extends the molecule
While the DrawTool is active, a click on an existing atom (press and release
without significant pointer movement) SHALL create a new atom of the selected
element bonded to the clicked atom, using the tool's currently selected bond
order. This rule SHALL apply to every element: the comparison is between the
clicked atom's element and the selected element, with no carbon-only
special case. When adjust hydrogens is enabled, the gesture SHALL NOT extend
from a hydrogen atom.

#### Scenario: Extend a carbon with carbon selected
- **WHEN** the selected element is carbon and the user clicks a carbon atom
- **THEN** a new carbon atom exists, singly bonded to the clicked carbon
  (or with the currently selected bond order), and both atoms render

#### Scenario: Different element still changes the element
- **WHEN** the selected element is nitrogen and the user clicks a carbon atom
- **THEN** no new atom is created and the clicked atom's element becomes
  nitrogen (existing behavior, unchanged)

#### Scenario: Rule generalizes to other elements
- **WHEN** the selected element is oxygen and the user clicks an oxygen atom
- **THEN** a new oxygen atom bonded to the clicked oxygen is created

#### Scenario: Selected bond order is honored
- **WHEN** the bond order selector is set to 2 and the user clicks a carbon
  atom with carbon selected
- **THEN** the new atom is connected by a double bond

#### Scenario: No extension from managed hydrogens
- **WHEN** adjust hydrogens is enabled and the user clicks a hydrogen atom
  with hydrogen selected
- **THEN** no new atom is created

### Requirement: Placed atoms have plausible geometry
The position of the atom created by a click-extend SHALL be derived from the
clicked atom's bond graph: the bond angle SHALL follow the clicked atom's
hybridization (triple bond or two double bonds → 180°; one double bond →
120°; otherwise → ~109.47°), the bond length SHALL be the sum of the covalent
radii of the clicked atom's element and the new element, and the surrounding
geometry SHALL be computed from heavy-atom neighbors only (explicit hydrogen
atoms do not consume a geometry slot). The computed position SHALL be finite
and deterministic: the same molecule state and camera produce the same
position on every invocation.

#### Scenario: Extending a saturated carbon
- **WHEN** the user click-extends from a carbon whose only heavy neighbor is
  reached by single bonds (explicit hydrogens present or not)
- **THEN** the new bond makes an angle of approximately 109.47° (±2°) with
  the existing heavy-atom bond, not a straight-line continuation

#### Scenario: Extending an alkene carbon
- **WHEN** the user click-extends from a carbon participating in one double
  bond
- **THEN** the new bond makes an angle of approximately 120° (±2°) with the
  existing heavy-atom bond, in a plane consistent with the existing geometry

#### Scenario: Extending an alkyne carbon
- **WHEN** the user click-extends from a carbon participating in a triple
  bond
- **THEN** the new bond is linear (180° ±2°) with the existing bond

#### Scenario: Bond length follows the elements
- **WHEN** the user click-extends from atom of element A with element B
  selected
- **THEN** the new bond length equals the sum of the covalent radii of A and
  B (±0.05 Å)

#### Scenario: Explicit hydrogens do not saturate the geometry
- **WHEN** the clicked atom has hydrogens among its neighbors but few or no
  heavy-atom neighbors
- **THEN** the placement still yields a well-defined direction derived from
  the heavy-atom neighbors (or a stable fallback when there are none), never
  a random or undefined position

#### Scenario: Deterministic and finite
- **WHEN** the same click-extend is performed twice on an identical molecule
  state
- **THEN** the created atom's position is identical both times and all
  coordinates are finite

### Requirement: Click-extend edits behave like drawn bonds
A click-extend SHALL be a single undoable step that removes both the new atom
and the new bond on undo and restores them on redo, with the same
adjust-hydrogens bookkeeping as a bond drawn by dragging: hydrogens are added
or removed so the clicked atom's valence stays consistent.

#### Scenario: Single undo step
- **WHEN** the user performs a click-extend and then activates undo once
- **THEN** both the new atom and the bond disappear in that single step

#### Scenario: Undo restores hydrogens on a saturated atom
- **WHEN** adjust hydrogens is enabled and the user click-extends a methane
  carbon (four explicit hydrogens), then undoes
- **THEN** the molecule is restored to four hydrogens; redoing the step
  leaves three hydrogens plus the new bond

### Requirement: Sketching coexists with a running optimization
When an optimization session is running, a click-extend SHALL leave no
constraint markers behind: the new atom and the clicked atom are free for the
optimizer to move immediately after the gesture ends, and the reported fixed
and ignored atom counts are unchanged by the gesture.

#### Scenario: Extend while AutoOpt runs
- **WHEN** the user performs a click-extend during a running optimization
  session with no intentional constraints
- **THEN** the newly created atom moves under the optimizer within the next
  steps, and the on-screen fixed/ignored counts remain zero after the click

### Requirement: Interaction state is scoped to one gesture
After any DrawTool gesture ends — completed, rejected by a guard, or failed
while a concurrent process holds the molecule lock — transient gesture state
(moving markers, stroke atoms, pending candidates) SHALL NOT survive into
later gestures: a subsequent click or drag SHALL behave as a fresh gesture.
This SHALL hold on every exit path of the tool's event handling.

#### Scenario: Rejected delete leaves no residue
- **WHEN** the user right-clicks a managed hydrogen (adjust hydrogens
  enabled), which the tool refuses to delete, during or after a stroke
- **THEN** the tool's transient state is cleared and the next click behaves
  as a fresh gesture (not swallowed, no stale extension or element change)

#### Scenario: Lock held at release does not wedge the tool
- **WHEN** the molecule lock is unavailable at gesture release (for example
  held briefly by a running optimization)
- **THEN** the tool releases its transient constraint markers and stroke
  state and the next click behaves as a fresh gesture

### Requirement: Sketching works without a force field
The DrawTool SHALL remain functional when no force-field implementation is
available: atom creation, click-extend, bond drawing, and deletion SHALL
work, and the absence of force-field state SHALL be skipped rather than
dereferenced.

#### Scenario: Click-extend with no force field loaded
- **WHEN** no force field is loaded and the user click-extends from an atom
- **THEN** the new atom and bond are created normally, with no crash

### Requirement: Documented gestures match behavior
The DrawTool's user-facing gesture documentation (tool tooltip and any user
help) SHALL describe all supported gestures — click on atom (extend or
element change), click and drag (draw bond), right-click (delete) — and the
documentation SHALL NOT describe fewer gestures than the tool implements.

#### Scenario: Tooltip documents the click gesture
- **WHEN** the user reads the DrawTool's tooltip
- **THEN** it mentions that clicking an atom extends the molecule or changes
  its element, in addition to the drag and delete gestures

### Requirement: Existing gestures are preserved
Apart from the same-element click defined above, all DrawTool gestures SHALL
behave as before: clicking empty space creates a new atom at the pointer,
clicking a bond cycles its bond order, dragging from an atom draws a bond
that follows the pointer, dragging onto an existing atom bonds the two atoms,
and right-clicking deletes the clicked atom or bond.

#### Scenario: Empty-space click still creates an atom
- **WHEN** the user clicks (no drag) on empty space with the DrawTool
- **THEN** a new atom of the selected element is created at the pointer
  position, as before

#### Scenario: Drag from an atom still stretches a bond
- **WHEN** the user presses on an atom and drags to empty space
- **THEN** a new atom follows the pointer bonded to the pressed atom, and the
  click-extend placement is not used

#### Scenario: Bond click still cycles order and right-click still deletes
- **WHEN** the user clicks a bond with the left button, or right-clicks an
  atom
- **THEN** the bond order cycles (1→2→3→1) and the right-clicked atom is
  deleted, exactly as before
