# surface-visualization Delta

## ADDED Requirements

### Requirement: Orbital status column renders progress accurately
The orbital explorer's status column SHALL render, for every visible
orbital row, a progress bar that fills within its own cell bounds and a
percentage label, reflecting the calculation state reported by the
calculation queue (0% for queued-or-never-calculated orbitals, intermediate
values while calculating, 100% for completed orbitals). Rendering SHALL NOT
depend on which repaint path produced the frame: newly exposed rows drawn
during scrolling SHALL paint the same bar geometry as rows painted at rest,
and no repaint SHALL deposit paint pixels outside the painted cell's rect
on any widget canvas. Scrolling the table SHALL NOT leave paint artifacts
on cells that outlive the repaint that caused them.

#### Scenario: Completed orbitals show 100% bars with labels
- **WHEN** orbitals finish precalculation and their rows are visible
- **THEN** each completed row's status cell renders a filled bar and a
  legible percentage label (not an empty groove)

#### Scenario: Scroll exposes rows without artifacts
- **WHEN** the user scrolls the table through the precalculated region and
  the view repaints newly exposed rows
- **THEN** every painted bar stays inside its status cell's rect, and no
  foreign color fragments remain on any cell after the repaint completes

#### Scenario: Rest and scroll paint identically
- **WHEN** the same completed orbital row is painted at rest and again
  after the table is scrolled
- **THEN** the bar geometry and label are identical in both frames

### Requirement: Orbital table opens at the HOMO row
After a document's orbital data fills the table for the first time, the
orbital explorer SHALL present the table scrolled such that the HOMO row is
visible, regardless of how many rows the table holds or which fill in the
widget's lifetime it is. The scroll SHALL land at the HOMO row on the first
fill after a document load with the same reliability as on later fills.

#### Scenario: First fill centers the HOMO row
- **WHEN** a quantum-output document is loaded and the orbital table is
  populated for the first time in the widget's lifetime
- **THEN** the table's view scrolls to the HOMO row (it is visible in the
  viewport), matching the behavior of later fills

#### Scenario: Reload of a different document also lands on HOMO
- **WHEN** a second document is loaded into the same widget
- **THEN** the table again presents the new document's HOMO row

### Requirement: Progress updates are bounds-safe
The orbital table model SHALL ignore a progress update (range change,
stage increment, value update, completion, queue reset) that names an
orbital with no corresponding row in the current table, instead of writing
through an out-of-range index. The model SHALL emit no data-changed
notification for such an ignored update. Clearing an already-empty table
SHALL be a safe no-op.

#### Scenario: Update for an orbital beyond the table
- **WHEN** a progress update names orbital number N while the table holds
  fewer than N rows
- **THEN** the update is ignored without corrupting memory, and the
  process continues to operate

#### Scenario: Clearing an empty table is a no-op
- **WHEN** the table is cleared while it holds no rows
- **THEN** the model performs no row-removal notification and remains
  consistent
