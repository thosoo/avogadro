# gpu-rendering-pipeline Delta

## ADDED Requirements

### Requirement: Compatibility context request precedes all context creation
The application SHALL configure its default surface format — GL version and
CompatibilityProfile — before any OpenGL context is created in the process, so that the
global share context and every widget context are created with the requested profile. A
context created before the default format is configured (a probe or any early GL use)
SHALL NOT poison subsequent context resolution. On systems whose driver offers a
compatibility profile, startup SHALL resolve a compatibility context.

#### Scenario: Compat-capable driver resolves Compatibility
- **WHEN** the application starts on a system whose GL driver offers a compatibility
  profile (for example radeonsi, or any Windows/WGL driver)
- **THEN** the startup log reports the resolved profile as Compatibility, and the first
  frame renders with correct camera projection

#### Scenario: No default-format warning at startup
- **WHEN** the application starts
- **THEN** Qt does not emit the "setting a new default format … after the global shared
  context is created" warning, because no context exists before the default format is
  configured

### Requirement: Resolved profile is detected and reported honestly
After the context is resolved, the application SHALL inspect the resolved profile. If the
resolved profile is Core — whether because the driver ignores the compatibility request or
for any other reason — the application SHALL log a startup diagnostic naming the resolved
version and profile, and SHALL NOT run fixed-function-only code paths against that context
(matrix-stack camera updates, lighting initialization, display-list rendering). Those
paths SHALL be disabled with a visible, explicit diagnostic instead of silently producing
corrupt output or per-frame GL errors.

#### Scenario: Core-only driver gets a loud diagnostic, not garbage
- **WHEN** the application starts on a system whose driver resolves the compatibility
  request to a Core profile (for example nouveau on pre-Kepler NVIDIA)
- **THEN** the startup log names the resolved profile as Core, the fixed-function-only
  paths are reported as unavailable, and the render does not present
  identity-transformed geometry as if it were correct

#### Scenario: No GL error storm from fixed-function init
- **WHEN** the application initializes a Core-profile context
- **THEN** fixed-function state initialization (lighting enums, matrix-stack operations)
  is skipped, and the debug log contains no per-frame `GL_INVALID_ENUM` reports from
  initialization

### Requirement: Tier selection evaluates the resolved profile
Renderer-tier resolution SHALL evaluate the resolved context profile in addition to the
GL version. Tiers that depend on fixed-function GL (compat, legacy) SHALL be considered
unavailable on a Core-profile context, and a forced tier that cannot run on the resolved
profile SHALL fail startup with the existing clear diagnostic rather than silently
degrading.

#### Scenario: Forced legacy tier on a Core-only context fails loudly
- **WHEN** `AVOGADRO_RENDERER=legacy` is set on a system whose context resolves to a Core
  profile
- **THEN** the application reports that the requested tier cannot run on the resolved
  context (profile requirement not met) instead of presenting a blank viewport

#### Scenario: Default tier choice on a Core-only context is explicit
- **WHEN** no tier override is set and the context resolves to a Core profile
- **THEN** the chosen presentation path is logged with its actual profile constraint, so
  field reports can be attributed to the resolved context rather than guessed
