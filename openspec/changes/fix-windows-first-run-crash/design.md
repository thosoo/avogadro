# Design

## Context

Two independent defects make the CI-built Windows installer fail on first run
(proposal.md). The crash diagnosis is complete to the evidence level: the faulting
thread in the cdb capture is a driver-spawned worker (no application frames), and
the application log proves GL calls were issued with no current context (valid
`glGetString` result while `QOpenGLContext::currentContext()` was null). The
remaining uncertainty is attribution between "our contextless-call UB poisoned
driver state" and "driver 573.22 deferral-queue bug"; the guard fix is correct under
either hypothesis, so it is the primary fix, with two cheap user-side experiments
(Threaded Optimization off; a fix-branch installer build) as confirmation.

Verification update (2026-10-26): the first post-change test on the reporting
machine was invalid as fix verification. The installed build's banner read
`Git: a54bc78` — the commit immediately before the fix commit `e1ddaaee1`, which
was (and is) local-only, so no CI-built artifact could contain the fixes. The
capture reproduces every pre-fix signature: the three extension load failures
(no closure gate existed at `a54bc78`), the old fallback wording ("using the
modern tier"), the same first-chance AV inside `nvoglv64!DrvPresentBuffers`, and
— sharpest yet — the second widget logging a live `glGetString` renderer string
immediately before `ModernRendererBackend` reported "no current GL context" in
the same initialization pass. The fix is therefore neither verified nor
falsified; H1-vs-H2 attribution stays open until a fix-built artifact is tested.

Verification update (2026-10-26, second attempt): the fix is now committed
(`81a169d0a`) and pushed to origin/master, so a CI-built artifact can contain it.
The Windows Installer workflow auto-fired on the push and run 40308885198 is green
end-to-end on headSha `81a169d0a`: the dependency-closure gate now passes (the
allowlisted OS DLLs and the import-driven Boost copy resolve every non-system
import), the smoke run passes, and `avogadro-win64-1.2.0.exe` was uploaded. The
installer-build defects (crash guard + dependency closure + smoke run) are
therefore verified at the CI level. What still needs the artifact on the
reporting machine (Quadro P3200 / 573.22): the crash itself (task 1.5) and the
three-extension load check (task 4.3), and with them the H1-vs-H2 attribution —
those remain open pending a user-installed fix build.

Verification outcome (2026-10-26, fix artifact on the reporting machine): the
CI artifact `avogadro-win64-1.2.0.exe` (banner `Git: 81a169d`) was installed and
run under cdb on the Quadro P3200 / 573.22 machine. The guard works exactly as
designed: the second widget bails out cleanly on the contextless path (no live
`glGetString` before "no current GL context"), logs the corrected fixed-function
fallback wording, and the modern tier recovers lazily ("modern backend
recovered"). Every extension loads (`Qt6Concurrent.dll`, `avogadro_OpenQube.dll`,
the orbital/surface/qtaim extensions all ModLoad; zero load failures), and only
the three needed Boost variants are present — the installer-runtime scope is
verified on a clean end-user machine. The crash, however, still occurs:
first-chance AV c0000005 in `nvoglv64!DrvPresentBuffers+0x1f6703` on a non-main
thread, now at `createObjects()` — after first paint and full startup, later
than the original first-paint fault. H1 (contextless-call UB as the sole cause)
is thereby disfavoured: the contextless GL path no longer executes, yet the
driver still faults. Note the delta scenario "No undefined driver dispatch is
observable" now holds in its letter (first paint survives with no AV during
it) while the launch still ends in the same driver fault — the residual is not
covered by this change's requirement. H2 (driver 573.22 present-path bug) is
the leading attribution; per the pre-registered branch the guard still lands
(hygiene verified) and the residual crash moves to a follow-up change, pending
the cheap user-side experiments (Threaded Optimization off; driver rollback)
and a second run with reused settings.
Experiment update 2026-10-26: Threaded Optimization OFF — no effect, the crash
persists. The specific threaded-optimization/deferral-queue mechanism is
disfavoured; the driver-side bug must lie elsewhere in the present path or be
a 573.22-branch regression. Experiment 2 2026-10-26: forced
`AVOGADRO_RENDERER=compat` — no effect, crash persists. Tier-independence
disfavours modern-tier-specific GL usage (instancing/fences/PBO) as the
trigger and narrows the application-side suspect set to code common to all
tiers: the GLPainter path (the compat painter AND the modern tier's
text/overlay/bridge painter) and widget/context plumbing. Driver rollback is
rejected by the user as a mitigation. Code investigation (2026-10-26) found a
candidate root cause in that common code: GL object names (display lists,
textures, mesh VBOs) are used across two unshared QOpenGLWidget contexts —
`main.cpp` never sets `Qt::AA_ShareOpenGLContexts`, while
`MainWindow::newGLWidget` shares the first widget's GLPainter with the second
(mainwindow.cpp:3905, glwidget.cpp:528-535); also `~GLWidgetPrivate` deletes
display lists with no context current (glwidget.cpp:235-241). Oracle verdict
2026-10-26: hypothesis CONFIRMED; fix = `Qt::AA_ShareOpenGLContexts` before
QApplication construction + context-safe `GLWidget` teardown, implemented in
`openspec/changes/fix-shared-gl-contexts` (local suite green; discriminating
test = fix artifact on the reporting machine, run outside the debugger).

Constraints:
- The legacy tier is not compiled into the installer build (`AVO_RENDERER_LEGACY`
  off), so on a headless CI runner (GDI Generic GL 1.1) the packaged app legitimately
  aborts via `qFatal` at tier resolution. The smoke-run design must accommodate that
  documented outcome instead of treating it as a healthy start.
- `windeployqt` scans only the main executable's static imports; Qt modules used
  exclusively by plugins/libraries (today `Qt6Concurrent.dll`, imported by
  `avogadro_OpenQube.dll` and `qtaimextension.dll`) are invisible to it. Any fix must
  be import-table-driven, not a hardcoded one-DLL copy, so the next module pickup
  cannot regress packaging.
- The dumpbin-based zlib check already in `create_installer.py` establishes the
  pattern (verify imports, fail loudly); the new closure check generalizes it.
- Windows leg parity: everything here is Windows/MSVC-specific; the Linux CI leg is
  unaffected. MSVC compatibility rules from AGENTS.md apply to the C++ edit.

## Goals / Non-Goals

**Goals:**
- Installer build fails the job when any packaged binary has an unresolved non-system
  dependency; `Qt6Concurrent.dll` (and any future plugin-only Qt module) is deployed.
- No debug-runtime DLLs in the package; release DLLs actually imported are kept.
- Smoke run produces real evidence (startup banner + renderer line) or a documented
  headless outcome; early exit fails the job.
- No GL call without a current context in `GLWidget` event paths; deferred
  initialization actually retries; accurate fallback logging.
- The user's machine starts the installed app (crash gone, plugins load).

**Non-Goals:**
- Diagnosing or working around NVIDIA driver 573.22 internals; if the guard alone
  does not fix the user's crash, driver rollback is a user-side mitigation, not a
  code change here.
- Replacing display lists / the fixed-function painter (frozen low tiers; the crash
  is not an application-stack fault).
- Shipping the VC++ redistributable from the installer (the closure allowlist
  documents that dependency; bundling vcredist is a future change if wanted).
- Static-linking Boost (broader build change; the DLL filter covers packaging).

## Decisions

1. **Import-table-driven dependency closure as a hard build gate**
   (`create_installer.py`). Walk every PE under `dist\bin` and `dist\lib\avogadro\**`
   (`dumpbin /dependents`), normalize names case-insensitively, resolve each import
   against `dist\bin` plus an allowlist (Windows system DLL set, `api-ms-*`/UCRT,
   `MSVCP140*`/`VCRUNTIME140*`/`VCOMP140` redistributable set). Missing → print a
   binary→DLL report and raise. Replaces the zlib-only check (which stays, subsumed).
   Alternative considered: windeployqt-only fix (run windeployqt on
   `avogadro_OpenQube.dll`) — rejected as primary because it patches today's symptom
   without a gate; kept as the mechanism that supplies the missing module.

2. **Qt-module top-up before the gate**: after windeployqt, for every Qt6 DLL import
   found unresolved by the scan, copy it from `Qt6_DIR\bin` (workflow already pins
   `Qt6_DIR`; pass it into the packaging step environment). This supplies
   `Qt6Concurrent.dll` and any future module without hardcoding names.

3. **Boost DLL filtering at packaging**: drop `*-gd-*` (debug) variants and anything
   importing debug runtimes; keep release DLLs that packaged binaries import
   (today `boost_iostreams/bzip2/zlib` via `maeparser.dll`). The workflow's bulk copy
   of the choco Boost lib dir into `openbabel-install\bin` stays, so OpenBabel's own
   build/probe steps keep working; the filter applies when copying into `dist`.

4. **Smoke run with polling and outcome classification** (workflow step): launch with
   stderr capture; poll the log for the startup banner (`Avogadro version:`) for up
   to ~20 s; then require either the renderer line (`OpenGL renderer:`) or a
   documented headless outcome — the process alive at the deadline counts as healthy;
   an exit before the banner with empty output fails the job; a `qFatal` tier message
   is reported as the known headless limitation. Do not kill the app before reading
   its log (today's step reads 0.5 s after launch).

5. **GL init guard in `GLWidget`** (`libavogadro/src/glwidget.cpp`):
   - `resizeEvent`: `if (makeCurrent()) { init-if-needed; resizeGL(...); doneCurrent? }`
     else skip GL work entirely (still `emit resized()`).
   - `paintEvent`: if `makeCurrent()` fails, skip the paint (Qt will re-send expose
     events; nothing drawable happened).
   - `initializeGL`: first statement — if `QOpenGLContext::currentContext()` is null,
     return without touching GL and **without** setting `d->initialized`; the next
     event path retries. Defense in depth behind the event-path checks.
   - The modern tier already recovers lazily in `renderModern()` when the backend is
     missing (existing design D3 recovery); no render-path change.
   - Alternative considered: create widgets only when shown — rejected; too invasive
     for the same effect.

6. **Accurate fallback log**: the `initializeGL` fallback message says what actually
   happened ("fixed-function presentation until the modern backend initializes") and
   `renderModern()`'s lazy recovery logs once when it succeeds. Keep `rendererTier`
   (the *dispatch* tier, correct as-is) distinct from the presentation path in
   messages; do not change dispatch semantics.

## Risks / Trade-offs

- [Allowlist too narrow blocks legit deps] → the allowlist covers the documented
  redist set (`MSVCP140*`, `VCRUNTIME140*`, `VCOMP140`, UCRT/`api-ms-*`, known
  system DLLs); anything else present on the build machine but absent from the
  package is exactly the bug class we want to fail on. Failures are loud and
  specific, so widening is a one-line change.
- [Smoke run flakiness on slow runners] → poll window (20 s) with a generous margin
  over the observed ~0.5 s startup; treat "alive at deadline without renderer line"
  as healthy-with-note rather than failure.
- [Guard changes paint-event timing] → a failed `makeCurrent` already means no
  drawable surface; skipping the paint cannot lose content (Qt re-issues expose on
  the next opportunity). The `ENABLE_THREADED_GL` path is compiled out by default and
  is untouched.
- [Guard may not fix the user's crash if driver 573.22 is itself buggy] → OBSERVED
  2026-10-26: the guard alone did not fix the reporting machine's crash (see
  Context — the fault moved from first paint to `createObjects()`, still inside
  `nvoglv64!DrvPresentBuffers`). The installer fixes and GL hygiene are verified
  and still land; the residual crash gets its own change. Next discrimination:
  Threaded Optimization OFF retest, then driver rollback.
- [`d->initialized` not set on bail-out] → repeated retry attempts per event are
  cheap (one `currentContext()` check), and a widget that never gets a context never
  did anything before either.
- [Fix commit never reaches CI] → RESOLVED 2026-10-26: the fix was committed
  (`81a169d0a`, only `scripts/installer/create_installer.py`, not a tasks-only
  commit) and pushed to origin/master. CI auto-fired; run 40308885198 checked out
  `81a169d0a` and is green end-to-end (Create installer, smoke run, Upload
  installer → avogadro-win64-1.2.0.exe). Artifact provenance is now bound to the
  fix commit (banner `Git: 81a169d`), so the precondition for interpreting a user-
  machine crash as fix failure/success is met. The remaining user-machine checks
  (tasks 1.5, 4.3) are still outstanding — they need the artifact installed on the
  reporting machine.

## Migration Plan

Single PR; no data migration. Land the guard + packaging fixes together, run the
Windows installer workflow, install the artifact on the reporting machine, and
verify: window appears, first paint survives, all plugins load (no module-not-found
lines), Orbitals/Surfaces/QTAIM extensions functional. Rollback = revert the PR; the
previous installer remains the fallback.
Migration outcome 2026-10-26: done as written — plugin load and GL-guard
behaviour verified on the reporting machine; the window-appears criterion is
blocked by the residual driver fault (see Context), which moves to a follow-up
change.

## Open Questions

- Attribution H1-vs-H2: the confirmation experiment has now validly run (fix
  artifact `Git: 81a169d` on the reporting machine, 2026-10-26). The crash
  reproduces with no contextless GL call having been made, so H1-as-sole-cause
  is disfavoured and H2 (driver 573.22 present-path bug) leads. Threaded
  Optimization OFF retested 2026-10-26: no effect. Remaining discrimination:
  driver rollback retest; forced `AVOGADRO_RENDERER=compat` run; second run
  with reused settings. (Attribution affects the follow-up change's
  approach, not this change — its scope is verified and lands as-is.)
