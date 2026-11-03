# Design: Fix orbital visualization — deadlock, responsiveness, and TM-complex scale

## Context

Deadlock stack (Windows cdb, non-invasive attach, modern tier, Quadro P3200):

```
paintEvent → render → renderModern → emitSceneToBuilder
→ SurfaceEngine::renderOpaque   (holds m_mesh1 read lock, surfaceengine.cpp:82)
  → painter()->drawMesh → SceneBuilder::drawMesh (scenebuilder.cpp:151)
    → Mesh::valid()             (mesh.cpp — QWriteLocker on the same mesh)
    → waits for readers to release … one reader is itself → permanent freeze
```

Current-state facts that shape the design:

- `Mesh::valid()` only compares three `std::vector::size()` values (mesh.cpp)
  yet takes a `QWriteLocker`. `QReadWriteLock` is non-recursive → same-thread
  read-then-write self-deadlocks. The legacy/compat painter never calls
  `valid()`, which is why only the modern tier freezes.
- `SurfaceEngine::updateOrbitalCombo` leaks a mesh read lock on the
  `if (!cube) continue;` path; `Mesh::clear()` (mesh.cpp:251) takes a
  `QWriteLocker`, so a leaked reader wedges the next generator forever.
- `SurfaceEngine::setMolecule` connects `primitiveAdded/Updated/Removed` without
  disconnecting first; `OrbitalExtension::renderOrbital` calls it on every
  click, so connections accumulate.
- `OrbitalExtension::loadBasis` parses the basis (ORCA MO matrix, regex-heavy)
  synchronously on the GUI thread inside `setMolecule`; `fillTable` emits one
  `qDebug` per orbital (741 lines on the Pt-CN file).
- `renderOrbital` returns silently for not-ready orbitals ("Orbital not found,
  or still calculating" — qDebug only).
- `GaussianSet::startCubeCalculationAsync` builds `QVector<GaussianShell>` with
  one 24-byte object per grid point, serially inside one worker before any
  parallel work; `processPoint` heap-allocates `deltas`/`dr2` per point and
  scans all shells per point for screening.
- OpenQube `Cube::m_data` is `std::vector<double>` (Avogadro `Cube` is already
  float); `Cube::setLimits` `resize()`s it on the calling (GUI) thread.
- Constraints: two CIs (Linux GCC/Clang + ASan, Windows MSVC NMake); local
  builds limited to `cmake --parallel 8` (RAM); repo MSVC pitfalls apply
  (mutable mutexes in const methods, no aggregate-init of classes with
  user-provided ctors, `-std:c++17 -permissive-`).

## Goals / Non-Goals

**Goals:**
- No deadlock or leaked lock on any mesh-paint or engine-bookkeeping path.
- File open and orbital clicks always respond; selection auto-renders.
- Cube evaluation memory independent of grid size except the result grids;
  single-precision grid; no GUI-thread grid allocation; bitwise-identical
  results.

**Non-Goals:**
- Size-aware precalculation policy (range/quality already clamped; revisit
  later with real TM-complex timings).
- Camera-aware placement of surfaces, mesh-generator changes (already
  parallel/deterministic), any rendering-backend or picker changes.
- Rewriting the ORCA parser; ECP handling changes (parser reads `NewGTO` only
  and shell/MO counts are consistent — validation render only).

## Decisions

### D1: `Mesh::valid()` becomes lock-free; the lock contract moves to callers
`valid()` mutates nothing, and every real caller (both `SurfaceEngine` render
passes, `SceneBuilder::drawMesh/drawColorMesh`) already holds at least the mesh
read lock, which excludes writers. Remove the `QWriteLocker` entirely and
document on the declaration: "caller must hold the mesh read lock (or have
exclusive access)". A read locker here would still be unsafe: same-thread
re-locking a non-recursive `QReadWriteLock` blocks if a writer queues between
acquisitions — so lock-free is the only correct shape. Task 1.2 audits every
`valid()` call site and adds explicit read locks to any lock-free caller found.

### D2: Surface engine hygiene
- `updateOrbitalCombo`: take the lock only around the work, and `unlock()`
  before *every* `continue` (single lock/release site pattern).
- `setMolecule`: disconnect the engine from the previous molecule before
  connecting to the new one, making repeat calls idempotent.
- Add `AVOGADRO_TEST_COMBO_REBUILDS` counter hook in surfaceengine.cpp
  (mirrors `AVOGADRO_TEST_UPLOAD_COUNTER` in glpainter_p.cpp): counts
  `updateOrbitalCombo` invocations so the hygiene test can assert exactly one
  rebuild per mesh event after repeated selections.

### D3: Async basis load with epoch guard
`OrbitalExtension::setMolecule` keeps its existing `m_generation` epoch. Parse
runs via `QtConcurrent::run` capturing the epoch; the queued completion re-checks
the epoch on the GUI thread, then fills the table and calls
`precalculateOrbitals()`. Dock stays disabled until the basis resolves (existing
no-electrons path already covers this). `setMolecule` teardown bumps the epoch
before releasing `m_basis` and quiesces the in-flight parse the same way the
cube-copy future is quiesced today. The parse must not touch `m_widget`/m_queue
— it produces only the `BasisSet*` (or null); all GUI work happens in the
completion. `precalcpolicytest` pins `precalculateOrbitals` routing — if it
drives `setMolecule` synchronously it is updated to call the completion path
directly.

### D4: Orbital selection semantics (latest click wins)
Extract the decision logic into a testable helper on `OrbitalExtension`
(no `GLWidget` dependency): given an orbital's queue state, return
`RenderNow | RenderWhenReady | EnqueueAndRenderWhenReady`. `OrbitalExtension`
keeps a single `m_pendingOrbital`; `calculationComplete` renders it when the
pending orbital lands (clearing `m_pendingOrbital` first). Clicking an
unqueued, uncompleted orbital enqueues at the widget's *selected* quality —
identical to the Render button, so there is one code path. Document switches
clear `m_pendingOrbital` (the epoch guard already discards stale completions).

### D5: Slab-based cube evaluation (no per-point objects)
Replace the `QVector<GaussianShell>` + `QtConcurrent::map` pair with K slab
workers inside the existing `QtConcurrent::run` task (K = `idealThreadCount`
clamped to [1,16], overridable via a new `GaussianSet::testSlabCount` hook,
mirroring `MeshGenerator::testThreadCount`). Each worker processes a contiguous
index range sequentially:

- per-atom shell buckets precomputed once in `initCalculation` /
  `initCalculationForOrca` (shells are already emitted grouped by atom — the
  ORCA parser's atom-major loop and the resulting non-decreasing
  `m_atomIndices` — so buckets are `[start,end)` ranges per atom);
- worker iterates atoms, computes `dr2` per atom once, skips the whole atom
  when its nearest shell cannot contribute beyond `kScreenTolerance`… kept
  per-shell as today (cutoffs stay per shell; the atom-level skip uses the
  atom's minimum shell cutoff), then evaluates only surviving shells in the
  same global shell order as the current flat loop — **per-point value is
  computed by the identical sequence of floating-point operations, so results
  are bitwise identical regardless of partitioning** (points are independent);
- `deltas`/`dr2` scratch becomes a `thread_local` grow-only buffer (no
  per-point malloc); deltas computed lazily per atom only when a shell
  survives screening;
- the inner loop writes via `Cube::setValue(index, value)` as today;
  `m_gaussianShells` member and its lifecycle are deleted outright
  (calculationComplete stops deleting it);
- same treatment for the density path (`processDensity` → slab workers).

Progress reporting: the existing watcher progress keeps working (the watcher
observes the outer future; per-point progress granularity is lost — acceptable,
the progress dialog is phase-based; keep the throttled 10 Hz update).

### D6: Single-precision OpenQube grid
`OpenQube::Cube::m_data` becomes `std::vector<float>`; `setValue(unsigned int,
double)` stores `static_cast<float>` (Avogadro `Cube::setValue` already does
this). Add an `Avogadro::Cube::setData(const std::vector<float>&)` overload
(direct copy — the existing double overload converts) and use it in both
extensions' copy workers, removing the double→float conversion pass. The grid
result magnitude is O(1) (MO values, density); float precision is far above
iso-surface needs, and the mesh generator already reads the float molecule
cube, so downstream math is unchanged.

### D7: Geometry/storage split for grids
`OpenQube::Cube::setLimits` stores geometry (min/max/dims/spacing) and marks
storage unallocated; a new `ensureData()` allocates+zeroes `m_data` and is
called by the evaluation worker before the first `setValue` (workers are the
only writers between setup and copy). `Avogadro::Cube::setData` already sizes
its own buffer — add a geometry-only guard so `newCube()` on the GUI thread
does not pre-allocate either: `setLimits` keeps computing dims (needed to size
the OpenQube geometry and for `MeshGenerator`), but the molecule-cube storage
allocation happens inside `setData` on the copy worker. Audit `Cube` consumers
that read `data()` before `setData` (mesh generators read *after* the copy —
verified by the existing phase order) before flipping the default.

### D8: Wait-free paint on regenerating meshes
Preserve current semantics: `SurfaceEngine` render passes keep `tryLockForRead`
(skip when a writer holds the mesh) — the spec's "skip or defer in bounded
time" scenario is satisfied by the existing skip. No new machinery.

## Risks / Trade-offs

- [Lock-free `valid()` misused by a future lock-free caller] → contract
  documented on the declaration; task 1.2 audits all current call sites and
  adds locks where missing; the regression test exercises the engine-held-read
  path end to end.
- [Async basis load regressions in tests] → `precalcpolicytest` and the full
  suite run after D3; epoch guard pattern is already proven in this file
  (cube-copy quiesce).
- [Auto-render surprises] → pending state is visible in the table's progress
  column and the latest click wins; document switch clears it.
- [Slab evaluation changing results] → impossible by construction (identical
  per-point op order, independent points); `cubeevaltest` asserts bitwise
  equality across 1 vs K slabs and finiteness at edge points.
- [float grid precision] → iso-surface extraction thresholds are ≥1e-3; the
  molecule-side cube was already float; determinism test covers the conversion.
- [MSVC compatibility] → `thread_local` POD scratch, no new threading
  primitives beyond existing QtConcurrent; counter hook uses the existing
  conditional-compilation pattern; Windows installer job is triggered before
  merge per AGENTS.md.

## Migration Plan

Single self-contained change; no data or settings migration. Rollback = revert
the commit.

## Open Questions

None material.
