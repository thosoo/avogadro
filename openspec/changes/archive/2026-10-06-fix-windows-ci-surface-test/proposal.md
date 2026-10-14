# Fix Windows CI: surfaceextensiontest fails to compile under MSVC

## Why

The `windows-installer.yml` CI workflow is red: the `build-installer (tests)` leg fails because `libavogadro/tests/surfaceextensiontest.cpp` cannot compile under MSVC. The test accesses `private` members of `SurfaceExtension` through a subclass, which is ill-formed standard C++ on every compiler. The Linux build only compiles because the test target carries GCC's `-fno-access-control`, which suppresses the access check; MSVC has no equivalent flag, so the ill-formed accesses surface as ten `error C2248` diagnostics and the build dies.

This also corrects a false root-cause record: an earlier diagnosis claimed this repo's toolchain "enforces Java/C#-style access semantics" (a non-standard compiler defect). That was wrong — GCC was correctly enforcing the C++ standard, exactly as MSVC does. The in-repo comments asserting that theory (test file header, `libavogadro/tests/CMakeLists.txt`) must be corrected as part of this change so they do not mislead future readers.

## What Changes

- Reclassify the `SurfaceExtension` members and helper the white-box test needs (`m_generation`, `m_runningGeneration`, `m_surfaceDialog`, `m_molecule`, `m_VdWsurface`, `loadBasis()`) from `private` to `protected` in `libavogadro/src/extensions/surfaces/surfaceextension.h`, so the test subclass accesses them legally on all compilers.
- Remove `target_compile_options(surfaceextensiontest PRIVATE -fno-access-control)` from `libavogadro/tests/CMakeLists.txt` — no longer needed once the accesses are legal.
- Rewrite the false "this environment's g++-13 enforces Java/C#-style access semantics" comments in `surfaceextensiontest.cpp` (header comment) and `libavogadro/tests/CMakeLists.txt` with an accurate note (white-box test uses `protected` access per standard C++).
- No production behavior change: no statements move, no logic edits, access-specifier keywords only. The test itself keeps asserting the same invariant (`m_runningGeneration == m_generation` after `calculate()`).

## Capabilities

### New Capabilities

(none — this is a build/portability fix inside an already-specified capability; no new behavior is introduced)

### Modified Capabilities

(none — the `document-switch-stability` requirements are unchanged; every requirement and scenario still holds. The regression test that exercises the "stale completion callbacks are harmless" requirement keeps its assertions and gains compiler portability.)

## Impact

- **Code**: `libavogadro/src/extensions/surfaces/surfaceextension.h` (access specifier only), `libavogadro/tests/CMakeLists.txt`, `libavogadro/tests/surfaceextensiontest.cpp` (comments only, plus nothing functional).
- **CI**: unblocks the `build-installer (tests)` leg of `.github/workflows/windows-installer.yml`; the `installer` leg and Linux `ci.yml` are already green and must stay green.
- **Risk**: low. `protected` widens access to future subclasses of `SurfaceExtension` only; no ABI/API surface used outside the class hierarchy changes. Secondary (unproven until it runs): the Windows tests leg also *executes* ctest; the test creates an unshown `SurfaceDialog` and runs a real VdW calculation, which is expected to be CI-safe but has not yet been demonstrated on a Windows runner.
