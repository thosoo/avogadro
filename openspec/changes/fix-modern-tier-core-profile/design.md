# Design: fix-modern-tier-core-profile

## Context

Diagnosed on two Linux machines (nouveau/Quadro FX 3800 — Debian 13; radeonsi/RX Vega 64 —
Ubuntu 24.04) and confirmed with screenshots, startup logs, gdb, and an LD_PRELOAD capture:

```
main.cpp:366  QOpenGLContext testContext; testContext.create();
              → creates Qt's GLOBAL SHARE CONTEXT with Qt's built-in default format
                (2.0, NoProfile) — captured: attribs 0x2091=2 0x2092=0, no 0x209B (profile)
main.cpp:378  QSurfaceFormat::setDefaultFormat(3.3, CompatibilityProfile);
              → Qt warns: "...after the global shared context is created..."
GLWidget paint → context resolves "profile Core" (both machines)
paintGL      → glMatrixMode/glLoadMatrix are dead on Core → GL stacks stay IDENTITY
currentModelViewProjection() → glGetFloatv reads {1,0,...,1} (gdb-proven on nouveau)
modern tier  → uMVP = identity → molecule drawn in raw world coordinates
               (giant, displaced, misshapen — the reported symptom)
compat/legacy tiers → black viewport; lighting init emits GL_INVALID_ENUM
```

Driver facts that rule out the drivers: `glxinfo` reports compat 4.6 on the Vega box, and a
raw `glXCreateContextAttribsARB` probe with (3.3, COMPATIBILITY) resolves
`4.6 (Compatibility Profile)` on the same display the app fails on. Even a (2.0, no-mask)
attribs request resolves compat. The divergence is introduced by Qt's context-creation
fallback once the global share context exists with a different format. Windows/WGL is
unaffected (compat always available and taken). Headless CI is unaffected (llvmpipe honors
compat; the GPU test layer resolves core and skips — the skip messages ("resolved GL
context is Core") were visible in consumerparitytest runs and misread as runner
limitations).

## Goals / Non-Goals

**Goals:**
- The global share context and all widget contexts are created with the app's requested
  format (3.3, CompatibilityProfile) on drivers that offer it.
- When the resolved profile is Core anyway, the app says so loudly and does not run
  fixed-function-dependent code against the context.
- Tier resolution (version-only today) becomes profile-aware.
- Same behavior on Windows/macOS paths; no regression on llvmpipe CI.

**Non-Goals:**
- Making the modern tier fully self-sufficient on Core contexts (camera matrices from the
  `Camera` object instead of GL stacks, shader-based overlays/text). Recorded as a
  follow-up candidate; not needed once the primary fix restores compat resolution.
- Supporting Core-profile-only drivers with a working legacy tier (frozen tiers accept
  this limitation; the requirement is honest reporting, not support).
- Changes to the instanced draw path, shaders, pick pass, or any tool code.

## Decisions

### D1: Reorder context-format initialization in main.cpp (root fix)
Delete the early `QOpenGLContext testContext` probe (or move it below
`setDefaultFormat`), and call `QSurfaceFormat::setDefaultFormat(3.3, CompatibilityProfile)`
before any context creation. The probe only decides "system supports OpenGL at all";
losing the early QMessageBox path is acceptable — an unusable GL stack now surfaces through
the widget's existing invalid-context diagnostics.
Alternative considered: keep the probe and recreate the global context after
setDefaultFormat — not possible through public Qt API; a QApplication restart hack is
worse than removing the probe.

### D2: Profile-aware tier support in renderertier.cpp
`contextSupportsTier()` gains a resolved-profile input: compat and legacy tiers require
`profile != CoreProfile`; the modern tier's floor check stays version-based (its own draws
are core-safe), with the fixed-function consumer degradation handled by D3. The
`Q_OS_MAC` legacy-default rule is unchanged. A forced tier that the resolved profile
cannot serve fails startup through the existing `Unavailable` path (design D7 of the
original tiering change).

### D3: Core-context degradation in GLWidget::initializeGL
When `context()->format().profile() == CoreProfile`: skip the fixed-function lighting init
block (the block already documents that it is invalid on Core), log one startup line
naming the resolved profile and the degraded consumers (axes glyph, text, tool overlays,
mesh flush), and route tier diagnostics accordingly. Reuse the existing Core check in
glpainter_p.cpp:282 as the pattern; extend it where the painter would emit per-frame
errors.

### D4: Verification instrumentation stays as-is
The resolved-version/profile startup log (added Nov 1) is the acceptance instrument: on
affected machines the line must flip from "profile Core" to "profile Compatibility" after
D1, or — on core-only drivers — the D2/D3 diagnostics must appear. No new test framework;
add a pure unit check for the profile-aware tier predicate where the existing
renderertier tests live.

## Risks / Trade-offs

- [Reordering exposes systems with truly broken GL earlier] → The probe's modal
  "system does not support OpenGL" path is replaced by the widget's existing
  invalid-context handling; startup still fails with a message, not a crash.
- [Qt fallback may still yield Core on some drivers even with a correct order] → D2/D3
  make that state loud and non-corrupt; the follow-up (Camera-matrix decoupling) is the
  durable escape hatch and is documented.
- [Behavior change on macOS] → The format block is already `#ifndef Q_OS_MAC`; macOS keeps
  its legacy-default path. No interaction with this change.
- [llvmpipe CI differences] → llvmpipe honors compat; the suite is expected green. The
  consumerparity skips on core contexts become diagnosable rather than mysterious.

## Migration Plan

Single-commit reorder plus guarded diagnostics; no data or settings migration. Rollback =
revert the commit.

## Open Questions

None material. The exact Qt-internal fallback step that converts the widget's compat
request into Core (sharing fallback vs EGL/GX integration choice) does not change the
chosen approach: D1 removes the trigger, D2/D3 contain any residue.
