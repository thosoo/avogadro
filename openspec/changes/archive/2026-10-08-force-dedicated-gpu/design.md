## Context

Avogadro renders through Qt6 `QOpenGLWidget` with a fixed-function, per-primitive immediate
path; on hybrid-GPU Windows machines the process lands on the integrated GPU by default
(see proposal.md - Why). Static verification performed during exploration:

- The exe has no existing exports; `windows.h` is already included under `#ifdef WIN32` in
  `avogadro/src/main.cpp:61-63`.
- The hint carrier is `avogadro.exe` (`add_executable(avogadro-app WIN32 …)`,
  `OUTPUT_NAME avogadro` — `avogadro/src/CMakeLists.txt:86,94`).
- CI is MSVC-only (`.github/workflows/windows-installer.yml:20,244`), where
  `__declspec(dllexport)` on exe-scope variables works without a `.def` file.
- All `GLWidget`s use the default constructor or share the main widget's format
  (`glgraphicsview.cpp:35,49`, `mainwindow.cpp:3897,3901`) — one process-wide decision
  covers every view, including detached windows.
- The driver reads the exported hint symbols before the first context is created; nothing
  needs to run at startup for layer 1 (exports) to take effect.

## Goals / Non-Goals

**Goals:**
- Dedicated-GPU routing happens with zero user interaction on NVIDIA and AMD hybrid systems.
- Users (or support) can see which renderer was actually resolved.
- The change is inert on single-GPU Windows and on Linux/macOS.

**Non-Goals:**
- Any render-path performance work (immediate-mode batching, quick-render, selection,
  picking truncation) — separate change, design pending.
- Conditional MSAA sampling, software-GL detection dialogs, installer changes.
- Overriding an explicit user GPU preference — the OS wins by design; we only fix the
  silent default.

## Decisions

### D1: Driver-level hint exports as the primary mechanism
Export `NvOptimusEnablement = 0x00000001` and `AmdPowerXpressRequestHighPerformance = 1`
from the exe, guarded for Windows only, at file scope in `main.cpp` before `main()`.

- Why over alternatives: it is the documented, official mechanism (NVIDIA Optimus
  application hint; AMD PowerXpress); honored by the driver at context creation with no
  runtime code, no registry writes, no elevation. Alternatives considered:
  - Shipping an NVIDIA per-app profile — requires driver tooling/inf files, fragile.
  - Only the registry layer (D2) — less universal; driver hints are the canonical path.
- Guard style: reuse the file's existing `#ifdef WIN32` block convention (MSVC defines
  `WIN32`; Q_OS_WIN is also acceptable, picked `WIN32` for local consistency). Use
  `Q_DECL_EXPORT`-equivalent spelling via `__declspec(dllexport)` directly since the CI is
  MSVC-only; if a MinGW build ever appears, wrap in a macro that expands to
  `-Wl,--export-all-symbols` or a `.def` file.

- Implementation notes (exact shape):

  ```cpp
  #ifdef WIN32
    #include <windows.h>
    #include <stdlib.h>

    // Ask hybrid-GPU drivers (NVIDIA Optimus / AMD PowerXpress) to route this
    // process to the high-performance GPU. The driver reads these named exports
    // from the executable before the first OpenGL context is created. File scope
    // in the primary exe only; no runtime code required. Windows' per-user
    // Graphics-settings preference, when present, overrides these hints.
    extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
  #endif
  ```

  - Types follow the respective vendor docs: `DWORD` (== `unsigned long`, 32-bit on
    MSVC) for NVIDIA, `int` for AMD. `windows.h` (already included two lines above)
    supplies `DWORD`.
  - Placement: `avogadro/src/main.cpp`, inside the existing `#ifdef WIN32` include block
    (~line 61), file scope before `main()`. Do not place in `libavogadro` — the driver
    only inspects the primary executable.
  - Symbol names must match exactly (that is the entire mechanism); `extern "C"` prevents
    mangling.
  - Linux/macOS: the `#ifdef WIN32` guard keeps the file untouched; verify by building
    the `avogadro-app` target locally.

### D2: Registry fallback, write-only-if-absent
At startup (Windows only, after the application object exists), read
`HKCU\Software\Microsoft\DirectX\UserGpuPreferences`; if the value named by the exe's full
path is absent AND a second graphics adapter is present (EnumDisplayDevices probe), write
`GpuPreference=2;` for that path. Never modify an existing value, even
`GpuPreference=1;` (power saving) or `GpuPreference=0;` (let Windows decide).

- Why include it: driver hints cover the common case, but the registry key is what
  Windows' own preference engine consults and it survives driver updates that re-enumerate
  Optimus bindings. Writing only-if-absent is non-destructive and matches the spec's
  "existing preference is left alone" scenario.
- Why `HKCU` not `HKLM`: no elevation needed; this is exactly the key the Settings UI
  writes for the current user.
- Risk of being "undocumented": the key format is stable and widely used by applications;
  worst case a future Windows ignores it, which is today's behavior anyway.

- Implementation notes (exact shape):

  - Hook point: a file-static helper `static void registerUserGpuPreference();` in
    `main.cpp` under `#ifdef WIN32`, called from the existing `#ifdef WIN32` block in
    `main()` right after the `SetDllDirectoryW` / PATH setup (i.e. after
    `Application app(argc, argv);` exists — the helper needs
    `QCoreApplication::applicationFilePath()`).
  - Sequence (all best-effort; any failure → one `qDebug()` line, never fatal):
    1. Normalize the value name: `QCoreApplication::applicationFilePath()` with `'/'`
       replaced by `'\\'` (the Settings UI stores backslash paths; matching its format
       keeps the two views consistent).
    2. `RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
       0, KEY_READ, &key)` — if the key itself is missing, treat as "no preference".
    3. `RegGetValueW(key, nullptr, valueName, RRF_RT_REG_SZ, ...)` — if it returns
       `ERROR_SUCCESS`, a preference exists (any value: 0, 1, 2) → **return without
       writing**. If `ERROR_FILE_NOT_FOUND`, proceed.
    4. Adapter probe: `EnumDisplayDevicesW(NULL, i, &dd, 0)` over `i = 0..`, counting
       entries with `StateFlags & DISPLAY_DEVICE_ACTIVE` (requires `_WIN32_WINNT >=
       0x0601`; otherwise fall back to `DISPLAY_DEVICE_ATTACHED_TO_DESKTOP`). Write only
       when count ≥ 2 (hybrid system).
    5. Reopen the key with `KEY_SET_VALUE` and `RegSetValueExW(..., REG_SZ,
       (const BYTE*)L"GpuPreference=2;", byteLenIncludingNull)`.
    6. `RegCloseKey` on every path.
  - Semantics to document in the code comment: Windows applies `UserGpuPreferences` at
    **process launch**, so this write affects *subsequent* launches. Launch 1 relies on
    the D1 exports; launches 2+ are covered by both layers. This is acceptable — D2
    exists for drivers that ignore D1, not for launch 1.
  - Non-Windows: the entire helper is inside `#ifdef WIN32`, so nothing compiles into
    other platforms (task 2.2 verifies by grepping Linux objects).

### D3: Honest diagnostics via qDebug in initializeGL + removal of the fake print
Log `glGetString(GL_VENDOR)`, `GL_RENDERER`, `GL_VERSION` (and
`context()->format().samples()`) once per widget's first `initializeGL()`. Remove the
`std::cout` "OpenGL capabilities found: … Direct Rendering." block at
`main.cpp:277-283`, which inspects the requested format, not the resolved context.

- Why qDebug and not a dialog: zero-i18n-impact, works in CI/console capture, matches the
  codebase's existing qDebug usage in `initializeGL()`. A user-visible "About → Renderer"
  line is a possible follow-up but adds UI surface this change doesn't need.

- Implementation notes (exact shape):

  - In `GLWidget::initializeGL()` (`libavogadro/src/glwidget.cpp`, after the
    `context()->isValid()` guard — the context is current there), add one line:

    ```cpp
    qDebug() << "OpenGL renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
             << "vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR))
             << "version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION))
             << "requested samples:" << context()->format().samples();
    ```

    Once-per-widget is guaranteed by construction — `initializeGL()` runs exactly once per
    widget lifetime in both render paths (guarded by `d->initialized` in
    `paintEvent`/`resizeEvent`, and `m_initialized` in `GLThread::run`), so no extra flag
    is needed.
  - In `main.cpp`, delete ONLY the print block (the comment "Test what capabilities we
    have" plus the four `std::cout <<` lines and their `if`s, ~lines 277-283). The three
    lines immediately above — `QSurfaceFormat defFormat = QSurfaceFormat::defaultFormat();
    defFormat.setSamples(4); QSurfaceFormat::setDefaultFormat(defFormat);` — are the MSAA
    setup and MUST stay. `#include <iostream>` stays too (`printVersion`/`printHelp`
    still use `std::cout` on Windows).
  - Windows log capture (for verification): `avogadro.exe` is a GUI-subsystem exe, so
    qDebug goes to `OutputDebugString` by default. To read it from a console, launch with
    `set QT_FORCE_STDERR_LOGGING=1` (or `QT_ASSUME_STDERR_HAS_CONSOLE=1`); alternatively
    use DebugView. CI ctest output shows it directly.

## Risks / Trade-offs

- [Driver ignores the hint on some OEM Optimus implementations] → D2's registry fallback
  covers it; diagnostics (D3) make the failure visible instead of silent.
- [Writing the registry key could surprise users who later want the iGPU] → they set the
  Graphics-settings preference, which overrides both the key and the hint; we never touch
  an existing value.
- [Forcing dGPU increases power draw on battery] → accepted trade-off; correct default for
  a 3D molecular editor. Users can override per-app in Settings.
- [Exports are read only from the primary exe; tests/CLI tools never get the hint] →
  intended; rendering only happens in the GUI process.

## Migration Plan

Pure additive build change; no data migration. Rollback = revert the commit; the registry
value written by D2 is harmless if left behind but a cleanup task should note that
`--erase-config`-style removal is unnecessary (Windows ignores stale values once the exe
path no longer matches).

## Open Questions

None blocking. (If desired later: surface the renderer string in Help → About as a UI
follow-up; does not affect this design's decisions.)
