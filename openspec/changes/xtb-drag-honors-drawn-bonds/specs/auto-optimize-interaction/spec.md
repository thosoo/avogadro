## ADDED Requirements

### Requirement: A drag cannot change the drawn bond graph
While an optimization session runs on an xTB backend (GFN1-xTB, GFN2-xTB, or GFN-FF) and the user
drags an atom, the molecule's drawn bond graph SHALL be an invariant of the drag-phase
optimization: every drawn bond SHALL be held by a restoring interaction anchored to the bond as
drawn (pulling its two atoms toward the drawn length with a force that grows with stretch), and
every pair of atoms that is NOT bonded SHALL be kept from entering new-bond range by a repulsive
floor interaction, both for the duration of the drag and both removed when the drag ends. No drawn
bond SHALL be severed and no new bond SHALL form: each atom's set of bond partners SHALL remain
exactly its drawn partner set while the drag lasts, so atoms never change bond partners through a
drag. The interactions SHALL act only through the session's own optimization — the dragged atom
SHALL continue to track the pointer exactly (drag authority is unchanged), no atom position SHALL
be teleported, and the session's failure containment, strain visibility, and drag-release
re-baselining requirements SHALL apply unchanged. Interaction targets SHALL be deterministic for
identical molecule state: the same geometry and the same grabbed atom produce the same targets on
every invocation. A held pull deforms the molecule toward the drawn bonds instead of pushing any
bond past re-formation reach or letting new contacts bond, so neither the dissociated basin nor a
partner swap is reachable through dragging. The drag SHALL feel like a force-field drag: the
neighborhood follows the pull, bonds hold, and release retracts. This requirement SHALL hold
identically on every backend: the force-field backends already satisfy it structurally through
their fixed connectivity (harmonic bond terms and non-bonded repulsion).

#### Scenario: Hard pull keeps the molecule connected
- **WHEN** the user drags an atom far beyond its bonded distance and holds the pointer there while
  the session evaluates
- **THEN** the partner atoms of the dragged atom's bonds move toward the dragged atom under the
  session's regular evaluations (the neighborhood follows the pull), the pulled bond's distance
  decreases over successive evaluations while the pull is held, and every drawn bond in the
  molecule stays within its re-formation reach

#### Scenario: Hard pull cannot form a new bond
- **WHEN** the user hard-pulls an atom so that atoms not bonded to each other are driven toward
  one another (for example pulling a carbon of ethane far away, which can swing its hydrogens
  toward each other)
- **THEN** no non-bonded pair enters new-bond range (no hydrogen molecule splits off): each
  atom's nearest neighbors remain its drawn bond partners, the session keeps evaluating, and no
  failure latch occurs from the pull alone

#### Scenario: Release contracts the bond back into bonded range
- **WHEN** the user releases a drag that left a stretched incident bond, starting from a released
  geometry the method can converge
- **THEN** the released atom rejoins the optimizer's coverage, and that bond's distance returns
  within the bonded range within a bounded number of evaluations

#### Scenario: Grabbing a previously broken bond begins repair
- **WHEN** the user grabs an atom whose incident bond already exceeds the strain threshold
  (1.3x the sum of the covalent radii) and moves it
- **THEN** the restoring interaction for that bond uses a target capped at the sum of the covalent
  radii, so the optimization pulls the bond back toward bonded range instead of holding the broken
  distance, and the capped target is deterministic for the same molecule state

#### Scenario: Drag authority survives extreme pulls
- **WHEN** the user drags an atom with several incident bonds to an arbitrary pointer position
- **THEN** the dragged atom's position equals the pointer position every tick it moves, unaffected
  by the restoring and floor interactions (which act only on the other atoms)

#### Scenario: Isolated atom drags without a bond tether
- **WHEN** the user drags an atom that has no bonds during a running xTB session
- **THEN** no bond-restoring interaction acts on the dragged atom (it has no drawn bonds to
  tether), the atom tracks the pointer, the molecule's other drawn bonds keep the same tethered
  treatment every drag gets, and the floor interaction still prevents the dragged atom from being
  driven into new-bond range of another atom

#### Scenario: Bond removed mid-drag leaves no stale tether
- **WHEN** a bond incident to the dragged atom is deleted (or the topology otherwise changes) while
  a drag is active
- **THEN** the session's one-time topology re-establishment clears or rebuilds the restoring
  interaction state for the new bond graph exactly once, and no removed bond remains tethered

#### Scenario: Force-field backend drag is unchanged
- **WHEN** the same hard-pull drag is performed during a force-field optimization session
- **THEN** the behavior matches the pre-existing force-field drag (the harmonic bond terms keep the
  drawn bond connected and retract it at release), and no regression is introduced

#### Scenario: Headless session without a live molecule evaluates safely
- **WHEN** an xTB session runs from a headless coordinate set with no live molecule source and a
  drag index is set
- **THEN** the session evaluates without any restoring interaction and does not crash
