## Why

The CI-built Windows installer produces an Avogadro that **cannot complete its first
run**: the process crashes during the first paint (`0xC0000005` inside
`nvoglv64.dll`) before the main window is ever presented, and three quantum-chemistry
plugins (Orbitals, Surfaces, QTAIM) fail to load because the packaged
`bin\` directory is missing `Qt6Concurrent.dll`. Both defects were invisible to CI:
the dependency check only covers `dist/bin` for zlib, and the installer smoke run
kills the app after 0.5 s and reads an empty log, verifying nothing.

Diagnosis evidence (2026-10-23, user machine, Quadro P3200 / driver 573.22):
extracted-artifact import-table scan proved `Qt6Concurrent.dll` is absent from the
entire package while `qtaimextension.dll` imports it directly and
`avogadro_OpenQube.dll` imports it transitively (taking down `orbitalextension.dll`
and `surfaceextension.dll`). The crash was captured under cdb: the faulting thread is
a **driver-spawned worker thread** (root `BaseThreadInitThunk`, no `avogadro.dll`
frames) dereferencing a garbage pointer while processing deferred GL work — the
async-poisoning signature of GL calls issued without a current context. The app log
proves that undefined dispatch: the second `GLWidget`'s `initializeGL` obtained a
valid `glGetString` renderer string while `QOpenGLContext::currentContext()` was null
in the same call, because `resizeEvent`/`paintEvent` ignore `makeCurrent()`'s return
value and run initialization (~15 fixed-function GL calls) with no context current.

## What Changes

- **Windows installer packages a loadable dependency set.** `create_installer.py`
  gains a full dependency-closure check: every packaged PE (`bin\*.dll|*.exe` and
  `lib\avogadro\**\*.dll`) is inspected (`dumpbin /dependents`), each non-system,
  non-redistributable import must resolve inside the package, and missing Qt modules
  (today `Qt6Concurrent.dll`) are copied from the Qt installation before the check
  runs. The build fails loudly on any unresolved dependency.
- **Debug-runtime Boost DLLs are no longer packaged** (~17 MB of `*-gd-*` DLLs whose
  own imports (`ucrtbased.dll`, `MSVCP140D.dll`) do not exist on end-user machines);
  only release Boost DLLs actually needed by packaged binaries are shipped.
- **The CI smoke run becomes a real verification**: launch the packaged exe, poll the
  captured log for the startup banner and the renderer line, fail if the process exits
  before producing them; treat a headless-runner GL limitation as a documented,
  reported outcome rather than a silent pass.
- **GLWidget initialization never issues GL calls without a current context.**
  `resizeEvent`/`paintEvent` check `makeCurrent()`'s return; `initializeGL` bails out
  cleanly when no context is current and initialization is retried later (the modern
  tier's lazy first-frame recovery already anticipates a delayed `initializeGL`);
  `d->initialized` is not set on the bail-out path.
- **Startup diagnostics report the tier actually in use** — the fallback message that
  says "using the modern tier" while running the fixed-function path is corrected.

## Capabilities

### New Capabilities

- `windows-installer-runtime`: contract for the Windows installer package contents —
  packaged binaries load on a clean machine (dependency closure, including
  plugin-only Qt modules), no dead debug-runtime DLLs, and CI verification that
  actually exercises the packaged runtime (dependency closure gate + smoke run with
  log polling).

### Modified Capabilities

- `gpu-rendering-pipeline`: new requirement — GL initialization and any GL call path
  in `GLWidget` SHALL run only with a current GL context; when none can be made
  current, initialization SHALL be deferred and retried (never silently skipped by
  marking initialization done), and the logged tier SHALL report what is actually
  in use. This is a robustness requirement of the tiered rendering pipeline, not
  tied to the installer.

## Impact

- `scripts/installer/create_installer.py` — dependency-closure check (dumpbin over
  `dist\bin` and `dist\lib\avogadro\**`), Qt-module top-up copy (needs `Qt6_DIR` in the
  environment), Boost debug-DLL filter.
- `.github/workflows/windows-installer.yml` — real smoke-run step (polling, fail on
  early exit); pass `Qt6_DIR` through to the packaging step.
- `libavogadro/src/glwidget.cpp` — `resizeEvent`/`paintEvent` `makeCurrent()` checks,
  `initializeGL` context guard, deferred-init flag handling, fallback log message.
- `libavogadro/src/rendering/modernbackend.cpp` — message wording only (bail-out
  behavior already correct).
- No shader, geometry, or adoption-path changes; the modern tier's render paths are
  untouched. The `fix-modern-tier-revision-churn` change is unaffected (different
  files/paths).
