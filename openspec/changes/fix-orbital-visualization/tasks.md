# Tasks: Fix orbital visualization

Implementation notes for the applying agent:

- Design decisions are D1–D8 in `design.md`; file references are exact.
- **Local builds MUST use `cmake --build build --parallel 8`** (RAM limit — never
  higher -j).
- Section order is dependency order. Tasks 1–3 fix the freeze and its immediate
  surroundings; tasks 4–6 change behavior/perf; section 7 verifies.
- All new test binaries are registered in `libavogadro/tests/CMakeLists.txt`
  following the patterns already present there (compile non-exported sources
  directly into the test when linking fails on Windows).
- After tasks 1–3 land, run the `reviewer` subagent on the diff so far; run it
  again on the full diff in section 7.

## 1. Deadlock fix (modern-tier paint freeze)

- [x] 1.1 Remove the `QWriteLocker` from `Mesh::valid()` in
      `libavogadro/src/mesh.cpp`; it only reads three vector sizes. Document the
      contract on the declaration in `libavogadro/src/mesh.h`: "caller must hold
      the mesh read lock, or otherwise have exclusive access" (design D1).
      Verify: `cmake --build build --parallel 8` compiles.
- [x] 1.2 Audit every `Mesh::valid()` call site
      (`git grep -n "\.valid()\|->valid()" libavogadro/` filtered to Mesh): for
      each caller NOT already holding the mesh read lock (expected candidates:
      none — SceneBuilder is reached under the engines' read locks — but check
      `glpainter_p.cpp`, `glwidget.cpp`, `meshgenerator.cpp`, tests), add an
      explicit read-lock acquire/release around the call or route it through a
      locked helper. Verify: cite each call site in the session log with
      "locked by caller" or "lock added".
- [x] 1.3 New headless regression test `libavogadro/tests/meshlocktest.cpp`,
      registered as `meshlock` in the `tests` list
      (`libavogadro/tests/CMakeLists.txt`; links `avogadro`, `Qt6::Test`;
      `QT_QPA_PLATFORM=offscreen` env like the other tests). Cases:
      - caller holds `mesh.lock()->lockForRead()` then calls `mesh.valid()` —
        must return (pre-fix this deadlocks the test run) — and return true for
        a mesh with equal vertex/normal sizes;
      - `valid()` returns false for mismatched sizes and for a color vector
        that is neither 1 nor N (build meshes via `Mesh::setGeometry`-style
        setters used by existing mesh tests);
      - `tryLockForWrite()` succeeds immediately after the read lock is
        released (sanity that no lock leaked from the test itself).
      Verify: `ctest -R meshlock --output-on-failure` green.
- [x] 1.4 Build everything and run the mesh-related suites:
      `cmake --build build --parallel 8 && ctest -R "meshlock|meshstream|meshgenparallel|consumerparity" --output-on-failure`.
      Verify: exit 0.

## 2. Surface engine hygiene (lock leak, duplicate connects)

- [x] 2.1 Fix the mesh read-lock leak in `SurfaceEngine::updateOrbitalCombo`
      (`libavogadro/src/engines/surfaceengine.cpp`): restructure the loop so the
      read lock is released before every `continue`/early-out (the stale-cube
      `if (!cube) continue;` currently skips `mesh->lock()->unlock()`). Prefer a
      single exit path (e.g., set a `locked` flag, or scope the lock) so no
      future early-out can repeat the bug. Verify: build.
- [x] 2.2 Make `SurfaceEngine::setMolecule` idempotent: before connecting
      `primitiveAdded/Updated/Removed` to the new molecule, disconnect this
      engine from the previous molecule (and from the new one if already
      connected — `QObject::disconnect` on the specific slots, or
      `Qt::UniqueConnection`). Keep `updateOrbitalCombo()` at the end.
      Verify: build.
- [x] 2.3 Add the test counter hook: in `surfaceengine.cpp`, under
      `#ifdef AVOGADRO_TEST_COMBO_REBUILDS` define a file-static counter
      incremented at the top of `updateOrbitalCombo()` (pattern:
      `AVOGADRO_TEST_UPLOAD_COUNTER` in `glpainter_p.cpp`). Export it via a
      small accessor guarded by the same macro. Verify: build.
- [x] 2.4 New test `libavogadro/tests/surfaceenginehygienetest.cpp`, registered
      as `surfaceenginehygiene` in the tests list; compile
      `../src/engines/surfaceengine.cpp` directly into the test (it is not
      exported from libavogadro) with `AUTOUIC` search path including
      `../src/engines/` for `surfacesettingswidget.ui` (pattern: the parity*
      tests that compile engine sources). `target_compile_definitions(...`
      `AVOGADRO_TEST_COMBO_REBUILDS)`. Cases:
      - stale-cube lock leak: build a molecule with one mesh whose `cube()` id
        does not resolve (`cubeById` returns null — set a bogus cube id);
        create the engine's settings widget (so `updateOrbitalCombo` does work),
        call `setMolecule`, then assert
        `mesh.lock()->tryLockForWrite()` succeeds immediately (no leaked
        reader);
      - connection idempotency: call `setMolecule(&mol)` 5 times, add one mesh
        (fires `primitiveAdded`), assert the combo-rebuild counter advanced by
        exactly 1 (not 5);
      - combo rebuild correctness: with meshes of type MO (positive isovalue)
        and a mesh with a stale cube, the combo contains only the valid MO
        entries.
      Verify: `ctest -R surfaceenginehygiene --output-on-failure` green.
- [x] 2.5 Build and run engine/surface suites:
      `cmake --build build --parallel 8 && ctest -R "surfaceenginehygiene|meshlock|surfaceextension" --output-on-failure`.
      Verify: exit 0.
- [x] 2.6 Run the `reviewer` subagent on the diff of tasks 1–2 (correctness +
      regression risk: lock semantics, Qt signal handling). Integrate accepted
      findings; record rejections with evidence.

## 3. Orbital selection UX (always respond, render when ready)

- [x] 3.1 In `OrbitalExtension` (`libavogadro/src/extensions/surfaces/orbitalextension.{h,cpp}`),
      add a pure testable helper with no `GLWidget` dependency, e.g.
      `enum class SelectionPlan { RenderNow, RenderWhenReady, EnqueueThenRender }`
      and a static/free function `planOrbitalSelection(bool completed, bool
      queuedOrRunning)` returning the plan (design D4). Verify: builds.
- [x] 3.2 Rewire `renderOrbital` to use the plan: RenderNow → existing logic;
      RenderWhenReady → store `m_pendingOrbital = orbital` and give visible
      feedback (the table's progress column already shows the running calc; add
      nothing else here); EnqueueThenRender → call
      `addCalculationToQueue(orbital, OrbitalWidget::OrbitalQualityToDouble(selected
      quality), m_widget->isovalue(), /*priority=*/0)` + `checkQueue()` + set
      pending. Verify: builds.
- [x] 3.3 In `calculationComplete()`: after completing an orbital, if
      `m_pendingOrbital == info->orbital`, clear `m_pendingOrbital` and render
      that orbital (call the RenderNow path) — before `checkQueue()`. Reset
      `m_pendingOrbital` in `setMolecule` teardown (it is already under the
      epoch guard). Verify: builds.
- [x] 3.4 Remove the per-orbital `qDebug() << desc;` in the table-fill loop of
      `OrbitalExtension::setMolecule` (741 lines on a 741-MO file) — the
      description is visible in the table. Also drop the `qDebug()` in
      `fillTable`'s HOMO scroll if trivially noisy. Verify: builds; grep shows
      no per-orbital logging remains in that loop.
- [x] 3.5 New test `libavogadro/tests/orbitalselectiontest.cpp`, registered as
      `orbitalselection`: compile `../src/extensions/surfaces/orbitalextension.cpp`
      into the test (AUTOMOC handles the Q_OBJECT; if the plugin factory export
      collides, add an `AVOGADRO_TEST_NOPLUGIN`-style guard in
      orbitalextension.h following drawtool.h). Cases:
      - plan: completed→RenderNow; queued/running→RenderWhenReady; neither→
        EnqueueThenRender;
      - pending lifecycle: with a tiny real queue (basis with 1 MO is not needed
        — drive `addCalculationToQueue` + `calculationComplete` directly if
        accessible, or test the helper + pending-member logic via a friend/test
        hook), assert the pending orbital is cleared after completion and that a
        second selection overwrites the first.
      Verify: `ctest -R orbitalselection --output-on-failure` green.
- [x] 3.6 Build and run: `cmake --build build --parallel 8 && ctest -R "orbitalselection|precalcpolicy" --output-on-failure`.
      Verify: exit 0.

## 4. Responsive file open (async basis load)

- [x] 4.1 In `OrbitalExtension::loadBasis` split the parse from the state
      update: a worker function that produces `OpenQube::BasisSet*` (or null)
      from a file name without touching any member except via the captured
      epoch; dispatch via `QtConcurrent::run`, track the future in a member
      (`m_basisLoadFuture`), and invoke a queued GUI continuation that
      re-checks `m_generation == m_runningGeneration`-style epoch before
      assigning `m_basis`, calling `m_surfaceDialog`-equivalent table fill
      (the existing `setMolecule` tail: HOMO/LUMO detection, `fillTable`, dock
      enable, `precalculateOrbitals()`). Keep the synchronous
      "File Format"-specific branches (gamout/gukout) inside the same worker.
      Verify: builds.
- [x] 4.2 Teardown safety in `setMolecule`: bump `m_generation` first (already
      done), disconnect nothing new (worker touches no signals), and wait for
      `m_basisLoadFuture` before `delete m_basis` (bounded wait like the
      existing `m_cubeCopyFuture.waitForFinished()`). Ensure the stale
      continuation no-ops on the new epoch. Verify: build + code review that no
      path deletes `m_basis` while the parse worker runs.
- [x] 4.3 Check `libavogadro/tests/precalcpolicytest.cpp`: if it drives
      `setMolecule` (or `precalculateOrbitals`) synchronously expecting a loaded
      basis, update it to invoke the new completion path (or stub the basis) so
      it still pins the clamp logic. Verify:
      `ctest -R precalcpolicy --output-on-failure` green.
- [x] 4.4 Build the full tree: `cmake --build build --parallel 8`. Verify: exit 0.
- [x] 4.5 Run everything affected: `ctest -R "orbitalselection|precalcpolicy|surfaceextension|dialogsmoke" --output-on-failure`.
      Verify: exit 0. (dialogsmoketest exercises ORCA dialogs.)

## 5. Slab-based cube evaluation (no per-point objects)

- [x] 5.1 In `GaussianSet` (`libavogadro/src/extensions/surfaces/openqube/gaussianset.{h,cpp}`),
      precompute per-atom shell buckets once per init: in `initCalculation()` and
      `initCalculationForOrca()` build `m_shellBucketStart` (size atoms+1) from
      the non-decreasing `m_atomIndices` (assert monotonicity in debug builds).
      Verify: builds.
- [x] 5.2 Add the slab worker: `void processPointSlab(int slabIndex, int slabCount,
      Cube* cube, unsigned int state)` iterating point indices `[lo, hi)`
      sequentially; per point: loop atoms, compute `dr2` per atom once, skip the
      atom when `dr2 > min shell cutoff of that atom's buckets` (cheap
      precomputed per-atom cutoff = min over its shells), otherwise evaluate
      that atom's shells in original shell order with the same per-shell
      `m_shellRMax2` screening and the exact same pointX call sequence as
      today's flat loop (bitwise-identical per-point op order, design D5).
      Deltas/dr2 scratch: `thread_local std::vector<Vector3d>/std::vector<double>`
      grow-only, sized to atoms. Write results via `cube->setValue(index, v)`.
      Add static test hook `int GaussianSet::testSlabCount = 0;`
      (0 = auto; mirrors `MeshGenerator::testThreadCount`).
      Verify: builds (this is the core task — take care to keep the shell
      iteration order identical to the current flat loop, including the
      `default: /* zero contribution */` fall-through).
- [x] 5.3 Rewire `startCubeCalculationAsync` and
      `startCubeDensityCalculationAsync`: inside the existing `QtConcurrent::run`
      task, spawn K slab workers (`QtConcurrent::run` per slab, K =
      `testSlabCount > 0 ? testSlabCount : clamp(idealThreadCount, 1, 16)`),
      wait for them, and delete the old `QVector<GaussianShell>` code entirely —
      including the member `m_gaussianShells`, its allocation, the per-item init
      loop, and the `delete` in `calculationComplete()`. Keep the cube write
      lock discipline and the watcher wiring byte-identical. Verify: build;
      grep confirms no `GaussianShell` vector remains in the MO/density paths.
- [x] 5.4 New test `libavogadro/tests/cubeevaltest.cpp`, registered as
      `cubeevaltest`: link `OpenQube` (as `screeningtest` does); if
      `GaussianSet` symbols are not exported from the OpenQube target on
      Windows, compile `../src/extensions/surfaces/openqube/gaussianset.cpp`
      and `.../openqube/cube.cpp` directly into the test (pattern:
      `renderertier`). Build a small synthetic `GaussianSet`
      (`addBasis`/`addMO`-style API as used in the openqube tests), a 10³ grid,
      and one MO; evaluate with `testSlabCount = 1` vs `= 4` vs auto into two
      cubes and assert **bitwise equality** of all values plus finiteness
      (no NaN/Inf), and that two auto runs are bitwise identical. Include a
      point far outside all screening cutoffs (tiny value but finite) and a
      point exactly on a nucleus. Verify:
      `ctest -R cubeevaltest --output-on-failure` green.
- [x] 5.5 Build and run math-adjacent suites:
      `cmake --build build --parallel 8 && ctest -R "cubeevaltest|screeningtest|surfaceextensionmothrottle" --output-on-failure`.
      Verify: exit 0. (screeningtest pins screening tolerance — must be
      untouched.)

## 6. Single-precision grid + allocation split

- [x] 6.1 `OpenQube::Cube` (`.../openqube/cube.{h,cpp}`): change `m_data` to
      `std::vector<float>`; `setValue(unsigned int, double)` stores
      `static_cast<float>`; `data()` returns the float vector; update internal
      readers (`value()`, `valuef()`, `position()`-adjacent math, `calculationComplete`
      paths) for the narrower type. Verify: builds OpenQube target.
- [x] 6.2 `Avogadro::Cube` (`libavogadro/src/cube.{h,cpp}`): add
      `bool setData(const std::vector<float> &values)` overload (direct copy,
      resize included). Verify: builds.
- [x] 6.3 Update both copy workers to the float path:
      `OrbitalExtension::calculateCubeDone` and
      `SurfaceExtension::calculateDone` use
      `cube->setData(*qube->data())` with the new overload (no conversion
      pass). Verify: builds.
- [x] 6.4 Geometry/storage split (design D7): `OpenQube::Cube::setLimits`
      stores geometry only and does NOT allocate; add private
      `void ensureData()` (allocates+zeroes when size mismatch) called by the
      evaluation worker before the first `setValue`, and by `data()` readers
      that legitimately need the buffer (audit: `data()->size()` call sites —
      the slab partitioning needs the size, so expose `pointCount()` returning
      dims product without allocating). Update the two async entry points to
      call `ensureData()` inside the worker. Audit `Cube` consumers that read
      `data()` before `setData` (mesh generators read after the copy — confirm
      via the phase order in both extensions). Verify: build; grep audit
      recorded in the session log.
- [x] 6.5 Confirm the GUI thread no longer grid-allocates: with
      `AVOGADRO_RENDERER` default, request a High-resolution surface in a
      headless path if practical, else verify by code reading that
      `newCube()`/`newQube()` only compute geometry (no `resize` on grid-sized
      buffers) and both evaluations allocate in the worker. Record the audit
      conclusion. Verify: code-review note in session log.
- [x] 6.6 Build and run: `cmake --build build --parallel 8 && ctest -R "cubeevaltest|meshstream|meshgenparallel|surfaceextensionmothrottle|molecule" --output-on-failure`.
      Verify: exit 0.

## 7. Verification

- [x] 7.1 Full build: `cmake --build build --parallel 8`. Verify: exit 0.
- [x] 7.2 Full suite: `ctest --test-dir build --output-on-failure`.
      Verify: exit 0, no new failures (watch `autooptrepro`, `drawcommand`,
      `addremovehydrogens`, `precalcpolicy`, `dialogsmoke`).
- [x] 7.3 Manual matrix (in the running app, modern tier — the deadlock is only
      reachable with GL):
      - open the Pt-CN output (741 MOs): window stays responsive during load;
        table appears; no per-orbital log flood;
      - during the precalc queue, click a queued orbital → pending indication;
        when it completes, it renders automatically;
      - click an orbital outside the precalc range → it is enqueued and renders
        on completion;
      - clicking many orbitals in sequence: no freeze, no growing lag;
      - switch document while a parse/precalc is in flight → no stale table,
        no crash;
      - rotate/zoom with a surface displayed → smooth (no regression to the
        old tier).
      Verify: all rows pass; record in the session log.
- [x] 7.4 Perf sanity (local): time one Medium MO cube evaluation on a
      synthetic ~60-atom molecule before/after this change (or against a
      pre-change build) and record both numbers + peak RSS at a High grid.
      Verify: no per-point-object allocation confirmed by RSS delta; wall time
      not worse than before (parallelism ≥ old map path).
- [x] 7.5 Run the `reviewer` subagent on the complete diff (correctness +
      regression risk per repo policy: lock semantics, Qt threads, MSVC
      portability). Integrate accepted findings; reject with evidence.
- [x] 7.6 Trigger the Windows installer CI to prove MSVC safety
      (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`),
      watch both `build-installer` jobs (incl. `autooptreproTest` under
      `QT_QPA_PLATFORM=offscreen`). Verify: green before declaring done.
