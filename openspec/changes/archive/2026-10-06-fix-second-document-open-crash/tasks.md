## 1. Epoch-token invalidation (stale callback safety)

- [x] 1.1 Add a generation counter to `OrbitalExtension` (bumped in `setMolecule`), captured in `startCalculation` and compared in `calculateCubeDone`, `calculatePosMeshDone`, `calculateNegMeshDone`, `calculationComplete`, and `updateProgress`; stale callbacks return early without touching `m_runningMutex` (see design N.1 — mutex teardown moves to `setMolecule` or the queue deadlocks). Verify: no unchecked `m_queue[m_currentRunningCalculation]` access possible — code review + switch-during-calculation smoke test.
- [x] 1.2 Bounds-check `m_currentRunningCalculation` in every completion slot before `m_queue[...]` indexing. Verify: grep shows no unchecked `m_queue[m_currentRunningCalculation]` remaining.
- [x] 1.3 Mirror the same guard pattern in `SurfaceExtension` for its `m_basis` watcher connections. Verify: same smoke test with the Surfaces dialog path.

## 2. Quiesce background work on document switch

- [x] 2.1 In `OrbitalExtension::setMolecule`, in order (design N.2): bump generation → disconnect `m_basis->watcher()` and `m_meshGen` → `WaitCursor` → `m_basis->watcher().future().waitForFinished()` if running → `m_meshGen->wait()` if running → then `m_queue.clear()` / `delete m_basis` / `loadBasis()`. Verify: open large ORCA file, start precalculation, open second file — no crash (manual + debug build).
- [x] 2.2 Apply the same quiesce ordering in `SurfaceExtension::setMolecule`. Verify: same scenario exercised through the Surfaces extension.
- [x] 2.3 Busy-cursor during the wait in 2.1/2.2 so a multi-second `MeshGenerator::wait()` does not read as a hang. Verify: switch during large-file precalculation shows WaitCursor.

## 3. Release-before-parse memory ordering

- [x] 3.1 In `MainWindow::loadFile`, disconnect and delete the previous `d->moleculeFile` before assigning the new one; `~MoleculeFile` must join its reader thread first (design N.3 — `ReadFileThread::run` writes through the raw `m_moleculeFile` pointer). Verify: repeated open loops show flat process memory (no per-open growth of the leak).
- [x] 3.2 Give `ReadFileThread` an owner: parent to `MoleculeFile` or `deleteLater` on `finished()`; never leak the thread object. Verify: code review + no Qt "thread still running" warnings on exit.
- [x] 3.3 In `MainWindow::setMolecule` same-window path, flush the outgoing molecule's payload before extensions parse: `blockSignals(true); clear(); blockSignals(false)` then `deleteLater` (design N.3). Verify: second open of a ~5k-basis-function ORCA file peaks below first-open peak ×1.5 in Task Manager (Commit column).

## 4. Render-boundary hardening

- [x] 4.1 Null-guard `SurfaceEngine::setOrbital` (`meshById` result before `->otherMesh()`) and `updateOrbitalCombo` (`cubeById` result before `->cubeType()`); return early instead of dereferencing. Verify: display an orbital, open a second document, rotate/zoom the view — no crash.
- [x] 4.2 Audit remaining engine-side `meshById`/`cubeById` uses for the same pattern; fix any found. Verify: grep audit list in PR description.

## 5. ORCA parser hygiene

- [x] 5.1 Delete the `m_basisFunctions` raw-pointer tree in `ORCAOutput` (destructor or ownership conversion). Verify: valgrind/ASAN or leak counters show no growth across repeated parses of `testfiles/koffein_orca.out`.
- [x] 5.2 Open-shell without the modal: `processLine` records `m_openShell` only; when open-shell, parse the alpha block into `m_MOcoeffs` and the beta block unconditionally into a new `m_MOcoeffsBeta`; `load()` picks via new `setUseBeta(bool)` (default alpha) — the question moves after parsing (design N.5). Verify: opening an open-shell output shows no modal during parse; defaults to alpha orbitals.
- [x] 5.3 Guard `calculateDensityMatrix` (`m_homo` vs `m_numBasisFunctions`) and the trailing-pz `at(idx+1)` swap in the MO reorder. Verify: unit-style parse of a crafted truncated MO block does not read OOB (ASAN build).

## 6. Verification

- [x] 6.1 Reproduction matrix executed on a debug build: (a) large ORCA file → second large ORCA file, same window; (b) same, opened while precalculation is running; (c) small file second; (d) open-shell file second. All pass without crash. Verify: `ctest` suite plus the four manual scenarios; record outcomes.
- [x] 6.2 Cross-check with the reporter's Windows Event Viewer exception code (`0xc0000005` vs `0xe06d7363`) and confirm the dominant defect is among the fixed paths. Verify: note added to this change or the follow-up; design unchanged either way.
