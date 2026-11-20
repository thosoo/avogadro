# Design: add-xtb-optimization

## Context

The xTB landscape (verified 2026-11-11):

- **Library**: libxtb 6.6.1 (Ubuntu `libxtb6`/`libxtb-dev`, universe) exposes a
  **singlepoint-only C API** (`xtb.h`, API version `1_0_0`): environment/molecule/
  calculator/results objects, `xtb_loadGFN0/1/2xTB`, `xtb_loadGFNFF`,
  `xtb_setAccuracy`, `xtb_setMaxIter`, `xtb_singlepoint`, `xtb_getEnergy`,
  `xtb_getGradient`, … There is **no optimizer entry point** (also true of 6.8.0).
  GFN1/GFN2 parameters are compiled into the library (no data files needed); GFN-FF
  loads `.param_gfnff.xtb` from `XTBHOME`/`XTBPATH`.
- **Windows**: the official xtb 6.8.0 release ships only a **static GNU archive**
  (`libxtb.a`, mingw/gfortran, OpenBLAS) — MSVC cannot link it, so no build-time
  linkage is possible for the MSVC installer. conda-forge ships win-64 **shared**
  builds (`libxtb-6.dll`, gfortran + OpenMP via libgomp + OpenBLAS). The conda-forge
  `libxtb-6.dll` imports: `libblas.dll`, `liblapack.dll`, `libcpx-1.dll`,
  `libdftd4-4.dll`, `libgcc_s_seh-1.dll`, `libgfortran-5.dll`, `libgomp-1.dll`,
  `libmctc-lib-0.dll`, `libmulticharge-0.dll`, `libtblite-0.dll` (+ UCRT system DLLs)
  — i.e. **OpenMP IS enabled on Windows**; its package also contains
  `Library/share/xtb/.param_gfnff.xtb`.
- **In-tree**: `xtbopttool` plugin (pkg-config-gated, Linux-only in practice) with a
  fixed-step steepest-descent loop, dead Engine/Level combos, `Threads=1` default, its
  own `QMutex` but no molecule-lock discipline; the hardened `autoopttool` (force
  fields) with snapshot publishing, quiesce, drag fix-markers, undo serialization.
  The existing `auto-optimize-interaction` spec already defines the optimizer
  interaction contract this change extends.

See proposal.md for motivation; the spec deltas define the behavioral contract.

## Goals / Non-Goals

**Goals:**
- xTB (GFN1/GFN2/GFN-FF) optimization usable on Windows and Linux from one tool.
- Interactive atom pulling that matches the existing optimizer contract exactly.
- Interactive speed: threads, accuracy adaptation, quasi-Newton convergence.
- No build-time xTB dependency on any platform; graceful runtime degradation.

**Non-Goals:**
- GPU/CUDA xTB builds (upstream experimental; conda-forge is CPU-only).
- Subprocess use of `xtb.exe` (too slow for interactive singlepoints).
- Direct tblite API adoption (xtb already delegates to tblite internally; the xTB C
  API is the stable consumer surface, and it is the method packaged in Ubuntu).
- Charge/spin/multiplicity settings UI (session uses neutral closed-shell as today;
  follow-up change if needed).
- FF-side changes of any kind (the hardened force-field path is preserved verbatim).
- Exact reproduction of xtb's own optimizer level thresholds (approximate named
  constants, tunable; see D5).

## Decisions

**D1 — Runtime discovery with a `QLibrary` shim; vendored header; no build-time xTB
dependency anywhere.** `libavogadro/src/xtb/xtb.h` is the API header (vendored from
libxtb 6.6.1, upstream's versioned C API — it does not drift without an API-version
suffix change), and `libavogadro/src/xtb/xtbapi.{h,cpp}` resolves every entry point
through `QLibrary` into a typed function-pointer table (`XtbApi::isAvailable()`,
`XtbApi::lastError()`). Library name candidates: `libxtb-6.dll` (Windows;
`QLibrary` searches the application directory, the plugin directory, and PATH — the
installer places it next to the app), then `libxtb.so.6`, then `libxtb.so` (Linux).
Alternatives rejected: (a) keep pkg-config build-time linkage — impossible on Windows
(the official Windows library is a GNU static archive; MSVC cannot link it; the
historical Intel oneAPI/MKL attempt failed for exactly this class of reason);
(b) MSVC `lib /def` import library generated from a downloaded DLL — couples the build
to binary layout details and still needs a DLL at runtime, so the shim is required
anyway; linking it statically saves nothing. The old plugin and the CI `xtb=ON/OFF`
matrix keep working because tests and the engine skip physics when
`isAvailable()` is false.

**D2 — Windows runtime bundle: pinned conda-forge win-64 artifacts, extracted and
copied by the installer workflow.** The workflow downloads exact `.conda` artifacts
(pin: exact filename + sha256 in the workflow; digest mismatch fails the build),
extracts the `Library/bin/*.dll`, `Library/share/xtb/.param_gfnff.xtb` (+ sibling
`param_*.txt` files) and each package's `info/licenses`, and stages them into the
package next to `avogadro.exe` (`share/xtb` for data, `licenses/xtb/` for texts).
Closure: `libxtb-6.dll` plus its full transitive DLL closure (first level:
`libtblite-0`, `libdftd4-4`, `libmctc-lib-0`, `libmulticharge-0`, `libcpx-1`,
`libblas`, `liblapack`, `libgcc_s_seh-1`, `libgfortran-5`, `libgomp-1`, `libwinpthread-1`,
`libquadmath-0`, `libopenblas`, plus whatever the transitive packages import — the
bundling task walks imports to a fixpoint with `dumpbin /dependents`). Because the
application consumes the DLL via `LoadLibrary` (through QLibrary), nothing in an
import table references it, so the existing import-driven closure check is neither
perturbed nor sufficient — the smoke step therefore asserts the load explicitly
(`LoadLibraryW` + `GetProcAddress("xtb_newEnvironment")` non-null). Licenses: LGPL-3.0
(xtb, tblite, …), GPL-with-runtime-exception (libgfortran/winpthread), BSD
(OpenBLAS) — shipping the license texts satisfies all of them for unmodified binary
redistribution. Alternatives recorded: (A) vendored xtb source submodule + mingw
gfortran CI leg producing our own DLL — the recorded **fallback** if conda-forge ever
breaks the pin pattern (user decision 2026-11-11: B first, A if it breaks); (C) ship
the self-contained `xtb.exe` and drive it as a subprocess — rejected: interactive
pulling needs hundreds of singlepoints per minute in-process, not per-invocation
process+file-I/O; (D) adopt tblite's C API directly — rejected for now: tblite is not
in Ubuntu, which would break the Linux distribution-package path (user decision:
Ubuntu packages on Linux).

**D3 — One tool: extend `AutoOptTool`, retire `xtbopttool`.** The hardened
force-field tool keeps its identity, undo behavior, and every interaction path; the
settings widget gains an "Engine" combo (Force field / xTB-GFN1 / xTB-GFN2 /
xTB-GFN-FF) that routes `enable()`/teardown to the FF thread or to `XtbEngine`, shows
or hides the backend-specific controls (FF method combo vs xTB level combo + thread
spin box), and persists via the tool's existing `readSettings`/`writeSettings`. When
the engine is xTB and `XtbApi::isAvailable()` is false, Start is disabled with a
tooltip/statusbar diagnostic (visible, logged) and FF remains startable. The HUD
gains the active engine/method name. `xtbopttool` is deleted (plugin sources, CMake
registration, `ENABLE_XTB_OPTTOOL` option, pkg-config linkage, `xtbopttooltest` —
its water-geometry physics checks are re-homed in `xtbenginetest`). Alternative
rejected: keep two tools (status quo duplication; user decision: one tool).

**D4 — `XtbEngine` mirrors `AutoOptThread`'s hardened discipline.**
`libavogadro/src/xtb/xtbengine.{h,cpp}`: a `QThread`-based worker owning the four xTB
objects (environment/molecule/calculator/results) created in `setup()` from an atomic-
number list + coordinates (no `Molecule*` retention across threads — the worker
reads positions published by the GUI under the molecule read lock at tick boundaries,
or works on its own coordinate vector). Publishing: the GUI applies worker-published
coordinate snapshots to the molecule (same cadence and lock pattern as the FF thread:
worker holds the molecule read lock only for the bounded snapshot copy; GUI
mutations hold the write lock; tool teardown does `stop(); quit(); wait()` and waits
for the in-flight singlepoint before deleting xTB objects). Setup failure (element
unsupported, missing parameter data) surfaces exactly once via a failed-setup signal
carrying `xtb_getError` text, then the session idles — no retries (spec).

**D5 — Client-side L-BFGS over singlepoints.** The C API has no optimizer, so
`libavogadro/src/xtb/lbfgs.{h,cpp}` implements standard two-loop-recursion L-BFGS
(history m = 10, named constant) over the flat coordinate array with an evaluate
callback (energy + gradient) and a stop callback (converged / cancelled). Convergence
levels map to gradient-norm thresholds in Eh/bohr — named constants approximating
xtb's own optimizer levels (`crude` 5e-3, `sloppy` 1e-3, `loose` 8e-4, `lax` 5e-4,
`normal` 3e-4, `tight` 1e-4, `vtight` 3e-5, `extreme` 1e-6) and a maximum-evaluation
cap (default 500, named constant). These are tunable constants; tests assert the
normal-level water geometry, not the exact thresholds. Alternatives: keep steepest
descent (status quo — typically 3–10× more singlepoints for the same tolerance);
port xtb's ANCopt (Fortran, out of reach from the C API).

**D6 — Pull mechanics for the xTB path.** The GUI keeps pointer authority for the
dragged atom (existing `translate()` path writes the atom position). At tick
boundaries the worker takes the dragged atom's current position, then: pins that
atom's coordinates (its gradient rows are zeroed; the coordinate is never updated by
the optimizer while the drag lasts), runs the L-BFGS step for the remaining atoms,
and publishes the snapshot — the dragged atom's published coordinate equals the
pointer position, so write-back cannot regress it (the spec's coordinate write-back
requirement). L-BFGS history is reset when a drag starts and when it ends (masked
variables invalidate the quasi-Newton model; a reset is cheap and bounded). On
release the optimizer resumes covering the atom from the dragged position.

**D7 — Performance adaptation and threading.** While a drag is active the worker
calls `xtb_setAccuracy(env, calc, kXtbInteractiveAccuracy=0.2)` and
`xtb_setMaxIter(env, calc, kXtbInteractiveMaxIter=30)`; on drag end it restores
`kXtbDefaultAccuracy=1.0` / `kXtbDefaultMaxIter=100` (named constants, documented as
tunable). Steps per update: 1 while dragging; the user's Steps setting (default 4)
when idle. Threads: before the first xTB call of a session, the tool sets
`OMP_NUM_THREADS` (user value, or auto = `QThread::idealThreadCount() - 1` clamped to
`[1, 8]`) and `OMP_WAIT_POLICY=PASSIVE` via `qputenv` if not already set by the user
environment — this configures the OpenMP runtime inside the library on both platforms
(libgomp on Linux, libgomp-1.dll bundled on Windows); the MSVC plugin itself does not
compile with OpenMP (D9), it only sets environment variables. Worker evaluations stay
off the GUI thread, so the camera never blocks on an SCF (D4).

**D8 — GFN-FF parameter data locator.** Before the first `xtb_loadGFNFF` call, the
engine ensures `XTBHOME`/`XTBPATH` point at a directory containing
`.param_gfnff.xtb`: (1) if the user's environment already sets `XTBHOME` or
`XTBPATH`, respect it untouched; (2) else probe the bundled location
(`QCoreApplication::applicationDirPath() + "/share/xtb"`, populated by the Windows
installer); (3) else probe `/usr/share/xtb` (the Ubuntu `xtb` package's data). When
located, `qputenv` both variables; when not, GFN-FF setup fails deterministically
with the library's diagnostic (spec'd failure path) while GFN1/GFN2 (compiled-in
parameters) are unaffected.

**D9 — MSVC compatibility (repo policy).** New code compiles under
`-std:c++17 -permissive-`: `QLibrary` + typedef'd function pointers for every xTB
entry point; plain Qt widgets in the settings UI; no aggregate initialization of
classes with user-provided special members; no OpenMP pragmas in MSVC-compiled code;
no new threading primitives beyond `QThread`. The Windows installer workflow is the
compile gate for all of it.

**D10 — Test strategy: physics when the library is present, skips when absent.**
Three new always-built test binaries: `xtbruntimetest` (shim: absence is reported
harmlessly; presence yields a negative water GFN2 energy through the function
pointers), `lbfgstest` (pure-logic: Rosenbrock/quadratic convergence, max-iteration
stop, stop-callback, drag-masking-free by construction), `xtbenginetest` (engine:
setup success/failure including unsupported-element, singlepoint sanity, water
converges to reference geometry at normal level, drag masking pins the dragged atom
to the pointer position, accuracy adaptation toggles, quiesce on immediate stop,
GFN-FF param-data path — every physics slot `QSKIP`s when `XtbApi::isAvailable()`
is false). `xtbphysicstest` is rewritten onto the shim (removing the last
link-time xTB consumer from the test tree). Tool-level lifetime safety extends the
existing `autooptreprotest` pattern: activating the tool with xTB selected while the
library is absent must not crash, and a start/drag/stop cycle must leave a valid
molecule.

**D11 — CI reshaping.** Linux `ci.yml`: the `xtb` matrix dimension stays and now
controls **runtime presence** — the ON leg installs `apt-get install -y xtb` (binary
package pulls `libxtb6` and the GFN-FF parameter data; `libxtb-dev` and pkg-config
usage are dropped), the OFF leg installs nothing (physics tests skip). The
`-DENABLE_XTB_OPTTOOL=` CMake flag and the option itself are removed (the plugin
always builds). `windows-installer.yml` gains the pinned-download/extract/stage step,
the license staging, and the smoke assertion (D2), and its ctest leg runs with the
bundled DLL on PATH so the shim resolves it like a user machine.

## Risks / Trade-offs

- **[Interactive cost on large molecules]** GFN2 singlepoint cost grows steeply with
  atom count; a session on a multi-thousand-atom system cannot stay fluid.
  → D7 adaptation makes drags responsive in the small-to-medium regime; the
  non-goal list records GPU as future work; the convergence cap bounds runaway
  sessions.
- **[OpenMP threads contend with the GUI/render threads]** xTB workers spinning
  between SCF iterations starve the GL thread.
  → `OMP_WAIT_POLICY=PASSIVE` (D7) and the auto thread budget clamped below full
  core count; the FF path is unaffected.
- **[conda-forge artifact rotation breaks the pin]** upstream re-uploads or removes
  a build.
  → sha256 digest check fails the build loudly (spec'd), and the recorded fallback is
  plan A: vendored-source DLL built by a mingw gfortran CI leg (same shim, same
  bundle layout).
- **[Transitive closure drifts]** a future pinned bump adds a new imported DLL.
  → the bundling task computes the closure to a fixpoint at packaging time and the
  smoke load-step catches any miss on the runner, before release.
- **[Dragged-atom masking invalidates the L-BFGS model]** masking changes the
  objective between iterations.
  → history reset at drag start/end (D6); masking never applies mid-iteration.
- **[Approximate convergence thresholds]** the level→threshold constants are close
  to, but not verified against, upstream's table.
  → named constants + water-reference test at the normal level; exact tuning is a
  one-constant change per level and cannot alter the contract.
- **[Element-coverage failures mid-session]** atom edits during an xTB session can
  introduce unsupported elements.
  → setup-time detection (once, deterministic, spec'd); mid-session edits follow the
  existing FF-spec rules for structural churn (undo/deletion quiesce paths shared
  with D4's teardown discipline).
- **[QSettings migration]** users of the deleted `xtbopttool` lose its settings
  (different keys from the unified tool's).
  → accepted; the old tool shipped no stable settings surface on Windows (it never
  shipped there at all) and its Linux settings were a duplicate of the FF tool's UI
  pattern.

## Migration Plan

Order is staged so the tree stays green throughout (task phases 1–8): shim and pure
logic land first with skips; the engine lands headless; the unified tool lands with
the old `xtbopttool` still present; the old plugin is deleted only after the unified
tool routes xTB; CI/installer bundling flips last. Rollback per phase is a revert;
the installer bundle step is independent of the tool code (the smoke check works
even before the engine ships).

## Open Questions

None material. The convergence-threshold constants (D5) and accuracy/iteration
constants (D7) are deliberately tunable named constants, and per-platform library
search-order details are fixed by D1/D2.
