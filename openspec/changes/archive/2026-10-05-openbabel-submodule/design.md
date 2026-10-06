# Design: OpenBabel via git submodule

## Context

Today `CMakeLists.txt` downloads a pinned zip of `thosoo/openbabel` via `ExternalProject_Add` (URL + SHA256 at `CMakeLists.txt:241-244`), regex-patches the source at build time for an MSVC TLS issue (`cmake/PatchOpenBabelMsvcTls.cmake`), and installs into `build/openbabel-install`. The commit pin is duplicated in `.github/workflows/ci.yml:8` for the cache key. The fork is a permanent companion, developed in tandem with this repo. See proposal.md — Why.

## Goals / Non-Goals

**Goals:**
- Single source of truth for the OpenBabel pin (git gitlink)
- Normal tandem-development workflow for OpenBabel changes (edit in-tree, commit, bump pointer)
- Eliminate build-time source patching; fixes live in the fork
- Preserve the existing isolated OpenBabel build, install prefix, CI caching, and Windows installer flow

**Non-Goals:**
- Building OpenBabel in-tree via `add_subdirectory` (isolation is deliberate)
- Supporting builds from GitHub source archives (non-git checkouts)
- Shallow submodule optimization
- Upstreaming OpenBabel changes to open-babel/openbabel
- Changing OpenBabel build flags or the coordgen/inchi configuration

## Decisions

1. **Submodule over continued zip download.** The pin becomes a gitlink: verifiable by git, bumpable in one commit, visible in diffs, and the source is local for fresh build directories. Alternative considered (ExternalProject with `GIT_REPOSITORY`/`GIT_TAG`) avoids `.gitmodules` but still needs network at build time and gives no in-tree dev loop; rejected since tandem dev is the primary motivation.

2. **Keep `ExternalProject_Add`, swap the source.** Point the existing external project at `${CMAKE_SOURCE_DIR}/extern/openbabel` instead of a downloaded URL. The separate-build/install-prefix model stays, so CI cache paths, `PKG_CONFIG_PATH`, and the Windows installer's `openbabel_ext` target all keep working unchanged. Alternative (`add_subdirectory`) risks install-rule/export collisions inside Avogadro's own CMake; rejected.

3. **Fail-loud guard instead of zip fallback.** If `extern/openbabel` lacks content, `message(FATAL_ERROR "OpenBabel submodule not initialized; run: git submodule update --init")`. A silent zip fallback would reintroduce a second pin source — the exact problem this change removes. The one lost capability (building from a source archive) is accepted; all documented build flows are git-based.

4. **No `shallow = true`.** Tandem development will sometimes pin mid-history commits; shallow fetches of non-tip SHAs are a known confusing-failure source. ~70 MB one-time clone is acceptable.

5. **MSVC TLS fix lands in the fork.** Port `PatchOpenBabelMsvcTls.cmake`'s transformation as a normal commit in `thosoo/openbabel`; pin the submodule to that commit (or later). Delete the patch script and its `PATCH_COMMAND`. Verify by the spec's clean-worktree and MSVC-build scenarios.

6. **Cache key from the gitlink.** CI computes the key from `git rev-parse HEAD:extern/openbabel` (step output), replacing the hardcoded `THOSOO_OPENBABEL_COMMIT` env var. The fallback cache-key prefix drops the commit segment as it does today.

7. **Tag pins in the fork.** Cut an annotated tag (e.g. `avogadro-2026.10`) at each commit Avogadro pins. Cheap, makes pins human-readable, and keeps a stable archive-URL escape hatch if ever needed.

## Risks / Trade-offs

- [Source-archive builds break] → Accepted deliberately; README/AGENTS.md updated so the git flow is unambiguous. Revisit a fetch fallback only if a real non-git build need appears.
- [Stale `build/` dirs carry old URL-download state and confuse the first configure] → Migration note in AGENTS.md and proposal: configure from a clean build directory after pulling this change.
- [Contributors forget `--init`] → Fail-loud guard names the exact command; one-time friction.
- [Fork default-branch rewrite would orphan pins] → Git gitlinks still resolve to the pinned commit as long as the fork keeps the objects; tags (Decision 7) pin them durably.
- [In-tree OpenBabel source could trip broad source globs] → Verify during implementation that no CMake `file(GLOB ...)` in Avogadro targets recurses into `extern/`; submodule dirs are excluded from Avogadro's own source lists anyway.

## Migration Plan

1. Fork: commit the MSVC TLS fix to `thosoo/openbabel`, tag it, confirm the fork's CI (if any) is green.
2. Avogadro: add `.gitmodules` + submodule pinned to the tagged commit; switch the external project's source dir; delete the patch script; add the configure guard.
3. Workflows: `actions/checkout` with `submodules: recursive` in both workflows; compute the cache key from the gitlink; drop the hardcoded commit env var.
4. Docs: update clone/configure instructions in `AGENTS.md` and README.
5. Validate: clean-build configure+build+ctest locally (Linux); rely on CI for MSVC verification of the fork-side fix.
6. Rollback: revert the Avogadro commit(s) — the zip mechanism remains in git history; the fork commit is additive and harmless.

## Downstream handoff (fork pin available)

The fork prerequisite is complete and pushed to `thosoo/openbabel`:

- Commit: `f30fefc71a914f98ba4bcb2e2f283d1f9c68ea47` — `Make typer globals MSVC-compatible (avoid C2492)` (edits to `include/openbabel/typer.h` and `src/typer.cpp`).
- Annotated tag: `avogadro-2026.10` → `f30fefc71` (`git rev-parse avogadro-2026.10^{commit}` confirmed).

Verification already performed on the fork side: (1) Avogadro's own `cmake/PatchOpenBabelMsvcTls.cmake` run against a pristine `git archive` copy of the fork produces `typer.h`/`typer.cpp` byte-for-byte identical to this commit, and reports "already present" (no mutation) against it; (2) a clean GCC configure+build of the library compiles the `#else` (THREAD_LOCAL) branch and leaves the working tree clean. Tasks 2.1+ of this change may pin directly to `avogadro-2026.10`.

## Open Questions

- None. The remaining specifics (exact submodule path spelling `extern/openbabel`) are implementation details.
