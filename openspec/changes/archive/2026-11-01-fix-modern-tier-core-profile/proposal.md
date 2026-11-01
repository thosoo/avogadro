# Proposal: fix-modern-tier-core-profile

## Why

On both Linux machines in daily use (nouveau/Quadro FX 3800 and radeonsi/RX Vega 64), the
modern renderer tier renders every molecule displaced and misshapen: giant atoms, bonds
poking through spheres, missing-triangle crescents. The root cause is proven: Qt resolves
the app's GL context to a **Core** profile even though CompatibilityProfile is requested,
because `main.cpp` creates a context (`testContext`) *before* calling
`QSurfaceFormat::setDefaultFormat(3.3, CompatibilityProfile)` — poisoning the global share
context with Qt's built-in 2.0/NoProfile default (confirmed with an LD_PRELOAD capture of
`glXCreateContextAttribsARB attribs: 2.0, no profile mask`, plus Qt's own
"setting a new default format … after the global shared context is created" warning). On a
Core context the camera is applied through the fixed-function matrix stack, which is dead,
so the modern tier reads identity matrices (`uMVP = identity`, gdb-proven) and draws the
molecule in raw world coordinates. Windows/WGL resolves the same code to a compatibility
context, which is why only Linux looks broken. Headless CI never sees this because llvmpipe
honors compat requests.

## What Changes

- Set the default `QSurfaceFormat` (3.3, CompatibilityProfile) **before any context is
  created** — remove or relocate the `testContext` probe in `main.cpp` so the global share
  context is born with the app's requested format.
- Make renderer-tier resolution **profile-aware**: `contextSupportsTier()` currently checks
  only the GL version; a resolved Core profile must be reported loudly and must disable the
  fixed-function-dependent tiers (compat/legacy) with honest diagnostics, per the existing
  gpu-rendering-pipeline requirements (no silent degradation).
- Guard the fixed-function initialization in `GLWidget::initializeGL` (lighting block) and
  degrade fixed-function-only consumers (axes glyph, text, tool overlays, legacy painter)
  with a visible diagnostic when the context is Core, instead of silently rendering nothing
  or emitting per-frame `GL_INVALID_ENUM` noise.
- **No changes** to the modern tier's instanced draw path, shaders, pick pass, or the
  OpenBabel pin. The optional deeper hardening (feeding camera matrices from the `Camera`
  object instead of the GL stacks, so the modern tier would fully work on core-only
  drivers) is recorded in design.md as a follow-up non-goal for this change.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `gpu-rendering-pipeline`: add a requirement that the compatibility context request
  precedes all context creation and that a resolved Core profile is detected and reported
  honestly (tier gating + diagnostics), instead of silently running the fixed-function
  dependent paths on a context that cannot execute them.

## Impact

- **Modified:** `avogadro/src/main.cpp` (context creation order), 
  `libavogadro/src/rendering/renderertier.{h,cpp}` (profile-aware tier support),
  `libavogadro/src/glwidget.cpp` (profile diagnostics, guard fixed-function init),
  possibly `libavogadro/src/glpainter_p.cpp` (existing Core-profile guard reuse).
- **No changes:** rendering backends, shaders, geometry, chunked uploads, pick path,
  tools, OpenBabel submodule.
- **Verification:** startup log on affected machines must report the resolved profile and
  either a working compat context or a loud, explicit core-profile diagnostic; molecule
  render must match the legacy/Windows appearance; existing full test suite stays green
  (llvmpipe machines unaffected).
