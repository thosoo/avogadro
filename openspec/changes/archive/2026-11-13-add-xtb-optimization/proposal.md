# Proposal: add-xtb-optimization

## Why

GFN-xTB (semi-empirical quantum) geometry optimization is effectively unusable in the
application today: the existing `xtbopttool` plugin never builds on Windows (the
Windows installer has no xTB runtime and MSVC cannot link the Fortran library), and on
Linux the tool that does build is weak — a hand-rolled fixed-step steepest-descent
loop with dead Engine/Level UI, a Threads setting defaulting to 1, no convergence
control, no molecule-lock discipline (Ctrl+Z mid-run can crash it), and a drag that
fights the optimizer (stale optimizer coordinates overwrite the dragged atom). At the
same time the application ships a second, overlapping optimization tool (the hardened
AutoOpt force-field tool), so quantum and force-field optimization are separate tools
with duplicated, divergent interaction quality. The user-visible goal: one
optimization tool, GFN2-xTB/GFN1-xTB/GFN-FF backends available on both platforms,
with interactive atom pulling and acceptable speed (multithreading, interactive
accuracy adaptation, a quasi-Newton optimizer).

## What Changes

- **Runtime discovery of the xTB library.** The plugin no longer links `libxtb` at
  build time and is no longer gated on a build-time find. A small `QLibrary`-based
  loader (`XtbApi` shim) resolves the xTB C API at runtime; a vendored `xtb.h` header
  provides the API declarations. When the library is absent the tool degrades
  gracefully (xTB backends hidden/disabled with a diagnostic; force-field backends
  unaffected). **BREAKING** (build-system): the `ENABLE_XTB_OPTTOOL` CMake option and
  pkg-config xTB linkage are removed; Linux builds need only the runtime library
  (`apt install xtb`) — `libxtb-dev` is no longer required.
- **Windows installer bundles a pinned xTB runtime.** The workflow downloads exact
  pinned conda-forge win-64 artifacts (`libxtb-6.dll` + its DLL closure + the GFN-FF
  parameter file + license texts), records their sha256 digests, and the installer
  smoke run verifies the library loads and exports the C API.
- **One optimization tool.** The hardened AutoOpt tool becomes the single optimization
  tool with a backend selector: OpenBabel force fields (as today) and xTB methods
  (GFN1-xTB, GFN2-xTB, GFN-FF). The old `xtbopttool` plugin is retired and deleted.
- **A real optimizer for xTB.** A client-side L-BFGS implementation drives library
  singlepoints; convergence levels (crude…extreme) map to gradient-norm thresholds;
  sessions stop deterministically when converged or when setup fails (e.g. unsupported
  elements).
- **Interactive pulling per the existing optimizer contract.** While the user drags an
  atom during an xTB session, the drag path is the authority for that atom: the worker
  masks it from optimizer write-back, and on release the optimizer resumes covering it
  from the dragged position.
- **Speed.** OMP thread configuration (auto-detect default, user-adjustable, passive
  wait policy) applied via environment before the first xTB call; SCC accuracy and
  max-iteration caps reduced while a drag is active and restored after; steps-per-
  update adaptive to dragging.
- **GFN-FF parameter data.** The app locates the `.param_gfnff.xtb` data directory
  (bundled `share/xtb` on Windows; probed `/usr/share/xtb` or `$XTBHOME` on Linux) and
  sets `XTBHOME`/`XTBPATH` when unset. GFN1/GFN2 need no data files.

## Capabilities

### New Capabilities

(none — the behavior lands in existing capabilities)

### Modified Capabilities

- `auto-optimize-interaction`: generalized from force-field-only to selectable
  optimizer backends; MODIFIED: thread-confinement and failed-setup requirements
  reworded to cover the xTB backend; ADDED: selectable backends (one tool), runtime
  xTB availability/discovery, deterministic xTB convergence, interactive performance
  adaptation.
- `windows-installer-runtime`: ADDED: bundled pinned xTB runtime requirement (closure
  present, param data present, licenses shipped, smoke verifies the library loads).

## Impact

- **Code:**
  - New (library-side, no GUI): `libavogadro/src/xtb/xtb.h` (vendored API header),
    `libavogadro/src/xtb/xtbapi.{h,cpp}` (runtime loader), `libavogadro/src/xtb/lbfgs.{h,cpp}`
    (quasi-Newton optimizer), `libavogadro/src/xtb/xtbengine.{h,cpp}` (session engine:
    setup/singlepoint/L-BFGS/drag masking/accuracy adaptation), param-data locator.
  - Modified: `libavogadro/src/tools/autoopttool.{h,cpp}` (backend selector, xTB
    routing, availability gating, HUD), `libavogadro/tests/CMakeLists.txt`,
    `libavogadro/src/tools/CMakeLists.txt`, `.github/workflows/ci.yml`,
    `.github/workflows/windows-installer.yml`.
  - Deleted: `libavogadro/src/tools/xtbopttool.{h,cpp,qrc}` (plugin), its CMake
    registration, `libavogadro/tests/xtbopttooltest.cpp` (its physics checks move to
    new shim-based tests); `libavogadro/tests/xtbphysicstest.cpp` rewritten on the
    shim.
  - New tests: `xtbruntimetest`, `lbfgstest`, `xtbenginetest` (all skip xTB physics
    when the runtime library is absent).
- **Dependencies:** Linux runtime: `xtb` (Ubuntu universe, provides `libxtb.so.6` and
  `/usr/share/xtb` parameter data). Windows runtime: bundled, pinned conda-forge
  win-64 artifacts (~13–15 DLLs + `.param_gfnff.xtb`, LGPL-3.0 / GPL-with-runtime-
  exception / BSD licenses shipped as texts). Build no longer requires any xTB
  development package on any platform.
- **Verification constraint:** CI runners have no interactive session; all engine
  behavior is unit-tested headlessly with real physics when the runtime library is
  installed (CI `xtb=ON` leg) and skip-gated when absent (`xtb=OFF` leg). Windows
  verification rides the installer workflow's ctest leg with the bundled DLLs.
- **MSVC:** all new code compiles under `-std:c++17 -permissive-` (plain Qt widgets,
  `QLibrary`, function-pointer typedefs; no new threading primitives; the Windows
  installer workflow is the gate).
