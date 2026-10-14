## Why

On hybrid-GPU Windows machines (NVIDIA Optimus, AMD switchable graphics), Windows routes
Avogadro to the power-saving integrated GPU by default. The application never signals a GPU
preference, so large-molecule rendering lands on the bandwidth-starved iGPU and the whole app
feels slow and sloppy. Fixing this per-machine requires a manual setting the user has to
discover; we cannot depend on users doing that. There is also no way to tell which renderer a
given machine actually used: the startup "capabilities" output inspects the *requested* format
rather than the resolved context and always reports "Direct Rendering." (main.cpp:271-283).

## What Changes

- Export the driver-level high-performance GPU hint symbols from the Windows executable
  (`NvOptimusEnablement` for NVIDIA, `AmdPowerXpressRequestHighPerformance` for AMD) so
  hybrid-GPU systems route the process to the dedicated GPU by default, with no user action.
- Optionally self-register a Windows per-user GPU preference (`GpuPreference=2;` under
  `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`) **only when no preference exists**,
  as a fallback for machines where driver-level hints are insufficient. A user's explicit
  choice is never overwritten.
- Report honest renderer diagnostics: log the resolved context's actual
  `GL_VENDOR` / `GL_RENDERER` / `GL_VERSION` at OpenGL initialization, and remove (or correct)
  the misleading always-positive startup print.
- Non-goal (deferred): all render-path performance work — immediate-mode batching,
  quick-render display-list churn, selection scans, picking truncation. That is a separate,
  not-yet-designed change. Forcing the dGPU is expected to materially improve
  gp23-monomer-scale scenes but will not fix capsid-scale scenes.

## Capabilities

### New Capabilities
- `hybrid-gpu-selection`: How the application selects the preferred GPU on hybrid-graphics
  Windows systems and reports which renderer it is actually using.

### Modified Capabilities

## Impact

- `avogadro/src/main.cpp` — exported hint symbols (Windows-only guard, file scope).
- `libavogadro/src/glwidget.cpp` — `initializeGL()` gains renderer logging via
  `glGetString` from the live context.
- `avogadro/src/main.cpp:271-283` — bogus capability print removed or replaced.
- Windows installer is the only affected artifact (exports live in `avogadro.exe`; the CI
  MSVC toolchain supports `__declspec(dllexport)` on the exe — verified in
  `.github/workflows/windows-installer.yml`, `avogadro/src/CMakeLists.txt:86,94`).
- No API or file-format changes; no effect on Linux/macOS builds (guarded).
