# Tasks

## 1. GL initialization guard (crash fix)

- [x] 1.1 `GLWidget::resizeEvent`: check `makeCurrent()`'s return; on failure skip
      all GL work (`initializeGL`/`resizeGL`) and still `emit resized()`. Verify:
      headless unit — construct a `GLWidget` in an offscreen/test environment where
      `makeCurrent` fails and assert no GL call is attempted (no crash, no
      backend init) and `resized()` is emitted.
- [x] 1.2 `GLWidget::paintEvent` (non-threaded path): check `makeCurrent()`'s
      return; on failure skip initialization and `paintGL()`. Verify: same headless
      setup — no paint attempted, no crash.
- [x] 1.3 `GLWidget::initializeGL`: early-return when
      `QOpenGLContext::currentContext()` is null **before** any GL call, and do NOT
      set `d->initialized` on that path. Verify: unit — call `initializeGL` with no
      current context; assert it returned cleanly, `d->initialized` stayed false, and
      a later call with a context current performs full initialization (tier logged).
- [x] 1.4 Correct the fallback diagnostics: when the modern backend fails to
      initialize, log that the widget is presenting via the fixed-function path until
      recovery; log once when `renderModern()`'s lazy recovery succeeds. Verify:
      manual — the second-widget log line no longer claims "using the modern tier"
      while presenting fixed-function.
- [ ] 1.5 Manual Windows verification of the crash fix: build a fix-branch installer
      (`gh workflow run windows-installer.yml --ref <branch>`), install on the
      reporting machine (Quadro P3200 / 573.22), launch — window appears, first paint
      survives, no `nvoglv64` crash in Event Viewer. Verify: user-reported outcome;
      run twice (fresh settings and reused settings).

## 2. Installer dependency closure (plugin fix)

- [x] 2.1 Extend `create_installer.py` with the dependency-closure scan: collect
      every PE under `dist\bin` and `dist\lib\avogadro\**`, run
      `dumpbin /dependents`, resolve imports against `dist\bin` plus the
      system/redistributable allowlist, print a binary→missing-DLL report and raise
      on any unresolved import. The existing zlib check is subsumed. Verify: run the
      packaging script against the current dist (with `Qt6Concurrent.dll` absent) —
      the scan reports `qtaimextension.dll`, `surfaceextension.dll` (via
      `avogadro_OpenQube.dll`), and `avogadro_OpenQube.dll` themselves missing it,
      and fails; after 2.2 it passes.
- [x] 2.2 Qt-module top-up: pass `Qt6_DIR` into the packaging environment (workflow
      Configure step already pins it), and in `create_installer.py` copy every
      unresolved `Qt6*.dll` import from `Qt6_DIR\bin` into `dist\bin` before the
      gate. Verify: packaged tree contains `bin\Qt6Concurrent.dll`; the closure scan
      passes with zero unresolved non-system imports.
- [x] 2.3 Boost debug-DLL filter: when copying the OpenBabel prefix's `bin` into
      `dist\bin`, exclude debug-runtime DLLs (`*-gd-*` variants; anything importing
      `ucrtbased`/`MSVCP140D`/`VCRUNTIME140D*`), keeping release DLLs that packaged
      binaries import (`boost_iostreams/bzip2/zlib` via `maeparser.dll`). Verify:
      packaged tree contains no `*-gd-*` DLLs; `maeparser.dll` still resolves its
      imports; package shrinks by ~17 MB.

## 3. CI smoke run repair

- [x] 3.1 Rewrite the "Capture live renderer log" step: launch the packaged exe with
      stderr/stdout capture; poll for the startup banner (`Avogadro version:`) up to
      ~20 s; classify the outcome — alive at deadline (healthy), banner + exit
      (healthy only if renderer line or documented headless `qFatal` tier message is
      present), exit before banner with no output (FAIL the job, print captured
      output). Never `Stop-Process` before the log has been read. Verify: workflow
      run on the branch shows the step's evidence in the log (banner observed, or
      documented headless outcome) and the step's verdict logic exercised in both
      directions by a temporary forced-failure check (removed after confirming).

## 4. Scenario verification

- [x] 4.1 Full suite: `cmake --build build --parallel && ctest --test-dir build
      --output-on-failure` green on Linux (guard change compiles under
      GCC/Clang/ASan legs). Verify: local build + ctest.
- [ ] 4.2 Windows leg: installer workflow green end-to-end on the fix branch,
      including the new closure gate and smoke run. Verify: `gh run watch` on the
      branch run; installer artifact produced.
- [ ] 4.3 User-machine acceptance: fresh install from the fix-branch artifact —
      window appears on first launch; DbgView log shows zero "failed to load" plugin
      lines; Orbitals/Surfaces/QTAIM extensions present in the Extensions menu.
      Verify: user-reported outcome (screenshots or log paste).
- [x] 4.4 Trigger the Windows workflow per AGENTS.md if the C++ guard change lands
      without a green Windows leg. Verify: workflow run for the branch is green
      before PR.
