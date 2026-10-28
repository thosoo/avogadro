# gpu-rendering-pipeline Delta

## ADDED Requirements

### Requirement: Pick affordance parity with the legacy pick scene
The modern tier's picking pipeline SHALL offer click affordances at least as forgiving as
the frozen legacy pick scene: the effective pick geometry of a bond SHALL be no smaller
than the legacy pick scene's bond representation (bond radius inflated by the legacy pick
slop, drawn across the bond's full extent), and atoms SHALL keep their legacy slop. A
click that selected a bond on the legacy tier from the same position SHALL NOT resolve to
an unrelated atom on the modern tier; in particular, a click whose nearest surface is an
atom silhouette adjacent to a bond SHALL still allow the user to reach the bond by moving
the cursor within the bond's visual extent.

#### Scenario: Click near an atom end of a bond
- **WHEN** the user clicks within the bond's visual extent, close to an endpoint atom's
  silhouette, on the modern tier
- **THEN** the bond is picked (the hit resolves to the bond, not to the atom), matching
  legacy-tier behavior from the same position

#### Scenario: Click the middle of a bond
- **WHEN** the user clicks the middle of a bond on the modern tier
- **THEN** the bond is picked

### Requirement: Legacy-visible mutations reach the render cache
Every molecule mutation that the legacy tier re-renders implicitly because it redraws from
live data each frame — including bond order changes and element changes — SHALL cause the
modern tier to rebuild or re-emit the affected scene content within one frame of the
mutation's notification. A mutation that changes rendered appearance on the legacy tier
SHALL NOT require a coincidental second mutation (such as unrelated atom add/remove
traffic) to become visible on the modern tier.

#### Scenario: Bond order click with hydrogen adjustment disabled
- **WHEN** the user clicks a bond to cycle its order while "adjust hydrogens" is disabled
  and no optimization session is running
- **THEN** the bond renders with its new order within one frame of the click, without any
  other molecule change occurring

#### Scenario: Element recolor
- **WHEN** the user clicks an atom with a different element selected (element alchemy)
- **THEN** the atom re-renders with the new element's color and radius within one frame

#### Scenario: Drag-over-bond order preview
- **WHEN** the user drags a stroke across an existing bond, temporarily changing its order
- **THEN** the preview order change is visible during the drag and reverts at release
