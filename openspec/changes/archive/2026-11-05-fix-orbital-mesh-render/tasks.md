# Tasks: Fix orbital mesh rendering on the modern tier

Implementation notes for the applying agent: follow `design.md` decisions D1–D5.
The build command for every step is `cmake --build build --parallel 8` (never any
other parallelism level). MSVC verification requires pushing the branch to GitHub and
taking the Windows Installer workflow from Actions (D5) — do not merge while either
CI leg is red.

## 1. Acceptance predicate

- [x] 1.1 Widen `Mesh::valid()` (`libavogadro/src/mesh.cpp:236`) so a mesh with
      `colors.size() == 0` and agreeing vertex/normal counts is valid; keep the
      existing `== 1` and `== vertices.size()` branches and keep mismatched
      vertex/normal counts invalid. Verify: `cmake --build build --parallel 8`
      compiles; full `ctest --test-dir build --output-on-failure` green (existing
      colored-mesh tests must not regress).

## 2. No silent mesh loss

- [x] 2.1 In `SceneBuilder::drawMesh`/`drawColorMesh`
      (`libavogadro/src/rendering/scenebuilder.cpp:151/166`), replace the silent
      `if (!mesh.valid()) return;` with: capture when valid (now includes uncolored
      meshes), otherwise emit a one-time diagnostic naming the mesh and reason
      (static-flag "report once" pattern, design D3). Verify: build `-j8`; a temp
      capture of a corrupt mesh logs exactly once and repeats silently.

## 3. Regression test with the real mesh shape

- [x] 3.1 New headless test `libavogadro/tests/meshcapturetest.cpp` (design D4):
      generate a mesh via `MeshGenerator` from a small cube WITHOUT calling
      `setColors`, assert `Mesh::valid()` is true, capture through
      `SceneBuilder::drawMesh` and `drawColorMesh`, assert the mesh store holds both
      instances with the expected instance color; assert a corrupt mesh
      (mismatched vertex/normal counts) is not captured and its diagnostic fired
      exactly once. Register the binary in `libavogadro/tests/CMakeLists.txt`,
      compiling `../src/rendering/scenebuilder.cpp` (and mesh/meshgenerator sources
      as needed) directly into the test following the existing pattern. Verify:
      `cmake --build build --parallel 8 && ctest -R meshcapture --output-on-failure`
      green; the test fails if `valid()` is reverted (discriminating power).

## 4. Verification

- [x] 4.1 Full suite: `cmake --build build --parallel 8 && ctest --test-dir build
      --output-on-failure` green (exit 0), including the existing mesh tests
      (`meshstream`, `meshrevision`, `consumerparity`, `meshgenparallel`).
- [x] 4.2 Live repro now passes: under `xvfb-run` with
      `LIBGL_ALWAYS_SOFTWARE=1`, open `testfiles/koffein_orca.out` (or an ORCA output
      with print directives), wait for precalculation, click an orbital row
      (e.g. HOMO), and confirm the blue/red isosurface lobes render on the canvas
      (screenshot comparison against the pre-fix bare-geometry state). Verify:
      surface visible both for a mid-precalculation click and a click on a completed
      orbital.
- [x] 4.3 Push the branch to GitHub and obtain the Windows Installer run from
      Actions (design D5): `gh workflow run 168745168 --ref <branch> --repo
      thosoo/avogadro`; watch both `build-installer` jobs and the test suite
      (incl. `autooptreproTest`) pass under `QT_QPA_PLATFORM=offscreen`. Verify:
      run conclusion=success; a red Windows leg blocks merge.
      Evidence: run 41903246600 conclusion=success; both build-installer jobs
      green (tests 50m31s, installer 47m47s); 100% tests passed, 0 failed out
      of 45; meshcaptureTest passed (0.09s); autooptreproTest passed (25.40s).
- [x] 4.4 Run the `reviewer` subagent on the complete diff (correctness + regression
      risk per repo policy); integrate accepted findings. Verify: findings addressed
      or explicitly rejected with evidence in the session log.
      Evidence: reviewer verdict "OK with notes, no blockers". Three P2 findings
      integrated: (1) CartoonEngine uncolored mesh -> drawColorMesh per-frame qDebug
      (DEFER: pre-existing CartoonEngine bug; visual parity maintained; D2 routes
      SurfaceEngine uncolored meshes via drawMesh); (2) report-once flag
      process-global/non-atomic (ACCEPT: design D3, repo-wide convention, pinned by
      test); (3) Windows/Qt 6.6.3 compile safety (TEST: proven by task 4.3).
