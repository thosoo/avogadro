# Tasks: OpenBabel via git submodule

## 1. Fork prerequisite (thosoo/openbabel)

- [x] 1.1 Port the `PatchOpenBabelMsvcTls.cmake` transformation as a normal commit in `thosoo/openbabel` (MSVC TLS declarations/definitions in `include/openbabel/typer.h` and `src/typer.cpp`); verify the patched file content matches what the script produces — compare patched-tree diff from a scratch MSVC-style run
  (landed: `f30fefc71` — patch script vs. pristine archive verified byte-identical per design.md handoff)
- [x] 1.2 Tag the commit (annotated, e.g. `avogadro-2026.10`) and push branch + tag; verify tag resolves (`git rev-parse avogadro-2026.10^{commit}`)
  (verified: annotated tag → `f30fefc71a914f98ba4bcb2e2f283d1f9c68ea47`, pushed to origin)

## 2. Submodule and build integration (avogadro)

- [x] 2.1 Add `.gitmodules` and submodule `extern/openbabel` pinned to the tagged fork commit; verify `git submodule status` shows the pin and `extern/openbabel` is populated (pinned to `f30fefc71`, tag `avogadro-2026.10`)
- [x] 2.2 Switch `ExternalProject_Add(openbabel_ext ...)` in `CMakeLists.txt` to `SOURCE_DIR ${CMAKE_SOURCE_DIR}/extern/openbabel` (drop `URL`, `URL_HASH`, download step); verify configure from a clean build dir completes and the OpenBabel install lands in `build/openbabel-install` (clean configure OK; openbabel_ext built; install at build/openbabel-install)
- [x] 2.3 Delete `cmake/PatchOpenBabelMsvcTls.cmake` and its `PATCH_COMMAND` wiring; verify no CMake file references the script (`grep -r PatchOpenBabelMsvcTls cmake CMakeLists.txt` is empty) (deleted; grep returns no matches)
- [x] 2.4 Add the fail-loud guard: configure aborts with `git submodule update --init` guidance when `extern/openbabel` content is missing; verify by temporarily emptying the submodule dir and confirming the error message and that no download is attempted (verified: configure aborts with guidance, exit non-zero, then restored clean)
- [x] 2.5 Confirm no broad `file(GLOB ...)` in Avogadro's CMake recurses into `extern/`; verify Avogadol source lists unchanged (`grep -rn "GLOB" libavogadro CMakeLists.txt cmake`) (all GLOBs scoped to specific dirs; none reference extern/)

## 3. CI and workflows

- [x] 3.1 `ci.yml`: add `submodules: recursive` to `actions/checkout`; compute the cache key from `git rev-parse HEAD:extern/openbabel` step output; remove `THOSOO_OPENBABEL_COMMIT` and verify the workflow YAML is valid (`yamllint` or `actionlint` if available) (ci.yml valid; key from `steps.obpin.outputs.commit`; env var removed)
- [x] 3.2 `windows-installer.yml`: add `submodules: recursive` to checkout; verify the `openbabel_ext` build step and installer verification steps are otherwise unchanged (checkout updated; build/verification steps untouched)

## 4. Docs

- [x] 4.1 Update `AGENTS.md` and README: clone line becomes `git clone --recurse-submodules`, add the clean-build-dir migration note, remove "OpenBabel will be downloaded automatically" wording; verify instructions match the new flow

## 5. End-to-end verification

- [x] 5.1 Clean configure + build + ctest on Linux against the submodule source; verify tests pass and `git status` inside `extern/openbabel` is clean after the build (clean configure OK; full build OK with -j8; 12/12 ctest passed; submodule clean after build)
- [x] 5.2 Confirm the submodule pin is the only OpenBabel pin: `grep -rn "d8695a26\|THOSOO_OPENBABEL_COMMIT" CMakeLists.txt .github/` returns nothing, and cache-key derivation uses the gitlink (grep returns no matches; ci.yml cache key from steps.obpin.outputs.commit)
- [x] 5.3 Push branch and confirm both CI workflows green, including the MSVC leg building the unpatched submodule source (pushed af1a82350 to origin/master; Qt6 CI success, Windows Installer installer+tests jobs success — MSVC built OpenBabel from the unpatched submodule source)
