## ADDED Requirements

### Requirement: A drag cannot break an existing bond
While an optimization session runs on an xTB backend (GFN1-xTB, GFN2-xTB, or GFN-FF) and the user
drags an atom, each bond incident to the dragged atom SHALL be held by a restoring interaction that
is anchored to the bond as drawn, for the duration of the drag: the interaction SHALL pull the
bonded partner atoms toward the dragged atom with a force that grows with the bond's stretch, and
SHALL be removed when the drag ends. The interaction SHALL act only through the session's own
optimization — the dragged atom SHALL continue to track the pointer exactly (drag authority is
unchanged), no atom position SHALL be teleported, and the session's failure containment, strain
visibility, and drag-release re-baselining requirements SHALL apply unchanged. Tether targets SHALL
be deterministic for identical molecule state: the same geometry and the same grabbed atom produce
the same targets on every invocation. The pull SHALL NOT sever a drawn bond: a held pull deforms the
molecule toward the drawn bonds instead of pushing an incident bond past re-formation reach, so the
dissociated-basin failure mode is unreachable through dragging. This requirement SHALL hold
identically on every backend: the force-field backends already satisfy it structurally through their
harmonic bond terms.

#### Scenario: Hard pull keeps the molecule connected
- **WHEN** the user drags an atom far beyond its bonded distance and holds the pointer there while
  the session evaluates
- **THEN** the partner atoms of the dragged atom's bonds move toward the dragged atom under the
  session's regular evaluations (the neighborhood follows the pull), and the pulled bond's distance
  decreases over successive evaluations while the pull is held

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
  by the restoring interaction (which acts only on the partners)

#### Scenario: Isolated atom drags without bonds
- **WHEN** the user drags an atom that has no bonds during a running xTB session
- **THEN** no restoring interaction is applied, the atom tracks the pointer, and the session behaves
  as before this change

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
