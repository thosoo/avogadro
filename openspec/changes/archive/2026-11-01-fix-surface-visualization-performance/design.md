# Design: Fix surface/orbital visualization performance and responsiveness

## Context

Verified pipeline facts (session 2026-10-30; line anchors approximate):

```
load ORCA output
 └▶ OrbitalExtension::precalculateOrbitals() (orbitalextension.cpp:447)
    queues 10 orbitals (or ALL MOs) at OrbitalWidget::defaultQuality()
    (orbitalwidget.cpp:66, persisted QSettings; OQ_VeryHigh → 0.05 Å,
    orbitalwidget.h:46,195). Serial queue: checkQueue:840 →
    startCalculation:516 → calculateCube:551.
per orbital:
 ① GaussianSet::calculateCubeMO (openqube/gaussianset.cpp:168)
    allocates QVector<GaussianShell>(points) + init loop (:182-189)
    ON THE GUI THREAD (called from calculateCube). 24 B/point.
    Then QtConcurrent::map over shells; processPoint (:~1000) evaluates
    deltas to ALL atoms + ALL primitives — screening only by tiny MO
    coefficient (pointS), never by distance. processDensity additionally
    heap-allocates MatrixXd values(matrixSize,1) PER POINT.
    cube->lock()->lockForWrite() held GUI→GUI (lock :193, unlock in
    calculationComplete — both on GUI; QReadWriteLock is not
    thread-affine-freed, keep this pairing).
 ② OrbitalExtension::calculateCubeDone (:591, queued slot → GUI thread)
    does info->cube->setData(*m_qube->data()) (:606) — deep copy of the
    whole grid (std::vector<double>, cube.h:237) ON THE GUI THREAD.
 ③ MeshGenerator::run (meshgenerator.cpp:88) — ONE thread;
    m_vertices.reserve(dim.x*y*z*3) (:98) — 3 vertices PER GRID POINT
    reserved for vertices AND normals before knowing surface size
    (~2 GB at 64M points); manual re-grow (:112-115) redundant with
    std::vector's own geometric growth.
    normal(pos) per triangle vertex (:143) = 6 × Cube::valuef trilinear
    (cube.cpp:250, ~48 guarded reads); Mesh::setVertices/setNormals
    DEEP COPY into the Mesh (mesh.cpp:84-91) and each bumps revision
    (:89,:135) → the GLPainter VBO cache (glpainter_p.cpp:268
    ensureMeshVBO, keyed mesh-pointer+revision) uploads the huge buffer
    2-3 times (vertices, normals, ESP colors).
 ④ (ESP) SurfaceExtension::calculateDone → calculateESP (:234, called
    :689-691) runs per-vertex neighbor query + QColor::fromHsv ON THE
    GUI THREAD.
 ⑤ every frame: GLWidget::flushCapturedMeshes (glwidget.cpp:1570)
    redraws all stable meshes: GLPainter::drawMesh (glpainter_p.cpp
    :1236) — fixed-function, blending, no culling, vertices duplicated
    per triangle. If the RESOLVED context is Core profile, ensureMeshVBO
    refuses (:279-291) and drawMesh falls back to CLIENT ARRAYS — the
    whole mesh re-sent from RAM every frame. App requests
    CompatibilityProfile 3.3 (main.cpp:375-376) but the gate checks the
    resolved profile (driver-dependent).
```

Constraints: two CIs (Linux GCC/Clang + ASan, Windows MSVC NMake) must both
pass; `extern/openbabel` is a pristine pinned submodule (must not be touched);
`document-switch-stability`'s generation-guard pattern (m_generation /
m_runningGeneration epochs in orbitalextension/surfaceextension) must survive
all threading changes; the drawtool change `add-draw-tool-click-extend` is
active in the same tree — do not touch `libavogadro/src/tools/`.

## Goals / Non-Goals

**Goals:**
- GUI event loop never blocked by grid/vertex-proportional work.
- Peak transient memory bounded by a small multiple of the result size.
- Rotation with displayed surfaces stays interactive; full quality on rest.
- Distance screening + density-path allocation fix + parallel marching cubes
  cut wall-clock of the compute phases.
- Background precalculation cannot repeat the whole cost ×N at Very High.

**Non-Goals:**
- Porting the mesh draw path to a core-profile shader/VAO pipeline (kept as
  the compatibility-profile fixed-function path; documented follow-up).
- Vertex welding / indexed mesh format (Mesh gains no index buffer in this
  change).
- Sliced (budgeted) mesh VBO uploads via ChunkedUploader — deferred; the
  compaction + single-upload work in this change shrinks arrival cost enough.
- Camera-aware isosurface placement, mesh LOD/decimation quality controls,
  changes to file parsers or OpenBabel/OpenQube evaluation math beyond the
  screening skip.

## Decisions

### D1: Off-GUI setup via one worker hop, lock pairing kept on the GUI
`OrbitalExtension::calculateCube` / `SurfaceExtension` equivalents stop
calling `m_basis->calculateCubeMO(...)` directly on the GUI thread. New
`GaussianSet::startCubeCalculationAsync(Cube*, state)`:
- GUI: acquires the cube write lock (as today), connects the watcher's
  `finished` (as today), then dispatches `QtConcurrent::run` that performs
  the shell-vector allocation + init loop and starts the existing
  `QtConcurrent::map`.
- Unlock stays in `calculationComplete` on the GUI thread (unchanged), so
  the lock pair remains GUI→GUI. The watcher is a member of `GaussianSet`
  (GUI affinity) so its `finished` still queues to the GUI thread.
Alternative rejected: moving the lock acquisition into the worker (would
pair lock/unlock across threads — undefined for QReadWriteLock).

### D2: Index-based map, no per-point shell objects
`processPoint`/`processDensity` already derive the grid position from an
integer index (`cube->position(index)`). Keep the `QVector<GaussianShell>`
for now (D1 removes it from the GUI thread and its cost is one allocation
on a worker) — replacing it with a range-over-ints map requires a container
anyway under `QtConcurrent::map`; do NOT invent a custom thread pool in this
change. (If allocation cost still shows in profiling, a follow-up can switch
to `QtConcurrent::blockingMapped` over a strided index vector — out of scope.)

### D3: Cube handoff copy runs on a worker
`calculateCubeDone` (`orbitalextension.cpp:606`) and the equivalent
`SurfaceExtension` phase-0 path (`surfaceextension.cpp:~656`) stop copying
`*m_qube->data()` into the molecule cube on the GUI thread. Add a small
helper that runs the copy in `QtConcurrent::run` (converting double→float,
D6), holding the cube write lock inside the worker, then queues the
continuation (`calculatePosMesh` / phase-1) back onto the GUI via
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. The generation-epoch
guard is checked again inside the continuation (stale document switch while
copying must no-op).
Alternative rejected: eliminating the OpenQube/Avogadro duplicate buffer
(correct, but touches both Cube classes' API — follow-up).

### D4: ESP coloring off the GUI thread
`calculateESP(mesh)`: build a `std::vector<Color3f>` sized to the vertex
count, fill it with `QtConcurrent::map` over an index vector (NeighborList
queries are read-only during the map), then call `mesh->setColors(...)`
once from the GUI thread. Colors convert with a plain HSV→RGB computation —
`QColor::fromHsv` per vertex is replaced by an inline HSV→RGB conversion in
the worker (QColor is GUI-ish and slow); verify hue/sat/val mapping matches
the current output on one test case before replacing.

### D5: Bounded marching-cubes memory
- Replace `reserve(dim³×3)` with `reserve(std::min<std::size_t>(dim³, 1<<21))`
  for vertices and normals; delete the manual capacity-doubling block
  (meshgenerator.cpp:112-115) — `std::vector::push_back` already grows
  geometrically.
- `Mesh::setVertices/setNormals` gain move overloads
  (`setVertices(std::vector<Vector3f>&&)`) using `std::move`; MeshGenerator
  calls them, eliminating the deep copies (mesh.cpp:84-91 paths stay for
  lvalue callers).
- Run the MO− generator only after the MO+ generator finished (chain in the
  existing phase machine: both orbitalextension and surfaceextension
  already tolerate "still calculating one of the meshes → return", so start
  meshGen2 from the meshGen1-finished continuation).

### D6: Single-precision cube data
`Avogadro::Cube::m_data` becomes `std::vector<float>`; `setData` gains a
double-convert path (worker-side, D3); `value()` returns double via cast to
keep call sites compiling; `valuef` unchanged semantics. Grep all `m_data`
and `setData` call sites (cube.cpp/h, meshgenerator.cpp, surfaces tests,
python bindings if present) and update mechanically. Halves grid RAM and
copy time; mesh coordinates were already float.

### D7: Distance screening with a conservative per-shell cutoff
In `initCalculation` (once per basis), precompute per-shell-index
`rMax2[i] = -ln(1e-12) / alphaMin(i)` where `alphaMin(i)` is the smallest
gaussian exponent of shell i (shell→atom mapping already exists as
`m_atomIndices`). In `processPoint` and `processDensity` loops, skip shell i
when `dr2[m_atomIndices[i]] > rMax2[i]` (before the switch). Density keeps
its contraction but over the surviving shells only — contributions beyond
the cutoff are < 1e-12 each, far below float storage precision. Screening is
unconditional (no user toggle) but the tolerance constant is a named
constexpr. Acceptance test compares screened vs a debug-unscreened build on
a small fixture.

### D8: Density path stops allocating per point
`processDensity`'s `MatrixXd values(matrixSize,1)` becomes a
`thread_local` Eigen vector, resized only when `matrixSize` changes. Each
QtConcurrent worker thread reuses its buffer. No output change.

### D9: Parallel marching cubes over deterministic slabs
Partition the outer cell loop range [0, dim.x()-1) into K contiguous slabs
(K = `QThread::idealThreadCount()`, clamped to [1,8]); each slab fills its
own local vertex/normal vectors via the existing `marchingCube` code path
(cube reads are read-only), then slabs concatenate in slab order — run-to-run
deterministic. The current single-loop progress emission moves to
slab-completion counting. The reserve/growth change (D5) applies to slab
vectors too. Triangle content is identical to serial; vertex ORDER may
differ from the serial order (spec allows equivalence-after-ordering).

### D10: Render-path fixes without a shader port
- `flushCapturedMeshes` (glwidget.cpp:1570) wraps each mesh draw in
  `glEnable(GL_CULL_FACE)`/`glCullFace(GL_BACK)` + restore — closed
  isosurfaces halve fill cost; visual change limited to backface blending
  (accepted, spec-documented).
- One revision bump per assembled mesh: MeshGenerator sets vertices+normals
  under one write-lock section via a new `Mesh::setGeometry(vertices&&,
  normals&&)` that bumps revision once; ESP path still bumps once more when
  colors are set (2 uploads max, down from 3).
- Interaction flag in GLWidget: set while any mouse drag is active on the
  widget (rotate/pan/zoom), cleared by a `QTimer::singleShot(250 ms)` after
  the last move; `flushCapturedMeshes` skips mesh draws while the flag is
  set and schedules one `update()` on clear. Surfaces disappear during the
  drag and reappear ~250 ms after release (spec-scoped behavior).
- Startup diagnostic: log the resolved QSurfaceFormat profile once at GL
  init so the client-array fallback condition is visible in the field
  (fallback path itself unchanged — non-goal is the core-profile port).

### D11: Precalculation policy clamp
`OrbitalExtension::precalculateOrbitals` computes the queue resolution as
`min(defaultQuality, OQ_Medium)` (0.18 Å) — `std::min` on the enum values
(OQ_Low < OQ_Medium < OQ_High < OQ_VeryHigh, orbitalwidget.h:42-46). Explicit
per-orbital calculation (`OrbitalWidget` table selection path,
orbitalwidget.cpp:170) keeps the user-chosen quality and still reuses cubes
via the `(orbital, resolution)` match in `calculateCube`. No new settings key
(preferences UI unchanged).

## Risks / Trade-offs

- [QReadWriteLock pairing broken by threading changes] → D1 keeps
  lock/unlock on the GUI thread; review checklist item; ASan CI helps.
- [Stale-callback guards regressed by async continuation] → D3 re-checks the
  generation epoch inside the continuation; existing surfaceextensiontest
  and document-switch scenarios remain in the suite as guards.
- [Screening changes results slightly] → tolerance is 1e-12 per contribution,
  ~float-storage-noise level; acceptance test quantifies (D7); deterministic.
- [Parallel marching cubes changes vertex order] → output equivalence
  defined as count + position-multiset; downstream consumers (Mesh, painter)
  are order-insensitive triangle soup.
- [Surfaces vanish during drags] → explicit spec'd behavior, bounded 250 ms
  return; keeps the alternative (full-mesh redraw per frame) from returning.
- [float cube loses precision] → grid values span ~1e-6..1; float's ~7
  digits are far above isosurface needs; marching cubes already reads via
  `valuef` (float).
- [Two active changes in one tree] → this change must not touch
  `libavogadro/src/tools/` (drawtool change owns it); CI full suite catches
  interference.

## Migration Plan

Self-contained; no settings/data migration. Rollback = revert the commit(s).
The precalc clamp (D11) changes behavior for users who set Very High as
default quality — that is the point (spec requirement), no opt-out.

## Open Questions

None material. (Core-profile mesh shader port and sliced uploads are
deliberately deferred follow-ups; ChunkedUploader reuse design exists if
pursued later.)
