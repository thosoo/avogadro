# Proposal: OpenBabel via git submodule

## Why

`thosoo/openbabel` is a permanent companion fork, developed in tandem with this repository. The current acquisition mechanism (GitHub archive zip downloaded by `ExternalProject_Add`, SHA256-pinned) fights that workflow: the pin commit is duplicated in `CMakeLists.txt` and `ci.yml`, the required MSVC TLS fix is applied as build-time regex patching instead of living in the fork, bumping the pin requires push → archive → recompute SHA256, and the SHA256 pin is fragile against GitHub archive regeneration. A git submodule makes the pin single-sourced, verifiable by git, and supports offline fresh builds and normal in-tree OpenBabel development.

## What Changes

- Add git submodule `extern/openbabel`, pinned to a commit of `thosoo/openbabel` that natively contains the MSVC TLS fix.
- Switch `ExternalProject_Add(openbabel_ext ...)` to build from the submodule source directory. The isolated build and `build/openbabel-install` install prefix are unchanged, preserving CI caching and the Windows installer workflow.
- **BREAKING**: Configure no longer downloads OpenBabel from the network. A source archive (non-git) checkout can no longer build the bundled OpenBabel; git clones must initialize submodules.
- Add a configure-time guard: if the submodule is not initialized, configuration fails with an actionable error message instead of downloading.
- Delete `cmake/PatchOpenBabelMsvcTls.cmake` and its invocation (the fix moves into the fork).
- CI (`ci.yml`): check out with submodules; compute the cache key from the submodule gitlink (`git rev-parse HEAD:extern/openbabel`); remove the hardcoded `THOSOO_OPENBABEL_COMMIT` env var.
- Windows installer workflow: check out with submodules; OpenBabel build/install steps unchanged.
- Prerequisite in the fork: commit the MSVC TLS fix into `thosoo/openbabel` and tag the pinned commit (e.g. `avogadro-2026.10`).
- Update `AGENTS.md` and README build instructions (`git clone --recurse-submodules ...`).

## Capabilities

### New Capabilities
- `bundled-openbabel`: how the bundled OpenBabel dependency is acquired, pinned, and validated — submodule-sourced pin with a single source of truth, fail-loud initialization guard, no post-checkout source mutation, and CI cache keys derived from the pin.

### Modified Capabilities
- (none — no main specs exist yet; this change establishes the first capability)

## Impact

- **Code**: `CMakeLists.txt` (source acquisition block), `cmake/PatchOpenBabelMsvcTls.cmake` (deleted).
- **Workflows**: `.github/workflows/ci.yml`, `.github/workflows/windows-installer.yml` (checkout + cache key).
- **New files**: `.gitmodules`, `extern/openbabel` (submodule gitlink).
- **External**: one commit + tag in `thosoo/openbabel`; contributors' clones need `--recurse-submodules`.
- **Migration note**: existing `build/` directories carry stale URL-download ExternalProject state; the first configure after this change should start from a clean build directory.
