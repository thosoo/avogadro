# Design: fix-windows-ci-surface-test

## Context

`build-installer (tests)` (MSVC 14.5x, NMake, `ENABLE_TESTS=ON`) fails with ten `error C2248` diagnostics in `libavogadro/tests/surfaceextensiontest.cpp` (lines 48, 49, 52, 57, 63, 64, 70, 71, 107) for `m_generation`, `m_runningGeneration`, `m_surfaceDialog`, `m_molecule`, `m_VdWsurface`, and `loadBasis()`.

**Machine constraint (applies to every local command):** the implementation machine has limited RAM — parallel builds MUST be capped at 8 jobs (`--parallel 8` / `-j8`). Bare `--parallel`, `-j$(nproc)`, or uncapped `-j` will exhaust memory and fail the build. This is a standing constraint, not a one-off flag choice.

The test's `TestSurfaceExtension` subclasses `SurfaceExtension` and reads those members. In standard C++, `private` members are accessible only to members and friends of the declaring class; derived classes may access `protected` and `public` only. GCC compiles the current code solely because the test target carries `-fno-access-control`, which suppresses access checks for an otherwise ill-formed translation unit. MSVC has no such flag.

**Correction of record:** an earlier diagnosis (recorded in the test file header comment and the CMakeLists comment, and in a prior goal's audit trail) claimed this repo's g++-13 "enforces Java/C#-style access semantics" — a non-standard compiler defect. That claim is false. GCC was enforcing the C++ standard correctly; the oracle-run matrix ("protected compiles, private rejected") is simply the standard's definition of the two keywords. This change must replace those comments with accurate ones.

Current exact state that matters:

- `libavogadro/src/extensions/surfaces/surfaceextension.h` lines ~64–95: one `private:` block containing all data members; `bool loadBasis();` is also private at the end of the block.
- `libavogadro/tests/CMakeLists.txt`: `target_compile_options(surfaceextensiontest PRIVATE -fno-access-control)` sits directly under `target_compile_definitions(surfaceextensiontest PRIVATE TESTDATA_DIR=...)`, followed by a comment block asserting the false "Java/C#-style" theory.
- `libavogadro/tests/surfaceextensiontest.cpp` lines 1–11: header comment asserting the same false theory ("NB: this environment's g++-13 enforces Java/C#-style access control ...").
- The test's subclass `TestSurfaceExtension` also contains `setDialog()`, `setMoleculePtr()`, `disconnectVdWCallback()`, `waitForVdW()` helpers that use these members — all become legal once the members are `protected`.

## Goals / Non-Goals

**Goals:**
- `surfaceextensiontest` compiles under MSVC (and GCC/Clang) with no access-suppression flags.
- Windows `build-installer (tests)` leg goes green; Linux `ci.yml` stays green.
- In-repo comments state the real, standard-C++ rationale for the test's access pattern.

**Non-Goals:**
- No behavior change in `SurfaceExtension` itself — no statements move, no logic edits; only access-specifier keywords change.
- No new test scenarios; the existing regression assertion (`m_runningGeneration == m_generation` after `calculate()`) is preserved verbatim.
- No attempt to make other test targets MSVC-clean, and no restructure of the archived goal's audit records (the correction lives in this change, the commit message, and the repo comments).

## Decisions

**D1: `private` → `protected` for exactly the members the test needs.**

In `libavogadro/src/extensions/surfaces/surfaceextension.h`, change the access specifier for these six declarations from private to protected:

- `unsigned long m_generation;`
- `unsigned long m_runningGeneration;`
- `GLWidget* m_glwidget;` — *not required by the test; leave private*
- `SurfaceDialog *m_surfaceDialog;`
- `Molecule *m_molecule;`
- `VdWSurface *m_VdWsurface;`
- `bool loadBasis();`

Concrete restructure of the header block (lines ~64–95): split the single `private:` section into a `protected:` section holding the six names above (keep their original relative order and comments) followed by a `private:` section holding everything else (`m_cubes`, `m_moCubes`, `m_calculationPhase`, `m_glwidget`, `m_actions`, `m_basis`, `m_loadedFileName`, `m_progress`, `m_mesh1`, `m_mesh2`, `m_meshGen1`, `m_meshGen2`, `m_cube`, `m_qube`, `m_cubeColor`, and the remaining private helpers). Simplest correct edit: insert `protected:` before `m_calculationPhase`... **No** — simplest *minimal* edit that satisfies the test: put the six names into a `protected:` section and leave the rest private. Prefer moving the smallest set; do not flip the whole block to protected.

Rejected alternatives:
- `friend class TestSurfaceExtension;` — the test class lives in an anonymous namespace, which a production header cannot name portably.
- `#ifdef`-gated public accessors — adds production API surface and needs a new define plumbed into both platforms' test builds.
- Test via public API only — cannot assert the guard invariant `m_runningGeneration == m_generation`, which is the regression signal the whole test exists for.
- Excluding the target on Windows — hides the regression from the platform that caught it.

**D2: Remove `-fno-access-control`.**

Delete the `target_compile_options(surfaceextensiontest PRIVATE -fno-access-control)` line from `libavogadro/tests/CMakeLists.txt`. With D1 the accesses are legal; keeping the flag would mask future ill-formed access in this target and confuse readers.

**D3: Rewrite the false comments.**

Replace the "Java/C#-style access semantics / broken compiler" narrative in both files with a two-to-three-line accurate note, e.g.: *"This is a white-box regression test: TestSurfaceExtension accesses protected internals of SurfaceExtension (the N.1 generation guard members) to assert the stale-callback invariant. The members are declared `protected` for this purpose — standard C++; no access-suppression flags are used."* Also correct the stale rationale comment on the `m_runningGeneration = m_generation;` line in `surfaceextension.cpp` only if it references the false theory (check; it currently references OrbitalExtension, which is accurate — leave it).

## Risks / Trade-offs

- **Wider access for future subclasses**: any subclass of `SurfaceExtension` can now touch the six names. Accepted: the class is a plugin extension, not a public library type; `protected` is the standard testing idiom.
- **Windows test runtime unproven**: after the build fix, the tests leg runs `ctest --test-dir build -C Release`. The test constructs a `SurfaceDialog` (never `show()`n) and runs a real VdW calculation via `QThreadPool`. Expected to pass headless, but if the GUI widget creation fails in the CI service session, fall back to `QT_QPA_PLATFORM=offscreen` for the test process only — do not weaken the assertions.
- **Data file dependency**: the test reads `TESTDATA_DIR` (`${CMAKE_SOURCE_DIR}/testfiles`) for `koffein_orca.out` + `koffein_orca.molden`; both exist in the repo and ship with the checkout, so the Windows runner has them.
- **ABI**: access-specifier changes can alter nothing here (no layout change — order and types untouched), and `SurfaceExtension` is a plugin-local class.
