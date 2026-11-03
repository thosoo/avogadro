# surface-visualization Delta

## ADDED Requirements

### Requirement: Orbital surfaces complete regardless of parse threading
When a document whose file carries orbital information is opened and an
orbital surface is requested, the surface SHALL be calculated and become
displayable regardless of whether the file's orbital-data parse runs on the
GUI thread or a background thread. A background parse SHALL NOT prevent
completion notifications, progress indication, or the surface-calculation
queue from operating for the lifetime of the document: once the parse for the
active document has finished, subsequent surface requests SHALL complete
without requiring the document to be closed and reopened, and the GUI SHALL
remain responsive during the parse itself (the responsiveness behavior
established for large orbital outputs is preserved, not traded away).

#### Scenario: ORCA output renders end to end
- **WHEN** an ORCA output file containing orbital data is opened and an
  orbital is selected (or precalculated and selected)
- **THEN** its surface calculation progresses through its stages, reaches
  completion, and the surface is rendered

#### Scenario: Queue drains without a document switch
- **WHEN** an orbital surface calculation completes and the user then
  requests a different orbital from the same document
- **THEN** the second request is calculated and rendered like a fresh
  request; no reopen, tool switch, or document change is needed

#### Scenario: Progress indication keeps flowing
- **WHEN** an orbital surface calculation runs after a background parse
- **THEN** its per-entry progress indication advances during the calculation
  and reaches its final state at completion

#### Scenario: Large orbital output stays responsive
- **WHEN** an orbital output file with a large basis is opened
- **THEN** the interface remains responsive during the file's orbital-data
  parse, as established by the responsiveness behavior this change builds on

#### Scenario: Gaussian fchk renders end to end
- **WHEN** a Gaussian formatted-checkpoint file containing orbital data is
  opened and an orbital is selected
- **THEN** its surface is calculated and rendered exactly as when the parse
  runs on the GUI thread

#### Scenario: Molden file renders end to end
- **WHEN** a molden file containing orbital data is opened and an orbital is
  selected
- **THEN** its surface is calculated and rendered exactly as when the parse
  runs on the GUI thread
