## 1. Driver-level hint exports (D1)

- [x] 1.1 In `avogadro/src/main.cpp`, inside the existing `#ifdef WIN32` include block (~line 61, after `windows.h`), add at file scope before `main()`: `extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;` and `extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;` with an explanatory comment (exact code shape in design.md D1). Verify guards exclude it on this Linux machine: `cmake --build build --target avogadro-app` compiles clean
- [x] 1.2 Verify the symbols are exported from a Windows build (CI installer artifact or local MSVC build): `dumpbin /exports avogadro.exe | findstr /i "NvOptimus AmdPowerXpress"` — both names must appear, unmangled

## 2. Registry fallback (D2)

- [x] 2.1 In `main.cpp` add a `#ifdef WIN32` file-static helper `registerUserGpuPreference()` implementing the D2 sequence (design.md D2: read `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` via `RegGetValueW` with backslash-normalized `applicationFilePath()` as value name; only-if-absent; `EnumDisplayDevicesW` probe ≥2 active adapters; write `GpuPreference=2;` REG_SZ via `RegSetValueExW`; `RegCloseKey` on every path; best-effort with qDebug on failure). Call it from the existing `#ifdef WIN32` block in `main()` after the PATH setup. Verify on Windows: fresh profile → `reg query "HKCU\Software\Microsoft\DirectX\UserGpuPreferences"` lists the exe path with `GpuPreference=2;` after first run; a pre-existing value (e.g. `GpuPreference=1;`) is byte-identical after a run; effect documented as launch-2+ (Windows applies the key at process start)
- [x] 2.2 Verify no registry/adapter-probe code compiles into non-Windows builds: `grep -c UserGpuPreferences build/` over Linux objects returns 0 and the Linux build still passes

## 3. Honest renderer diagnostics (D3)

- [x] 3.1 In `GLWidget::initializeGL()` (`libavogadro/src/glwidget.cpp`), after the `context()->isValid()` guard, add the one-line qDebug from design.md D3 (`glGetString` RENDERER/VENDOR/VERSION + `context()->format().samples()`); no extra once-flag needed (initializeGL runs once per widget in both render paths). Verify by running the app on this machine and checking the log prints the real renderer string (Mesa/NVIDIA, not a generic claim); on Windows capture with `set QT_FORCE_STDERR_LOGGING=1` or DebugView
- [x] 3.2 Delete only the print block at `avogadro/src/main.cpp:277-283` (comment + four `std::cout` lines); KEEP the three `defFormat`/`setSamples(4)`/`setDefaultFormat` lines above it (MSAA setup) and `#include <iostream>` (still used by `printVersion`/`printHelp`). Verify startup output no longer prints "OpenGL capabilities found:" while the app still starts normally with MSAA intact

## 4. Validation

- [x] 4.1 Full build + test suite passes locally: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
- [x] 4.2 On a hybrid-GPU Windows machine: launch a clean build (no user GPU preference set) and confirm in Task Manager → Details → "GPU engine" column that avogadro.exe runs on the dedicated GPU (spec scenario "Hybrid laptop with no user preference"); also check the D3 log names the NVIDIA/AMD renderer string
- [x] 4.3 On a single-GPU machine and on Linux: behavior and startup unchanged (spec scenario "Single-GPU and non-Windows systems are unaffected")
