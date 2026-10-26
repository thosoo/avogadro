# Tasks

## 1. Shared-context fix (crash fix)

- [x] 1.1 `avogadro/src/main.cpp`: set `QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts)`
      immediately before `Application app(argc, argv);`. Verified: Linux build
      compiles; attribute is set before QApplication construction.
- [x] 1.2 `libavogadro/src/glwidget.cpp` `~GLWidget`: wrap the GL-object-owning
      teardown (painter release, engines, `delete d`) in
      `makeCurrent()`/`doneCurrent()`, guarded on `context() && context()->isValid()`.
      Verified: Linux build + full ctest green; never-shown/headless widget path
      skips GL calls.
- [x] 1.3 Full local suite: `cmake --build build -j8 && ctest --test-dir build
      --output-on-failure` green on Linux. Verified 2026-10-26: build green
      (-j8), ctest exit 0, all tests pass on the default platform. Note: forcing
      `QT_QPA_PLATFORM=offscreen` locally segfaults the legacy-tier widget parity
      tests with "QOpenGLWidget is not supported on this platform" — an
      offscreen-platform artifact, verified 2026-10-26 by a checkout-based A/B
      (ce2b54205 pre-fix code + offscreen → identical SEGFAULT; HEAD fix code +
      offscreen → identical SEGFAULT), not a regression of this fix. An earlier
      stash-based baseline was voided by a wrong CMake target name and briefly
      overstated this verification; this A/B supersedes it.
- [x] 1.4 Windows legs: both workflows green on the fix commit; installer artifact
      produced. Verified 2026-10-26: Qt6 CI run 40328029120 and Windows Installer
      run 40328028783 both completed/success on headSha `ac487391e`; the installer
      artifact `avogadro-win64-1.2.0.exe` was produced and uploaded.
- [x] 1.5 User-machine discriminating test: install the fix artifact on the
      reporting machine (Quadro P3200 / 573.22), run **outside** the debugger,
      twice (fresh + reused settings). Window presents and survives; Event Viewer
      shows no `nvoglv64` access violation. Verified 2026-10-26: user reported
      "the GUI is working again with the latest exe created by
      windows-installer.yml". Outcome: cross-context UB CONFIRMED as the root
      cause; the crash is fixed on the reporting machine.

## 2. Bookkeeping

- [x] 2.1 Record the oracle validation + this change's scope in
      `fix-windows-first-run-crash` artifacts (root cause moved here).
      Verified 2026-10-26: design.md Context/Open Questions updated with the
      oracle verdict and the pointer to this change.
