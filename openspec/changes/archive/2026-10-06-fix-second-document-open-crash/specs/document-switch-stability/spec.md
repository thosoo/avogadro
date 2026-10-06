## Purpose

Keeps the application stable when the active document changes: loading a second file — including large quantum-chemistry output files whose orbital and surface computations run in the background — must never crash, corrupt memory, or exhaust address space because of state left behind by the outgoing document.

## ADDED Requirements

### Requirement: Quiesced document switch
When the active document changes, the application SHALL stop or safely detach all background computations owned by the outgoing document (surface/mesh generation, orbital data calculations, file reading) before releasing that document's data. A document switch SHALL NOT delete or release data that a still-running background computation is using.

#### Scenario: Switch while orbital surfaces are still computing
- **WHEN** a large quantum output file is open and its orbital/surface calculations are still running in the background, and the user opens a second file
- **THEN** the switch completes without a crash, background work for the first file is stopped or detached before its data is released, and the second file loads normally

#### Scenario: Switch after all computations finished
- **WHEN** the first file is fully loaded and all its calculations have completed, and the user opens a second file
- **THEN** the switch completes without a crash and the second file's content is displayed

### Requirement: Stale completion callbacks are harmless
A completion or progress notification belonging to the outgoing document SHALL be ignored safely if it arrives after the document switch. It SHALL NOT index removed work-queue entries, dereference released data, or modify the incoming document.

#### Scenario: Callback arrives after its queue was cleared
- **WHEN** a background computation finishes after the document switch has cleared its work queue
- **THEN** the notification is discarded without a crash and without touching the new document

### Requirement: Old document state releasable before new document load
Opening a replacement document in the same window SHALL release the outgoing document's heavy derived data (basis-set matrices, volume grids, surface meshes) before the incoming document's parsing allocates comparable amounts of memory. When the application opens a document in a new window instead, that is acceptable, but the total memory in use SHALL remain within platform limits for the supported file sizes.

#### Scenario: Two large output files opened in sequence
- **WHEN** a large quantum output file is loaded (possibly with precalculated orbitals) and the user opens a second comparably large file
- **THEN** the application does not fail from memory exhaustion: either the outgoing document's derived data was released before the incoming parse, or the two documents' combined memory stays within the process address space

### Requirement: Rendering tolerates missing surfaces
After a document switch, rendering SHALL NOT dereference surface or volume data that does not exist in the active document. Views that referenced the outgoing document's surfaces SHALL render without them (empty or regenerated) rather than crash.

#### Scenario: Displayed orbital followed by a new document
- **WHEN** an orbital surface is displayed for the first document and a second document is opened
- **THEN** the view redraws without crashing; stale surface references are dropped or resolved against the new document only when the data exists

### Requirement: File parsing does not run modal dialogs
Reading a quantum output file SHALL NOT open modal dialogs from inside the parsing loop. Interactive choices that affect parsing (such as open-shell handling) SHALL be made before parsing starts, after parsing ends, or via non-blocking UI.

#### Scenario: Open-shell output file
- **WHEN** the user opens a quantum output file that contains open-shell results
- **THEN** the application does not appear to hang during parsing, and the open-shell question is presented outside the parse path
