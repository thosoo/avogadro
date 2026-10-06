## Why

Opening a second document after loading a large quantum output file (reproducibly: ORCA `.out`) crashes the application. Static analysis of the open/load path found a family of document-switch lifecycle defects: background work (cube QFutures, mesh-generation threads, file-reading threads) is tied to per-document state by raw pointers and is never quiesced when the active document changes; deferred deletion (`deleteLater`) keeps the old document's multi-hundred-MB payload alive while the new document's parse allocates its own N² matrices; and stale callbacks index cleared queues. Which defect fires first is not yet confirmed (Windows Event Viewer check pending), so the fix must cover the family, not one suspect.

## What Changes

- Quiesce all background work belonging to a document before switching: disconnect/wait `QFutureWatcher` cube calculations, stop/wait/disconnect `MeshGenerator` threads before deleting `BasisSet` or clearing the calculation queue (`OrbitalExtension::setMolecule`, `SurfaceExtension::setMolecule`).
- Invalidate stale completion callbacks with an epoch/generation token and bounds checks, so a callback from the previous document becomes a no-op instead of indexing `m_queue[-1]` or dereferencing freed `m_basis`.
- Reorder document switching to release the old document's heavy state (basis matrices, cubes, meshes) before the new document's parse allocates; fix the double parse (Orbital + Surface extensions each re-parsing the same file) where practical.
- Null-guard primitive lookups that dereference unconditionally: `SurfaceEngine::setOrbital` (`meshById(...)->otherMesh()`), `updateOrbitalCombo` (`cubeById(mesh->cube())->cubeType()`).
- Remove the modal `QMessageBox::exec()` from inside the ORCA parse loop (open-shell prompt) — defer the question out of the parsing path.
- Fix leaks that accumulate per open: parentless `ReadFileThread` and overwritten `MoleculeFile` in `MainWindow::loadFile`, and the `ORCAOutput::m_basisFunctions` raw-pointer tree (empty destructor).

## Capabilities

### New Capabilities
- `document-switch-stability`: behavior contract for switching the active document — background calculations tied to the old document must be quiesced or invalidated, old-document state must be releasable before new-document work allocates, and stale callbacks must be harmless.

### Modified Capabilities

## Impact

- `libavogadro/src/extensions/surfaces/orbitalextension.cpp` / `surfaceextension.cpp` — document-switch lifecycle, queue/epoch handling.
- `libavogadro/src/engines/surfaceengine.cpp` — null-guarded primitive lookups.
- `libavogadro/src/extensions/surfaces/openqube/orca.cpp` — leak fix, dialog removal out of parse path.
- `libavogadro/src/moleculefile.cpp`, `libavogadro/src/readfilethread_p.*` — reader thread ownership/lifetime.
- `avogadro/src/mainwindow.cpp` — `loadFile` cleanup of previous `MoleculeFile`/thread.
- No API changes; no spec-level impact on `bundled-openbabel`.
- Verification aid (not a code change): Windows Event Viewer exception code (`0xc0000005` vs `0xe06d7363`) will confirm which failure class dominates; 32-bit address-space exhaustion ruled out for current win64 builds but should be confirmed once on the reporting machine.
