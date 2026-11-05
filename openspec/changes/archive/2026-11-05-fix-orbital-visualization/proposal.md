# Proposal: Fix orbital visualization — deadlock, responsiveness, and TM-complex scale

## Why

Selecting any orbital on the modern rendering tier freezes the window permanently: a
stack-confirmed self-deadlock where `Mesh::valid()` (mesh.cpp) takes a mesh *write*
lock while the paint path already holds the same mesh's *read* lock
(SurfaceEngine holds read across `painter()->drawMesh`; `SceneBuilder::drawMesh`
calls `mesh.valid()`, scenebuilder.cpp:151/168). Around it sit several further
defects in the same pipeline: a mesh lock **leak** in `updateOrbitalCombo`
(`continue` on a stale cube skips `unlock()`), duplicate signal connections
accumulating on every orbital click, a GUI-thread stall at file open (synchronous
ORCA basis parse + 741-row table fill), a silent no-op when clicking an orbital
that is still calculating, and a scaling wall for transition-metal complexes
(Pd(PPh₃)₄, Ir(ppy)₃): the cube evaluation allocates an O(grid points) × 24-byte
per-point object vector (≈4.7 GB at Very High on Pd(PPh₃)₄), stores the grid in
double precision, and zero-fills grid-sized buffers on the GUI thread.

## What Changes

- **Deadlock fix.** `Mesh::valid()` no longer takes the mesh write lock (it
  mutates nothing); the caller-held-lock contract is documented and audited at
  every call site. Painting a mesh through the modern-tier scene builder on a
  thread that holds the mesh read lock cannot block.
- **Regression test.** A headless test holds a mesh read lock and calls
  `Mesh::valid()` on the same thread; pre-fix this deadlocks, post-fix it returns.
- **Surface engine hygiene.** `updateOrbitalCombo` releases every mesh lock it
  takes, including the stale-cube early-out; `SurfaceEngine::setMolecule`
  disconnects before reconnecting so repeated orbital clicks cannot multiply
  per-primitive work (verified via a test-counter hook, following the existing
  `AVOGADRO_TEST_UPLOAD_COUNTER` precedent).
- **Responsive file open.** Basis-set parsing/loading moves off the GUI thread
  with an epoch guard (stale parse results are discarded on document switch, the
  parse worker is quiesced before teardown); per-orbital `qDebug` spam during
  table fill is removed.
- **Orbital selection always responds.** Clicking a completed orbital renders it
  (unchanged); clicking a queued/running orbital marks it pending and renders it
  automatically on completion; clicking an orbital that is neither queued nor
  completed enqueues it at the selected quality and renders on completion. The
  latest click wins.
- **Scalable cube evaluation.** MO/density evaluation maps over grid slabs on
  worker threads instead of building one `GaussianShell` object per grid point;
  shells are iterated via precomputed per-atom buckets with fused distance
  screening; per-point scratch vectors become thread-local buffers. Grid storage
  becomes single precision and grid-sized allocations move off the GUI thread
  (geometry is computed on the GUI thread, storage is allocated by the worker
  that fills it). Per-point math order is unchanged, so cube values are bitwise
  identical to the current serial-per-point evaluation.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `surface-visualization`: adds deadlock-freedom and engine-state-hygiene
  requirements for mesh painting and the surfaces engine; extends GUI
  responsiveness to basis-set loading at document open; extends bounded memory
  and determinism to the cube-evaluation worker partitioning; adds orbital
  selection feedback/auto-render behavior.

## Impact

- **Modified:**
  `libavogadro/src/mesh.{h,cpp}` (valid() lock, contract docs),
  `libavogadro/src/rendering/scenebuilder.cpp` (comment/contract only),
  `libavogadro/src/engines/surfaceengine.{h,cpp}` (lock leak, connections,
  counter hook),
  `libavogadro/src/extensions/surfaces/orbitalextension.{h,cpp}` (async basis
  load, pending selection, logging),
  `libavogadro/src/extensions/surfaces/orbitalwidget.{h,cpp}` (selection state),
  `libavogadro/src/extensions/surfaces/surfaceextension.cpp` (copy path),
  `libavogadro/src/extensions/surfaces/openqube/gaussianset.{h,cpp}` (slab
  evaluation, buckets, test hook),
  `libavogadro/src/extensions/surfaces/openqube/cube.{h,cpp}` (single precision,
  lazy allocation),
  `libavogadro/src/cube.{h,cpp}` (float setData overload),
  `libavogadro/tests/CMakeLists.txt` (new tests).
- **New:** `libavogadro/tests/meshlocktest.cpp`, `libavogadro/tests/cubeevaltest.cpp`,
  `libavogadro/tests/surfaceenginehygienetest.cpp`.
- **Constraints:** local builds must use `cmake --build build --parallel 8`
  (RAM limit). All new code must compile under MSVC (`-std:c++17 -permissive-`;
  no non-mutable mutex in const methods; no aggregate-init of classes with
  user-provided ctors). CI runs two legs (Qt6 Linux incl. ASan; Windows
  installer) — the Windows job must be triggered before merge.
