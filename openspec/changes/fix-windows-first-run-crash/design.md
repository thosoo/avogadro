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
- [Guard may not fix the user's crash if driver 573.22 is itself buggy] → the two
  confirmation experiments (Threaded Optimization off; fix-branch installer) separate
  attribution; if H2 wins, this change still lands (hygiene + installer fixes) and
  the crash gets its own change (e.g., avoid deferral-sensitive calls on that driver
  or document a driver floor).
- [`d->initialized` not set on bail-out] → repeated retry attempts per event are
  cheap (one `currentContext()` check), and a widget that never gets a context never
  did anything before either.

## Migration Plan

Single PR; no data migration. Land the guard + packaging fixes together, run the
Windows installer workflow, install the artifact on the reporting machine, and
verify: window appears, first paint survives, all plugins load (no module-not-found
lines), Orbitals/Surfaces/QTAIM extensions functional. Rollback = revert the PR; the
previous installer remains the fallback.

## Open Questions

- None blocking. (Attribution H1-vs-H2 affects only verification prose, not the
  approach; the confirmation experiments resolve it after this change lands.)
