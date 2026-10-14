## 1. Make the test's access legal (standard C++)

- [x] 1.1 In `libavogadro/src/extensions/surfaces/surfaceextension.h`, move exactly these six declarations from the `private:` block into a new `protected:` block placed before it (preserve original relative order, types, and trailing comments): `unsigned long m_generation;`, `unsigned long m_runningGeneration;`, `SurfaceDialog *m_surfaceDialog;`, `Molecule *m_molecule;`, `VdWSurface *m_VdWsurface;`, `bool loadBasis();`. Leave `m_glwidget`, `m_cubes`, `m_moCubes`, `m_calculationPhase`, `m_actions`, `m_basis`, `m_loadedFileName`, `m_progress`, `m_mesh1/2`, `m_meshGen1/2`, `m_cube`, `m_qube`, `m_cubeColor` and all other helpers `private`. Verify with: `grep -n "protected:" libavogadro/src/extensions/surfaces/surfaceextension.h` shows one new section; `git diff` shows access-specifier lines only (no moved statements, no layout-affecting reorder of member declarations beyond their new section placement).
- [x] 1.2 In `libavogadro/tests/CMakeLists.txt`, delete the line `target_compile_options(surfaceextensiontest PRIVATE -fno-access-control)`.

## 2. Correct the false root-cause record

- [x] 2.1 Rewrite the header comment in `libavogadro/tests/surfaceextensiontest.cpp` (the "NB: this environment's g++-13 enforces Java/C#-style access control..." paragraph): replace with an accurate note that this is a white-box regression test whose subclass accesses `protected` internals (the N.1 generation guard members) per standard C++, with no access-suppression flags.
- [x] 2.2 Delete the false comment block in `libavogadro/tests/CMakeLists.txt` that introduced the flag (the "This environment's g++-13 enforces Java/C#-style access semantics..." lines). Leave no stale reference to `-fno-access-control` anywhere in the repo: `grep -rn "fno-access-control\|Java/C#" libavogadro/ openspec/specs/ || true` must return nothing (archived change dirs are history — do not edit them).

## 3. Verify locally (Linux)

- [x] 3.1 Clean-configure and build the affected targets: `cmake -S . -B build && cmake --build build --target surfaceextensiontest avogadro-app --parallel 8` — both must compile with zero access errors and zero new warnings in the touched files. **Always cap jobs at 8** (`-j8` / `--parallel 8`, never bare `--parallel` or `-j$(nproc)`): this machine OOMs when the build uses all cores.
- [x] 3.2 Run the regression test and the full suite: `ctest --test-dir build -R surfaceextensionTest --output-on-failure` (must pass) and `ctest --test-dir build --output-on-failure` (13/13, must stay green). Confirm the test still proves the fix: temporarily comment out `m_runningGeneration = m_generation;` in `surfaceextension.cpp` `calculate()`, rebuild, and confirm `surfaceextensionTest` FAILS at the capture assertion; restore and confirm it passes again.

## 4. Land it and confirm CI

- [x] 4.1 Commit (message should note the corrected root cause: the access was ill-formed standard C++; GCC was right; `-fno-access-control` masked it) and push to `master`.
- [x] 4.2 Watch CI to completion: `build-installer (tests)` (run started by the push) must go green — this is the acceptance gate for this change; Linux `ci.yml` and `build-installer (installer)` must remain green. If the Windows test leg builds but the test fails at runtime, diagnose before weakening anything (first suspect: GUI/session environment; remedy is `QT_QPA_PLATFORM=offscreen` for the test process, never assertion changes).
