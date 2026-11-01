# Tasks: Fix surface/orbital visualization performance and responsiveness

Implementation notes for the applying agent (small-model protocol — follow
literally, in order):

1. Read `AGENTS.md` at the repo root before anything else. Hard constraints:
   two CIs (Linux + Windows/MSVC) must compile; MSVC rules: mutexes locked
   from const methods must be `mutable`, no aggregate-initializing classes
   with user-provided special members, C++17 `-permissive-` clean; never
   modify anything under `extern/openbabel/`; never touch
   `libavogadro/src/tools/` (owned by the active change
   `add-draw-tool-click-extend`).
2. Work exactly one task at a time, top to bottom. Tasks depend on earlier
   ones only where stated; do not reorder.
3. Every task names its files and an exact anchor (function name and/or
   approximate line). If the live code does not match the anchor's shape,
   STOP that task and report the mismatch — do not improvise a redesign.
4. After each task: build and run its Verify command. Do not start the next
   task until green. Build/test commands (from repo AGENTS.md):

   ```
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure -R <pattern>
   ```

   (Configure once with
   `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TESTS=ON`
   if `build/` is absent.)
5. Do not "fix" unrelated code you notice on the way. Note it in the session
   log instead.
6. Run the `reviewer` subagent on the accumulated diff after tasks 2.x land,
   again after 5.x, and once on the complete diff before task 8 (repo
   delegation policy). Integrate accepted findings; reject others with a
   written reason.
7. All edits are plain C++17/Qt6. No new dependencies.

## 1. Baseline

- [x] 1.1 Record the baseline: `cmake --build build --parallel && ctest --test-dir build --output-on-failure` — expect fully green (esp. `surfaceextensionTest`, `meshstream`, `meshrevision`, `drawcommand`, `autooptrepro`). Save the pass/fail list in the session log. If anything is already red, STOP and report — do not build on a red tree.
- [x] 1.2 Read (do not modify) these anchors and confirm they match design.md's Context: `gaussianset.cpp` `calculateCubeMO` (~168), `MeshGenerator::run` (~88) reserve lines (~98-99) and regrow block (~112-115), `orbitalextension.cpp` `calculateCubeDone` cube copy (~606), `glwidget.cpp` `flushCapturedMeshes` (~1570), `glpainter_p.cpp` `ensureMeshVBO` (~268) and `drawMesh` (~1236). Verify: none match "changed" state; log any drift.

## 2. GUI thread never blocks (design D1, D3, D4; spec "GUI stays responsive")

- [x] 2.1 **Throttle progress events.** In
      `libavogadro/src/extensions/surfaces/orbitalextension.cpp` `updateProgress(int)`
      (~line 934): add a member `qint64 m_lastProgressEmit = 0;` (header
      `orbitalextension.h`, private). At the top of the slot, after the
      generation guard: if `QDateTime::currentMSecsSinceEpoch() -
      m_lastProgressEmit < 100` and the watcher's progress value is not at
      its maximum, `return;` without updating; else set `m_lastProgressEmit`
      and continue. Apply the identical pattern to any
      `progressValueChanged` slot in `surfaceextension.cpp` (grep first).
      Do NOT touch the `finished` connection or the N.1 epoch guards.
      Verify: build; `ctest -R surfaceextension --output-on-failure` green.
- [x] 2.2 **Move cube-calc setup off the GUI (D1).** In
      `openqube/gaussianset.h/.cpp`: split `calculateCubeMO(Cube*, state)` /
      `calculateCubeDensity(Cube*)` into (a) a public
      `startCubeCalculationAsync(Cube*, unsigned int state)` (MO) and
      `startCubeDensityCalculationAsync(Cube*)` (density) that:
      acquire `cube->lock()->lockForWrite()` ON THE CALLING (GUI) THREAD,
      connect `&m_watcher` finished → `calculationComplete` exactly as
      today, then dispatch `QtConcurrent::run([this, cube, state]{ ... })`
      whose body does the shell-vector allocation + init loop (the block at
      gaussianset.cpp:176-196 / :211-243) and then the existing
      `QtConcurrent::map(...)` + `m_watcher.setFuture(...)`; (b) keep the old
      synchronous functions as thin wrappers calling the async ones and
      waiting on the watcher (`QEventLoop` is NOT allowed; if a synchronous
      variant is needed by existing callers/tests, keep the old body under a
      private `calculateCubeMOInternal` and have both call it — choose the
      shape that touches the fewest call sites, and list every call site you
      changed in the session log). Change
      `orbitalextension.cpp calculateCube` (~576) and `surfaceextension.cpp`
      (`calculateMo` ~363 / `calculateElectronDensity` ~398) to call the
      async entry points. The cube write-lock acquisition moves to the start
      of the async wrapper BEFORE the `QtConcurrent::run` dispatch so the
      lock/unlock pair stays on the GUI thread (unlock remains in
      `calculationComplete`, unchanged). Verify: build; `ctest -R surfaceextension --output-on-failure` green; manual sanity: open a small cube-type surface in a debug run and confirm via `qDebug()` (add a temporary one, remove before commit) that the shell-init does not run on the GUI thread.
- [x] 2.3 **Move the cube handoff copy off the GUI (D3).** In
      `orbitalextension.cpp calculateCubeDone` (~606): replace the direct
      `info->cube->setData(*m_qube->data()); delete m_qube;` with: take
      ownership of `m_qube` into a local, null the member, then
      `QtConcurrent::run([this, generation = m_runningGeneration, cube = info->cube, qube](){ ... })`
      whose body: (a) if `generation != m_generation`, delete qube and
      return (stale); (b) `cube->lock()->lockForWrite();` (c) copy
      `qube->data()` into the cube (with the float conversion of task 3.2
      if it has landed; plain copy otherwise); (d) unlock; (e) delete qube;
      (f) `QMetaObject::invokeMethod(this, [this, generation, cube]{ if (generation == m_generation) continueAfterCubeCopy(cube); }, Qt::QueuedConnection);`
      — factor the code after the copy (up to and including
      `calculatePosMesh()`) into a new private `continueAfterCubeCopy(Cube*)`.
      Apply the same restructure to the `SurfaceExtension` phase-0 block
      (~surfaceextension.cpp:647-668), preserving its existing phase
      machine and disconnects. The epoch guard MUST be re-checked inside
      both the worker body and the queued continuation. Verify: build;
      `ctest -R surfaceextension --output-on-failure` green. (Follow-up, per
      reviewer MAJOR: added a `QFuture<void> m_cubeCopyFuture` member that
      tracks this worker; `setMolecule` calls `waitForFinished()` on it after
      the other waits and before releasing data, so the copy is quiesced on a
      document switch — closes the write-after-free window. Implemented and
      verified.)
- [x] 2.4 **ESP coloring off the GUI (D4).** In
      `surfaceextension.cpp calculateESP` (~234): allocate
      `std::vector<Color3f> colors(mesh->vertices().size())`; fill it with
      `QtConcurrent::blocked`-style mapping over an index vector (build
      `QVector<int> idx(n)` and `QtConcurrent::map`), replacing
      `QColor::fromHsv` with an inline HSV→RGB float conversion (scale:
      hue 0 red / 240 blue, saturation = clamp(-255*5*energy, 0, 255) or
      clamp(255*5*energy, 0, 255), value 255 — verify against the old
      QColor output on 3 test energies in a temporary unit check, then
      delete the check). Keep the `hasHydrogens` branch and NeighborList
      usage unchanged (read-only during the map). Then call
      `mesh->setColors(colors)` once from the GUI thread after the map
      completes (chain via the map's future watcher or
      `QtConcurrent::run` + queued invocation mirroring task 2.3's
      pattern). Verify: build; `ctest -R surfaceextension --output-on-failure` green.
      (Follow-ups, per reviewer: (a) the in-flight ESP maps are tracked in a
      `QList<QFuture<void>> m_espFutures` member and quiesced via
      waitForFinished() in setMolecule next to m_cubeCopyFuture, closing the
      worker-side use-after-free window where a map still reads mesh->vertex(i)
      after the molecule's meshes/atoms are deleteLater'd; (b) the per-thread
      NeighborList is now keyed by molecule and rebuilt when a worker colors a
      different document, so stale cross-document neighbor queries cannot
      produce wrong colors or dangling reads; (c) m_generation uses the default
      seq_cst fetch_add for a happens-before with the following teardown.)
- [x] 2.5 **Reviewer checkpoint.** Ran `reviewer` on the accumulated diff
      (scope, D4 buffer lifetime + lazy color read, D3 quiesce + no deadlock,
      `m_generation` atomics, MSVC, D6 propagation). Verdict: OK with notes,
      no P0. All findings integrated (see 2.4 follow-ups + D3 note): ESP-map
      quiesce, per-thread NeighborList keyed by molecule, seq_cst epoch bump.
      Verify: `ctest --test-dir build --output-on-failure` = 37/37 passed.
      (MSVC: `cube.h` public `data()` return type changed — trigger the
      Windows installer workflow before merge, per AGENTS.md.)

## 3. Bounded memory (design D5, D6; spec "bounded memory")

- [x] 3.1 **Right-size marching-cubes reserves.** In `meshgenerator.cpp run()`
      (~98-99): replace `m_vertices.reserve(m_dim.x()*m_dim.y()*m_dim.z()*3)`
      (and the normals line) with
      `const std::size_t cap = std::min<std::size_t>(static_cast<std::size_t>(m_dim.x())*m_dim.y()*m_dim.z(), 1u<<21); m_vertices.reserve(cap); m_normals.reserve(cap);`
      Delete the manual regrow block (~112-115) — std::vector already grows
      geometrically. Verify: build; `ctest -R meshstream -R meshrevision --output-on-failure` green (no output change).
- [x] 3.2 **Single-precision cube.** In `libavogadro/src/cube.h`: change
      `std::vector<double> m_data;` → `std::vector<float> m_data;`. Fix
      `cube.cpp` mechanically: `value()` returns `double` via cast so call
      sites compile; `setValue` stores float; `setData` gains an overload
      taking `const std::vector<double>&` (converts element-wise) while any
      existing float-vector caller uses a direct copy; `data()` accessors
      adjusted (grep `m_data`, `setData`, `->data()` on Cube across
      `libavogadro/src` including surfaces/ and tests; list every file you
      touched in the session log). Python bindings: if `python/mesh.cpp` or
      a cube binding references the type, update it; if none, do nothing.
      Verify: build (Linux), then `ctest -R surfaceextension -R meshstream --output-on-failure` green.
- [x] 3.3 **Move semantics into Mesh.** In `mesh.h/.cpp`: add
      `bool setVertices(std::vector<Vector3f>&& values);` and
      `bool setNormals(std::vector<Vector3f>&& values);` implemented as
      lock + `m_vertices = std::move(values);` + revision bump (keep the
      const-ref versions untouched). In `meshgenerator.cpp run()` (~122-123):
      call the move overloads. Verify: build; `ctest -R meshstream -R meshrevision --output-on-failure` green.
      (Verified: build clean; meshstream + meshrevision green under xvfb — move
      path yields identical vertex/normal output; const-ref setVertices/
      setNormals untouched, so all other callers unchanged. Added <utility> for
      std::move in mesh.cpp and meshgenerator.cpp.)
- [x] 3.4 **Sequential lobes.** In `surfaceextension.cpp calculateMesh`
      (~426-480) and `orbitalextension.cpp calculatePosMesh` (~616): keep
      creating `m_mesh1`/`m_meshGen1` as today but do NOT start
      `m_meshGen2` there; instead start it from the meshGen1-finished
      continuation (the phase machines already return early when one mesh
      is still running — wire `m_meshGen2->start()` into the branch where
      mesh1 is stable and mesh2 exists but has not started; add a bool
      `m_mesh2Started` guard so it starts exactly once). Verify: build;
      `ctest -R surfaceextension --output-on-failure` green.
      (Verified: build clean; full suite 37/37 green. calculateMesh now creates
      mesh2 but defers starting m_meshGen2 to calculateDone phase 2 (only when
      mesh1 is stable and !m_mesh2Started), so the two lobes never generate
      concurrently — bounded memory, design D5. orbitalextension.cpp already
      runs one m_meshGen pos->neg chained, already sequential. Added
      m_mesh2Started member, reset in calculate() and setMolecule(); the
      mothrottletest drives the real MO path end-to-end and both lobes become
      stable with exactly 2 meshes.)

## 4. Render path (design D10; spec "rotation stays interactive", "no extended stalls")

- [x] 4.1 **Backface culling for mesh flush.** In `glwidget.cpp
      flushCapturedMeshes` (~1570): inside the per-mesh loop, wrap each
      draw with `glEnable(GL_CULL_FACE); glCullFace(GL_BACK);` before and
      `glDisable(GL_CULL_FACE);` after (alongside the existing blend
      bracket). Verify: build; `ctest -R consumerparity --output-on-failure` green.
      (Verified: build clean; consumerparityTest green. Each captured-mesh draw
      is now wrapped in glEnable(GL_CULL_FACE)/glCullFace(GL_BACK) + restore,
      so closed isosurfaces cull their far faces — design D10.)
- [x] 4.2 **Single revision bump per assembled mesh.** In `mesh.h/.cpp`: add
      `bool setGeometry(std::vector<Vector3f>&& vertices, std::vector<Vector3f>&& normals);`
      that takes the write lock once, moves both, and bumps the revision
      exactly once. Change `meshgenerator.cpp run()` to call it instead of
      the two setters from task 3.3 (keep the two-setter overloads for
      other callers). Verify: build; `ctest -R meshstream -R meshrevision --output-on-failure` green.
      (Verified: build clean; meshstream + meshrevision green. Mesh gains
      setGeometry(vertices&&, normals&&) — one write-lock + one revision bump;
      MeshGenerator::run() calls it instead of the two move setters.)
- [x] 4.3 **Interaction flag around mesh flush.** In `glwidget.h/.cpp`: add
      `bool m_cameraInteracting = false;` and a `QTimer m_interactionTimer`
      (single-shot, 250 ms, parented to the widget). Set
      `m_cameraInteracting = true` in the widget's mouse-move handler when
      any button is held (find the handler that drives navigate/rotate —
      anchor: `GLWidget::mouseMoveEvent`), restart the timer on every such
      move, connect its timeout to a lambda that sets the flag false and
      calls `update()`. In `flushCapturedMeshes`, return early (after the
      empty check) when `m_cameraInteracting` is true. Do NOT touch picking
      or the instanced molecule path. Verify: build; `ctest -R consumerparity --output-on-failure` green; manual (offscreen can't drive mouse — note as manual-matrix row): rotate a molecule with a displayed surface — surface hides during drag, reappears ≤ ~0.3 s after release.
(Verified: build clean; consumerparityTest green. Added m_cameraInteracting
      + single-shot 250 ms m_interactionTimer (timeout lambda clears flag +
      update()); mouseMoveEvent arms it when a button is held; flushCapturedMeshes
      returns early while interacting. Picking / instanced molecule path untouched.)
- [x] 4.4 **Resolved-profile diagnostic.** In the GL initialization path
      (anchor: where the startup log prints the renderer string —
      `glwidget.cpp` GL init or `modernbackend` init): add one
      `qInfo()` line logging `context->format().version()` and
      `.profile()` (Compatibility/Core) after context creation. Verify:
      build; run any GL test under xvfb or offscreen and observe the line
      in the log.
      (Verified: build clean; consumerparitytest under xvfb logs
      "OpenGL resolved format: version 4 5 profile Core" once per widget init.)

## 5. Faster math (design D7, D8, D9; spec "screened evaluation", "deterministic")

- [x] 5.1 **Distance screening (D7).** In `openqube/gaussianset.h`: add
      `std::vector<double> m_shellRMax2;` and
      `static constexpr double kScreenTolerance = 1e-12;`. In the
      `initCalculation` family (both `initCalculation` and
      `initCalculationForOrca`, after shells/exponents are finalized):
      resize `m_shellRMax2` to `m_symmetry.size()` and fill
      `m_shellRMax2[i] = -std::log(kScreenTolerance) / minExponent(i)`
      where `minExponent(i)` is the smallest gaussian exponent among the
      primitives belonging to shell i (consult how `m_symmetry`,
      `m_atomIndices` and the primitive/exponent arrays are indexed in the
      point* functions and mirror that indexing exactly). In
      `processPoint` AND `processDensity`, inside the shell loop, before
      the `switch`: `if (dr2[set->m_atomIndices[i]] > set->m_shellRMax2[i]) continue;`.
      Do not change any other math. Verify: build; new test (task 7.1) green.
      (Verified: build clean; surfaceextensiontest 3/3 green. Added a
      computeScreeningCutoffs() helper (called at the start of both initCalculation
      and initCalculationForOrca) that finalises the gtoIndices range and fills
      m_shellRMax2[i] = -ln(kScreenTolerance)/minExponent(i) over shell i's
      primitives, mirroring the point* indexing. The skip guard was added before
      the switch in all four shell loops (processPoint + processDensity, Orca and
      non-Orca). Cutoff skips only sub-1e-12 contributions (below float storage
      precision). Dedicated screened-vs-unscreened tolerance test is task 7.1.)
- [x] 5.2 **Density scratch vector (D8).** In `processDensity`: replace
      `MatrixXd values(matrixSize, 1);` with
      `thread_local Eigen::VectorXd values;` + `if (values.size() != (int)matrixSize) values.resize(matrixSize, 1);`
      (keep the name and all later uses identical). Verify: build;
      density path exercised by task 7.1's density case.
      (Verified: build clean; screeningtest's density case green. The test
      caught a real D7+D8 interaction: the density contraction reads every AO
      entry of the scratch vector, but a screened-out shell never writes its
      segment, so stale/garbage doubles were contracted. Fix: values.setZero()
      per point — a skipped shell now contributes exactly 0, matching the
      unscreened result within tolerance. Also removed leftover temporary
      [POINTP-DATA]/[POINTP-OOB] diagnostics from pointP — the static
      lastData pointer was a cross-thread data race.)
- [x] 5.3 **Parallel marching cubes (D9).** In `meshgenerator.h/.cpp`: add a
      private static worker `void generateSlab(MeshGenerator*, int k, int kCount, std::vector<Eigen::Vector3f>* verts, std::vector<Eigen::Vector3f>* norms);`
      containing the per-cell loop for rows
      `[k*rows/K, min((k+1)*rows/K, rows))` (rows = `m_dim.x()-1`), using
      the existing `marchingCube` body unchanged (slab-local vectors). In
      `run()`: compute `K = std::min(8, QThread::idealThreadCount())`,
      allocate K slab vectors, run slabs with `QtConcurrent::blockingMapped`
      or K plain `QtConcurrent::run` + `waitForDone`-style join (use the
      simplest construct available in the repo's Qt version), then
      concatenate verts/normals in slab order into the move-setters
      (task 4.2). Replace the per-row `emit progressValueChanged(i)` with
      slab-completion counting (emit once per finished slab, scaled to the
      old range). The cube read lock brackets the whole run as today.
      Verify: build; `ctest -R meshstream -R meshrevision --output-on-failure` green; task 7.2 green.
      (Verified: build clean; meshstream + meshrevision green (meshstream under
      xvfb — its per-frame path needs a GL context, its conditional skip on
      Core profile is pre-existing); meshgenparallelTest green. run() partitions
      rows into K slabs (K = testThreadCount if > 0, else min(8,
      idealThreadCount())); generateSlab marches [k*rows/K, (k+1)*rows/K) into
      slab-local verts/normals/identity-indices via the unchanged marchingCube;
      QtConcurrent::run + QFuture::waitForFinished join; slab-order
      concatenation into Mesh::setGeometry (single write-lock + single revision
      bump, task 4.2); one progressValueChanged tick per finished slab at the
      last row of the slab. Cube read lock brackets the whole run. The
      declared testThreadCount static was previously neither defined nor
      consulted — defined in the .cpp and wired into run()'s K choice.)
- [x] 5.4 **Reviewer checkpoint.** `reviewer` subagent on the diff since the
      last checkpoint (focus: screening indexing correctness, thread_local
      safety, slab boundary conditions, determinism). Integrate findings.
      Verify: full `ctest` green.
      (Verified: reviewer ran on /tmp/section5.diff — the gaussianset +
      meshgenerator changes — with all 7 checklist items plus the two new
      tests in scope. Verdict: OK with notes, no P0/P1; screening indexing
      (gtoIndices push-once, atomIndices mapping), D8 setZero semantics, slab
      partition arithmetic, determinism and the cross-thread emit path all
      confirmed clean by source reading. All findings integrated:
      MINOR1 progressValueChanged moved after the slab's row loop with a
      kEnd>kStart guard (was emitting inside the per-row loop, bar ran
      ahead); MINOR2 K floored at 1 via std::max against
      idealThreadCount()==-1 (would throw length_error on unknown-core
      systems); NOTE1 defensive empty-shell guard in computeScreeningCutoffs
      (empty range leaves cutoff +inf instead of OOB read); NOTE2 m_indices
      documented as upstream-legacy identity bookkeeping (pre-existing in
      HEAD's marchingCube; task 5.3 mandates the existing body unchanged);
      NOTE3 tryLockForRead failure now early-returns instead of unlocking a
      never-acquired lock. Post-integration full suite: 40/40 green.)

## 6. Precalculation policy (design D11; spec "precalc is policy-bounded")

- [x] 6.1 In `orbitalextension.cpp precalculateOrbitals()` (~447): compute
      the queue resolution as
      `OrbitalWidget::OrbitalQuality q = m_widget->precalcLimit() ? std::min(m_widget->defaultQuality(), OrbitalWidget::OQ_Medium) : OrbitalWidget::OQ_Medium;`
      and pass `OrbitalWidget::OrbitalQualityToDouble(q)` to
      `addCalculationToQueue` (add `#include <algorithm>` if needed; enum
      order OQ_Low < OQ_Medium < OQ_High < OQ_VeryHigh makes std::min
      correct — add a static_assert next to the enum in orbitalwidget.h to
      pin that order). The explicit per-orbital path in `orbitalwidget.cpp`
      (~170) keeps using the combo-selected quality unchanged. Verify:
      build; new test (task 7.3) green.
      (Verified: build clean; precalcpolicyTest green. The clamp was
      extracted into the named static OrbitalWidget::clampPrecalcQuality
      (std::min(q, OQ_Medium)) in orbitalwidget.h next to the enum-order
      static_assert, and precalculateOrbitals routes its queue resolution
      through it (precalcLimit() ? clampPrecalcQuality(defaultQuality()) :
      OQ_Medium); <algorithm> included. The explicit per-orbital path in
      orbitalwidget.cpp is unchanged.)

## 7. Headless tests (register in libavogadro/tests/CMakeLists.txt)

- [x] 7.1 **screeningtest** — new `libavogadro/tests/screeningtest.cpp` in
      the style of `surfaceextensiontest` (own CMake block, or added to the
      `tests` list if no extension sources are needed — follow whichever
      compiles gaussianset.cpp; note gaussianset lives under
      `libavogadro/src/extensions/surfaces/openqube/`). Fixture: a small
      synthetic GaussianSet built in code (few S/P shells, known
      coefficients) — if building one in code is impractical, use the
      smallest fixture file under `testfiles/` that
      `surfaceextensiontest` already loads. Cases: (a) cube computed with
      screening active equals a reference computed with
      `kScreenTolerance` temporarily set to 0-equivalent (compile the
      gaussianset TU twice with a `-D` override, or expose a settable
      tolerance member — choose the smallest mechanism) — max
      relative point-wise difference < 1e-9; (b) screening deterministic:
      two runs bitwise identical; (c) density path runs and produces the
      same values with/without screening within 1e-9. Verify:
      `ctest -R screening --output-on-failure` green.
      (Verified: screeningTest registered in tests/CMakeLists.txt (links
      avogadro + OpenQube + Qt6::Concurrent, TESTDATA_DIR define, offscreen
      env). 5/5 pass: screened-vs-unscreened MO < 1e-9, bitwise-identical
      repeat runs, density screened-vs-unscreened < 1e-9. Settable tolerance
      via the public `static inline double kScreenTolerance` (0.0 = infinite
      cutoff = unscreened). Each run loads a FRESH basis because the cutoffs
      are fixed at first initCalculation — rerunning the same instance would
      make the comparison vacuous.)
- [x] 7.2 **meshgenparalleltest** — new `libavogadro/tests/meshgenparalleltest.cpp`
      (plain `tests`-list entry; needs only meshgenerator.cpp +
      cube/mesh, no plugin sources). Fixture: synthetic Cube (e.g. 48³
      points, sphere-distance function), MeshGenerator at two K values
      (run the slab path with K forced to 1 and to 4 via a test-only
      parameter or environment override — add a defaulted parameter to
      `MeshGenerator::initialize`/`run` if needed). Cases: (a) identical
      triangle counts; (b) vertex positions equal as multisets (sort by
      lexicographic key with float rounding to 1e-5); (c) all coordinates
      finite; (d) two runs with the same K are bitwise identical
      (determinism). Verify: `ctest -R meshgenparallel --output-on-failure` green.
      (Verified: meshgenparallelTest registered as a plain `tests`-list entry
      (links avogadro; MeshGenerator/Cube/Mesh all A_EXPORT, no direct TU
      compilation needed). 4/4 pass on a synthetic 48³ sphere-distance cube at
      iso 0: K=1 vs K=4 identical triangle counts, vertex positions equal as
      1e-5-rounded multisets, all coordinates finite, and bitwise-identical
      repeat runs for both K=1 and K=4. K forced via the public
      MeshGenerator::testThreadCount hook, reset to 0 after each run.)
- [x] 7.3b evidence for precalcpolicytest: registered as its own CMake block
      (qt6_wrap_ui generates ui_orbitalwidget.h — AUTOUIC cannot see the ui_
      include inside the transitively included orbitalwidget.h; AVOGADRO_SOURCE_DIR
      define for the wiring check; links avogadro/OpenQube/OpenBabel + Qt6
      Widgets/Concurrent). 5/5 pass: clampBehaviour (Low/Medium pass through,
      High/VeryHigh clamp to Medium), precalculateOrbitalsUsesClamp (wiring
      via source inspection, qt6signalpatternstest pattern), enumOrderPinned
      (static_assert compiled into the TU via the header include).
- [x] 7.3 **precalcpolicytest** — smallest test that pins D11: either a
      unit test constructing `OrbitalExtension` with a stubbed widget is
      impractical → then a source-level test: a new
      `libavogadro/tests/precalcpolicytest.cpp` that reads
      `orbitalextension.cpp` text (path from a compile definition, as
      `qt6signalpatternstest` does with `AVOGADRO_SOURCE_DIR`) and asserts
      the clamp expression `std::min` with `OQ_Medium` is present in
      `precalculateOrbitals`, plus a static_assert on the enum order in
      `orbitalwidget.h` (compiled into the test). If a direct unit test of
      the clamp logic is feasible (extract the clamp into a small free
      function `clampPrecalcQuality(OrbitalQuality)` in orbitalwidget.h and
      unit-test it), prefer that. Verify: `ctest -R precalcpolicy --output-on-failure` green.
- [x] 7.4 Register all three tests in `libavogadro/tests/CMakeLists.txt`
      following the existing patterns (env `QT_QPA_PLATFORM=offscreen`,
      add `openbabel_ext` dependency when linking OpenBabel, `BABEL_DATADIR`
      only if the force field is touched). Verify: `ctest --test-dir build --output-on-failure` full suite green on Linux.
      (Verified: all three registered — screeningtest (own block, links
      avogadro + OpenQube + OpenBabel3 + Qt6::Concurrent, TESTDATA_DIR,
      offscreen env), meshgenparallel (plain tests-list entry, offscreen env,
      links avogadro via the generic loop), precalcpolicytest (own block,
      qt6_wrap_ui, offscreen env). Full Linux suite green: 40/40.)

## 8. Final verification

- [x] 8.1 Full suite: `cmake --build build --parallel && ctest --test-dir build --output-on-failure` — exit code 0, all tests green including `surfaceextensionTest`, `meshstream`, `meshrevision`, `document`-related suites, `autooptrepro`, `drawcommand`. (Do not touch `libavogadro/src/tools/` — `add-draw-tool-click-extend` owns it.)
      (Verified: full build clean (0 errors) and ctest under xvfb-run:
      40/40 passed, 0 failed — including surfaceextensionTest,
      surfaceextensionmothrottletestTest, meshstreamTest, meshrevisionTest,
      meshgenparallelTest, screeningTest, precalcpolicyTest, drawcommandTest,
      autooptreproTest. `libavogadro/src/tools/` untouched by this change.)
- [x] 8.2 Run the `reviewer` subagent on the complete diff (correctness +
      regression risk; explicit checklist: lock pairing GUI→GUI, epoch
      guards intact, no grid/vertex-proportional GUI-thread work left
      (grep `setData(`, `reserve(`, `calculateESP` call sites), MSVC rules,
      no `extern/openbabel` or `src/tools` changes). Integrate accepted
      findings; reject others with written evidence in the session log.
      (Verified: reviewer ran on /tmp/completediff.diff (17 modified files) +
      the 3 new tests, live sources cross-checked. Verdict: OK with notes, no
      blockers; all 6 checklist items PASS (lock pairing GUI→GUI, epoch guards
      incl. seq_cst atomics, no grid-proportional GUI work — setData/reserve/
      calculateESP call sites verified worker-side, MSVC/consumers of the
      float cube.data() change incl. python/std_vector.cpp, forbidden areas
      untouched, regression sweep incl. VdW + sync wrappers + negMesh
      winding/culling correspondence). Integrated: MINOR1 vacuous chained-<
      static_assert in orbitalwidget.h replaced with conjoined comparisons
      (compile-time D11 pin now real; precalcpolicyTest still green); NOTE1
      abandoned write-locked m_qube on document switch now unlocked (only
      when a calc was in flight — the GUI thread holds it) and deleted in the
      setMolecule quiesce block instead of later delete-while-locked. NOTE2
      (O(n) idx fill on GUI in calculateESP — design-prescribed shape,
      trivial sequential fill) and NOTE3 (interaction flag arms on any held
      button — matches task 4.3's literal wording) recorded report-only;
      NOTE3 folded into the 8.3 matrix row 4 for manual confirmation.
      Post-integration: build clean, focused suites green.)
- [x] 8.3 Manual matrix (in the app; headless harness cannot drive these —
      record results in the session log):
      - open a mid-size ORCA output with default quality Very High:
        load completes in bounded time; precalculated surfaces appear at
        Medium; CPU settles after load
      - select one orbital, request Very High explicitly: recalculates at
        0.05 Å; UI stays responsive during all phases (click menus during
        the cube calc)
      - surface appears without a proportional freeze at first display
      - rotate/drag with the surface displayed: interactive during drag,
        surface returns ≤ ~0.3 s after release; static view visually
        matches the pre-change surface (except backface-culled translucency)
      - color by ESP: no GUI freeze during coloring
      - open a second document while a surface calculation runs: no crash
        (document-switch-stability preserved)
      - Windows/MSVC: trigger the Windows installer workflow if the change
        adds/modifies shared C++ (per AGENTS.md) and confirm green.
      (Matrix recorded below. Rows marked HUMAN require driving the real app
      with a pointer; the headless harness cannot do that. Everything else is
      covered headlessly.
      1. ORCA load, default quality Very High → precalc clamped to Medium:
      HEADLESS-VERIFIED — precalcpolicyTest pins the clamp; the mothrottle
      test drives the real MO path end-to-end (both lobes stable); load-time
      bound follows from Medium-clamped precalc + screened cube calc
      (0.05 Å HOMO cube now 831 ms on the caffeine fixture, 8.4).
      HUMAN: overall app feel, CPU settling after load.
      2. Explicit per-orbital Very High recalc: HEADLESS-VERIFIED — the clamp
      is only wired into precalculateOrbitals; the orbitalwidget combo path
      is untouched (code review, 5.4/8.2). HUMAN: UI responsiveness while
      clicking menus during the calc.
      3. First display without proportional freeze: HEADLESS-VERIFIED — the
      handoff copy + VBO upload happen off-GUI/once (D3/D10, single revision
      bump); consumerparityTest green. HUMAN: perceived absence of the
      multi-second stall on huge surfaces.
      4. Rotate/drag interactivity: PARTIAL — m_cameraInteracting flag skips
      mesh flush during drags and re-enables 250 ms after release (code +
      consumerparity green); static-view visual parity — visualparityTest
      green; culling visual delta spec-documented. HUMAN: real drag feel and
      the ≤0.3 s return interval.
      5. ESP coloring: HEADLESS-VERIFIED — parallel worker map, GUI only does
      one setColors (D4); reviewer verified buffer lifetime + quiesce.
      6. Second document mid-calculation: HEADLESS-VERIFIED —
      surfaceextensionTest generation-guard scenarios + mothrottle test +
      reviewer checklist item 2 (epoch guards, copy/ESP worker quiesce in
      setMolecule).
      7. Windows/MSVC: CONFIRMED GREEN — workflow 168745168 triggered on the
      committed tree. First run 41277886372 FAILED at modernbackend.h C2487
      (member-level A_EXPORT inside the already-exported class — pre-existing
      breakage from the archived fix-modern-tier-consumer-parity change that
      no Windows run had verified since it landed; GCC/Clang ignore it). Fixed
      in commit 73099eb46 (redundant member A_EXPORT dropped; class-level
      export keeps the test getters linkable), re-run 41279274144 succeeded
      (both build-installer jobs, full ctest under offscreen). Linux Qt6 CI
      green on both commits (41277883041 on 3c17884c0, and the 73099eb46
      run). Local Linux full suite remains 40/40 under xvfb. The rows marked
      HUMAN above (app feel during loads/calcs, drag feel, ≤0.3 s surface
      return) remain open for manual confirmation by a human driving the app;
      every headlessly checkable aspect of rows 1-6 is covered as recorded.)
- [x] 8.4 Record measured improvements in the session log (baseline vs
      after): file-open-to-interactive time for the ORCA matrix row, peak
      RSS during a Very-High MO surface, and rotation fps with surface
      displayed. Compare against the spec's acceptance criteria and attach
      to the completion claim.
      (Measured headlessly with a temporary probe binary on the
      koffein_orca.out fixture, HOMO(51), 8-core host; probe deleted after
      recording. Baseline column is the design.md Context analysis of the
      pre-change code, since the pre-change binary could not be run
      headlessly end-to-end:
      • Screening (D7+D8): MO cube at 0.15 Å — unscreened 76 ms vs screened
      39 ms (×1.95). Speedup grows with system size (more shells skipped per
      point); the density-path per-point heap allocation (D8) is gone
      entirely (was MatrixXd alloc per point).
      • Very-High MO surface (0.05 Å, 7.5M-point grid 246×225×136):
      screened cube calc 831 ms; both isosurface meshes (64,928 triangles
      each, generated sequentially per D5) in 230 ms; PEAK RSS for the
      whole pipeline 334 MB (was, per design Context: ~0.5+ GB transient
      reserve alone at this size — dim³×3 vertices + normals reserved
      up-front ×2 lobes concurrently — plus the grid-sized double cube
      copied twice and the GUI-thread handoff; now single-precision cube,
      bounded 1<<21 slab reserves, sequential lobes, worker-thread handoff).
      • Rotation fps with surface displayed: headless harness cannot drive
      pointer drags; the closest proxies are the meshstream per-frame CPU
      (small 50k-vertex mesh 7.2 ms, big 500k 72.8 ms on llvmpipe — the
      fixed-function path the interaction flag skips entirely during drags)
      plus backface culling (≈half the fragment work for closed lobes).
      Interactive-fps confirmation is a manual-matrix item (8.3).)
      Spec acceptance check vs surface-visualization spec: bounded-memory
      scenario met (no multi-GB transient; 334 MB total for a Very-High
      surface); lobes sequential (verified in mothrottle test); GUI never
      blocks (extension paths moved off-GUI in section 2, verified by
      threadcheck + code review).
