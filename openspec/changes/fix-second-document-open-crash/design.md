## Context

Static analysis of the document-open path (see proposal Why) found the following concrete defect sites:

- `orbitalextension.cpp` — `setMolecule()` clears `m_queue` and sets `m_currentRunningCalculation = -1` and deletes `m_basis`, but never disconnects `m_basis->watcher()` or stops `m_meshGen`; completion slots do `calcInfo *info = &m_queue[m_currentRunningCalculation]` unchecked.
- `surfaceextension.cpp` — same pattern with its own `m_basis` and watcher connections.
- `surfaceengine.cpp:288` — `m_meshById(...)` result derefenced without null check (`->otherMesh()`); `updateOrbitalCombo` derefs `cubeById(mesh->cube())->cubeType()`.
- `openqube/orca.cpp` — open-shell `QMessageBox::exec()` inside `processLine` (nested event loop during parse); `m_basisFunctions` raw-pointer tree leaked (empty destructor); `calculateDensityMatrix` unguarded `m_homo` column reads and trailing-pz swap OOB.
- `moleculefile.cpp` / `readfilethread_p.*` — `ReadFileThread` parentless, never joined or deleted; `MainWindow::loadFile` overwrites `d->moleculeFile`, leaking the previous `MoleculeFile`.
- `mainwindow.cpp` — `deleteLater` on the old molecule defers release of its cubes/meshes until after the new document's parse (extensions re-parse the file synchronously inside `setMolecule`).
- Memory profile: parse peak ≈ 3·N²·8 B, retained per document ≈ 2·N²·8 B, plus per-orbital cubes (~40–300 MB each). Second open doubles the footprint. Current win64 builds are 64-bit, so OOM is an aggravator rather than the primary killer — unconfirmed until the Windows Event Viewer exception code is checked (`0xc0000005` → UAF/race, `0xe06d7363` → bad_alloc/OOM).

## Goals / Non-Goals

- Goals: no crash or memory corruption on any document switch (in-flight or quiescent); bounded memory on sequential large-file loads; stale callbacks provably harmless.
- Non-Goals: no redesign of the orbital pipeline (queue/priority system stays); no cancellation of in-flight marching cubes mid-run beyond current capabilities (wait-for-finish is acceptable); no changes to file formats or the OpenBabel submodule; not fixing the ORCA parser's OOB edge cases beyond the density-matrix guard (separate change if desired).

## Decisions

- **Epoch-token invalidation over only wait-for-finish.** Each document switch bumps a generation counter captured by every launched background task; completion slots compare and no-op if stale, plus bounds-check the queue index. Rationale: makes stale delivery harmless regardless of ordering; wait-for-finish alone still needs it because queued signals can arrive between disconnect points. Alternative (only waiting) blocks the UI for minutes on large files.
- **Wait, don't kill, `MeshGenerator`.** On switch: disconnect signals, then `wait()` for the thread before deleting `m_basis`/clearing state. Rationale: `MeshGenerator` has no cancellation; shortest correct path. Alternative (adding cancellation) is a bigger refactor deferred as a follow-up.
- **Release-before-parse ordering.** In `OrbitalExtension/SurfaceExtension::setMolecule`, delete the old basis and explicitly flush the old molecule's cubes/meshes (delete the outgoing `Molecule` synchronously when the same-window path is taken, or at minimum delete `m_basis` before `loadBasis()` re-parses) so peak memory is not the sum of both documents. Fix `MainWindow::loadFile` to delete the previous `MoleculeFile` (and its reader thread) before overwrite.
- **Reader thread ownership.** Parent `ReadFileThread` to the `MoleculeFile` (or track and `deleteLater` after `finished()`), and delete the previous pair in `loadFile`. Rationale: leak today, trivial ownership fix.
- **Move the open-shell prompt out of the parse loop.** Detect open-shell during parse, store the fact, and ask afterwards (or default to alpha and expose a setting). Rationale: a nested event loop mid-parse re-enters arbitrary slots while document state is half-torn-down; it also reads as a hang. Alternative (QProgressDialog-style non-blocking prompt) noted but not required.
- **Null-guard lookups at the render boundary.** `setOrbital`/`updateOrbitalCombo` return early when `meshById`/`cubeById` yield null, matching the existing `QPointer` guards in the paint path.

## Implementation Notes (implementation-level detail for the applying agent)

These notes pin down the non-obvious mechanics behind the tasks. File/line references were verified at planning time; re-check before editing.

### N.1 Stale-callback guards (tasks 1.1–1.3)

- Add members to `OrbitalExtension`: `unsigned long m_generation;` (init 0) and `unsigned long m_runningGeneration;`. `setMolecule` bumps `m_generation` **first, before any teardown**. `startCalculation()` captures `m_runningGeneration = m_generation;` alongside `m_currentRunningCalculation = queueIndex`.
- Guard pattern at the top of all five queue-indexing slots — `calculateCubeDone`, `calculatePosMeshDone`, `calculateNegMeshDone`, `calculationComplete`, **and `updateProgress`** (it is connected to both watcher and meshGen progress signals and also does `&m_queue[m_currentRunningCalculation]`):
  ```cpp
  if (m_runningGeneration != m_generation ||
      m_currentRunningCalculation < 0 ||
      m_currentRunningCalculation >= m_queue.size())
    return; // stale or invalid — never touch m_queue, m_basis, m_meshGen, or the widget
  ```
- **Mutex trap:** `checkQueue()` (orbitalextension.cpp:786) intentionally returns *without* unlocking `m_runningMutex` when it finds a Running entry — the lock is held from `checkQueue` until `calculationComplete` unlocks it. If stale callbacks simply no-op, the mutex stays locked after a mid-calculation switch and the new document's queue deadlocks silently (every `tryLock` fails, nothing calculates).
- Resolution: `setMolecule` owns the teardown. After quiescing (N.2), if a calculation was in flight at entry (`m_currentRunningCalculation != -1`), call `m_runningMutex->unlock()` exactly once. Stale completion slots therefore never touch `m_runningMutex` (double-unlock of a non-recursive QMutex is UB).
- Mirror in `SurfaceExtension`: it connects `m_basis->watcher()` progress/finished slots around surfaceextension.cpp:341–353; guard its `finished`/progress handlers with the same generation check (its queue bookkeeping is lighter but the `m_basis` deref pattern is identical).

### N.2 Quiesce ordering in `setMolecule` (tasks 2.1–2.3)

Exact order in `OrbitalExtension::setMolecule` / `SurfaceExtension::setMolecule`:

```cpp
m_generation++;
// 1. stop new callbacks from arriving
disconnect(&m_basis->watcher(), 0, this, 0);
if (m_meshGen) m_meshGen->disconnect();
// 2. wait for in-flight work under a busy cursor
QApplication::setOverrideCursor(Qt::WaitCursor);
if (m_basis && m_basis->watcher().isRunning())
  m_basis->watcher().future().waitForFinished();   // see note below
if (m_meshGen && m_meshGen->isRunning())
  m_meshGen->wait();
QApplication::restoreOverrideCursor();
// 3. mutex teardown per N.1, then:
m_queue.clear();
m_currentRunningCalculation = -1;
delete m_basis; m_basis = 0;      // safe now: nothing references it
loadBasis();                      // new document's parse starts after old data is gone
```

- **API gap note:** `GaussianSet::m_future` is private with no wait method, but `watcher()` is public and `QFutureWatcher::future()` returns the `QFuture`, so `watcher().future().waitForFinished()` works with **no header change**. Guard with `watcher().isRunning()` (future is only set once `calculateCubeMO`/`calculateCubeDensity` ran).
- The `QtConcurrent::map` lambda writes through `GaussianSet::m_cube`/`m_moMatrix` (gaussianset.cpp:198, 238) — deleting `m_basis` before the wait is a guaranteed cross-thread UAF, which is why the wait must precede the delete.
- `MeshGenerator::run()` marches over raw `const Cube *m_cube; Mesh *m_mesh;` (meshgenerator.h:158–159); `QThread::wait()` is the only safe release path — it has no cancellation.
- The wait can be seconds on huge grids; the busy cursor is not optional (spec: switch must not read as a hang).

### N.3 Release-before-parse ordering (tasks 3.1–3.3)

- `MainWindow::loadFile` (mainwindow.cpp:1287): before `d->moleculeFile = MoleculeFile::readFile(...)`, insert:
  ```cpp
  if (d->moleculeFile) {
    disconnect(d->moleculeFile, 0, this, 0);   // ready()/firstMolReady() from the old read must not fire
    delete d->moleculeFile;                    // synchronous; ~MoleculeFile waits on its thread (below)
    d->moleculeFile = 0;
  }
  ```
  The WaitCursor is already active in `loadFile`, so a bounded wait on the old reader thread is acceptable.
- `ReadFileThread` ownership: give `MoleculeFile` a `ReadFileThread *m_thread` member set by `readFile()`. `~MoleculeFile` does `if (m_thread) { m_thread->wait(); delete m_thread; }` — a running `ReadFileThread::run()` writes through the raw `m_moleculeFile` pointer (readfilethread_p.cpp throughout), so the thread **must be joined before the MoleculeFile dies**. Do not use the `connect(finished, deleteLater)` pattern — it races with the manual delete. Single owner: `MoleculeFile`.
- Old-molecule payload flush (mitigates the deleteLater memory stacking): in `MainWindow::setMolecule`'s same-window branch (options & DeleteOld), before `deleteLater()`:
  ```cpp
  d->molecule->blockSignals(true);
  d->molecule->clear();          // molecule.cpp:1653 — frees cubes/meshes now, not at deleteLater
  d->molecule->blockSignals(false);
  ```
  Safe because we are inside a slot chain: paint events are queued and will not run before `GLWidget::setMolecule` swaps in the new molecule, and the engines' `QPointer` guards tolerate the vanished meshes.

### N.4 Render-boundary null guards (tasks 4.1–4.2)

`SurfaceEngine::setOrbital` (surfaceengine.cpp:287–291) — resolve into locals, assign members only when everything is valid:

```cpp
Mesh *mesh1 = m_molecule->meshById(m_meshes.at(n));
Mesh *mesh2 = mesh1 ? m_molecule->meshById(mesh1->otherMesh()) : 0;
Cube *cube  = mesh1 ? m_molecule->cubeById(mesh1->cube()) : 0;
if (!mesh1 || !mesh2 || !cube)
  return;                       // stale ids after a document switch: render nothing
m_mesh1 = mesh1; m_mesh2 = mesh2;
```

`updateOrbitalCombo` (:236): `Cube *cube = m_molecule->cubeById(mesh->cube()); if (!cube) continue;` before `->cubeType()`.

### N.5 ORCA parser hygiene (tasks 5.1–5.3)

- `m_basisFunctions` (`std::vector<std::vector<std::vector<Eigen::Vector2d>*>*>`): add `void clearBasisFunctions()` deleting both pointer levels; call it in the destructor **and** at the GTO-branch `resize(0)` (orca.cpp:196) — today every re-entry of the GTO branch leaks the previous tree.
- Open-shell (5.2): `processLine` sets `m_openShell = true` and **no longer shows any dialog**. When open-shell, the MO branch parses the alpha block into `m_MOcoeffs` as today and additionally parses the beta block into a new `m_MOcoeffsBeta` (unconditionally — the choice must not require the dialog mid-file). `load(basis)` consults a new `setUseBeta(bool)` (default false = alpha) when picking which block goes into `basis->addMOs`. Callers set it after parsing (extension-level UI decision or a stored setting). Net behavior change: open-shell files default to alpha orbitals instead of prompting — this is the spec-mandated trade (no modal in the parse path).
- Density-matrix guards (5.3): in `load()`, clamp `m_homo` to `[1, m_numBasisFunctions]`; in `calculateDensityMatrix`, bail with `m_orcaSuccess = false` when `m_MOcoeffs.size() < (size_t)m_numBasisFunctions * m_numBasisFunctions`. In both pz-reorder loops (:327, :401), require `idx + 2 < (int)orcaOrbitals.size()` (and per-column `columns[i]` sizes) before the `qSwap(at(idx), at(idx+1))` pairs; else break.

### N.6 Verification environment (task 6)

- ASAN build (Linux): `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"`; MSVC equivalent: `/fsanitize=address`.
- Existing `ctest` suite must pass unchanged; add no new tests unless a synthetic switch-during-calculation test is cheap (design Open Question).
- Memory measurement (task 3.3): Task Manager / `Commit size` column, not RSS.
- Task 6.2 is the reporter-side Event Viewer check (pending): `0xc0000005` → the pointer-lifecycle fixes were the story; `0xe06d7363` → OOM path dominated; either way record the note in this change — the design covers both.

### N.6a Verification outcomes recorded (headless Linux session)

- `ctest` suite: **12/12 pass unchanged** (build type empty / Debug, `ENABLE_TESTS=ON`), including `moleculefileTest` (exercises the `MoleculeFile` lifecycle fixed by tasks 3.1/3.2) and `dialogsmokeTest` (GUI smoke).
- Task 5.1/5.2/5.3 (ORCA parser hygiene): verified with an AddressSanitizer harness (`/tmp/orca_leak_test`) that constructs `ORCAOutput` on `testfiles/koffein_orca.out` (2.7 MiB, 246 MOs) and destroys it repeatedly. **5/5 iterations parse OK with no ASAN leak/OOB report** — confirms the `m_basisFunctions` tree is now freed (5.1) and the pz-reorder / density-matrix guards (5.3) do not read out of bounds. (Task 5.2 open-shell path shares this same parser machinery; no open-shell ORCA test file exists anywhere in the repo, so it is covered by code review + the shared parser, not an end-to-end parse here.)
- Reproduction matrix (task 15) scenario-by-scenario:
  - (a) large ORCA -> large ORCA, same window: library path covered by `moleculefileTest` + ASAN harness; the real `MainWindow::openFile` path runs without crashing under `QT_QPA_PLATFORM=offscreen` (exit 124 = timeout on the GUI event loop, i.e. SIGTERM, not a SIGSEGV crash).
  - (b) same, opened while precalculation is running: **requires the reporter's GUI** — needs a live OrbitalExtension precalculation (background MeshGenerator threads) concurrent with a document switch; not reproducible headlessly.
  - (c) small file second: small-file load covered by `moleculefileTest`; GUI-level manual (reporter).
  - (d) open-shell file second: **no open-shell ORCA output exists in the repo** (verified: `koffein_orca.out` is the only `MOLECULAR ORBITALS` file, single-shell); crafting a valid alpha+beta file against the strict paginated parser is fragile. Covered by code review (task 13) + the shared ASAN-verified parser; end-to-end manual (reporter).
- Task 6.2 Event Viewer check (task 16): **pending reporter output.** The design records both signatures up front — `0xc0000005` (access-violation) points at the pointer-lifecycle / quiesce fixes; `0xe06d7363` (C++ exception / bad_alloc) points at the OOM path. Either way the implementation covers the whole defect family and the design is unchanged; the reporter's Event Viewer code will confirm which path dominated.

## Risks / Trade-offs

- [Waiting for `MeshGenerator` on switch can freeze the UI for seconds on huge grids] → show busy cursor; acceptable short-term; cancellation listed as follow-up.
- [Epoch checks could hide real completion work if set too broadly] → scope: only skip queue/progress bookkeeping, never skip data ownership transfer that already happened.
- [Synchronous release of the old molecule changes timing users may depend on (undo across switch)] → same-window path already treats switch as replacement; undo stack is cleared by `MainWindow::setMolecule` today.
- [Root cause not yet runtime-confirmed] → implementation covers the whole defect family; Event Viewer check by the reporter validates which path dominated without changing the design.

## Open Questions

- Exact Windows failure signature (exception code/faulting module) from the reporting machine — confirms the dominant defect but does not change the task breakdown.
- Whether to add a regression test that loads two large ORCA outputs in-process (test file size concerns) versus a synthetic small-file integration test for switch-during-calculation.
