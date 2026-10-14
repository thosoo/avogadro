## Purpose

Selects the preferred GPU on hybrid-graphics Windows systems (NVIDIA Optimus, AMD switchable
graphics) without user action, and makes it observable which OpenGL renderer the application
actually obtained, so rendering performance problems can be attributed to the right device.

## ADDED Requirements

### Requirement: Default to the dedicated GPU on hybrid systems
On Windows, the application executable SHALL export the driver-recognized
high-performance GPU preference symbols (`NvOptimusEnablement` for NVIDIA hybrid systems,
`AmdPowerXpressRequestHighPerformance` for AMD hybrid systems) so that, when no user GPU
preference exists for the application, the process is routed to the dedicated GPU by
default. The mechanism SHALL require no user interaction and no elevated privileges.

#### Scenario: Hybrid laptop with no user preference
- **WHEN** a user launches the application on a Windows machine with an integrated and a
  dedicated GPU, having never set a per-app GPU preference
- **THEN** the OpenGL context is created on the dedicated GPU

#### Scenario: Single-GPU and non-Windows systems are unaffected
- **WHEN** the application runs on a machine with a single GPU, or on Linux or macOS
- **THEN** behavior is unchanged and startup succeeds as before

#### Scenario: Explicit user preference is respected
- **WHEN** the user has set an explicit per-app GPU preference in Windows Graphics settings
- **THEN** that preference wins over the application's exported hint

### Requirement: Fallback per-user GPU preference registration
The application MAY register its own Windows per-user GPU preference
(`GpuPreference=2;` under `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`) when it
detects a dedicated GPU and no preference entry exists for its executable. It SHALL NOT
modify an existing preference entry.

#### Scenario: No existing registry preference on a hybrid system
- **WHEN** the application starts on a hybrid-GPU machine and the per-user preference
  registry value for its executable is absent
- **THEN** the application writes a high-performance (`GpuPreference=2;`) preference for
  itself under HKCU without requiring elevation

#### Scenario: Existing preference is left alone
- **WHEN** the application starts and a per-user GPU preference already exists for its
  executable (including "power saving" or "let Windows decide")
- **THEN** the application makes no registry modification

### Requirement: Honest renderer diagnostics
The application SHALL report the OpenGL vendor, renderer, and version actually resolved
for its rendering context, obtained from the live context (e.g. `glGetString`), and SHALL
NOT present requested-but-unverified format attributes as established capabilities. The
diagnostics SHALL be emitted at startup logging level so that support and debugging can
attribute rendering behavior to a specific device.

#### Scenario: Startup logs the real renderer
- **WHEN** the first OpenGL view is initialized on any machine
- **THEN** the application logs the actual GL vendor, renderer name, and version of the
  created context

#### Scenario: Software fallback is visible
- **WHEN** the dynamic GL selection falls back to a software rasterizer (or a
  vendor-implemented GL 1.1 context) on Windows
- **THEN** the logged renderer name identifies the software implementation rather than
  claiming hardware direct rendering

#### Scenario: Misleading capability print is removed
- **WHEN** the application starts
- **THEN** no output claims "Direct Rendering." (or similar) based on the requested
  surface format rather than the resolved context
