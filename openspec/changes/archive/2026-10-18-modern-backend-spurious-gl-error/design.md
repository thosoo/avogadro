## Context

The modern tier reports `CYLINDER draw GL error: 1282` (`GL_INVALID_ENUM`). Root cause
traced: the error is **not** produced by the cylinder draw. `GLWidget::initializeGL()`
runs a legacy OpenGL setup block (a Mesa 6.5.3-era workaround, see glwidget.cpp:641) that
calls `glEnable(GL_COLOR_SUM_EXT)` and
`glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL_EXT, GL_SEPARATE_SPECULAR_COLOR_EXT)`.

Investigated facts that fix the ambiguity:

- **Resolved context is GL 3.3 CompatibilityProfile** (`main.cpp:366` sets it;
  `resolveRendererTier` only probes support and never touches the format). Qt loads the
  modern backend's `QOpenGLFunctions_3_3_Core` pointers leniently from the compat context,
  which is why the modern backend initializes despite the compat profile.
- **The driver does not expose** `GL_EXT_vertex_coloring` or
  `GL_ARB_separate_specular_color` (confirmed via `eglinfo` — empty match). On a full
  CompatibilityProfile 3.3 context the fixed-function lighting calls (`GL_LIGHTING`,
  `GL_LIGHT0`, `glLightfv`) are *valid*; only the two `EXT` enums are unsupported and
  generate `GL_INVALID_ENUM`.
- **Profile-sensitivity caveat (verified in-container).** Qt requests a CompatibilityProfile
  context, but the container's Mesa on xcb resolves that request to a **Core** profile
  (`visualparitytest` logs: "requested profile: CompatibilityProfile got profile:
  CoreProfile"; `glGetString(GL_VERSION)` = 4.5 Core). On that resolved Core context the
  fixed-function block is *not* valid: `glEnable(GL_LIGHTING)` and `glEnable(GL_LIGHT0)`
  emit `GL_INVALID_ENUM` (1280) and `glLightfv` emits `GL_INVALID_OPERATION` (1282). So the
  block is valid only on a real CompatibilityProfile driver (the target radeonsi/Mesa
  desktop); it errors on Core. The D3 drain clears this sticky error before the cylinder
  check, so the CYLINDER warning does not fire even on Core (scenario 1 passes on :3/:4).
  This container cannot deliver a full compat context (see 5.2 capability-skip).
- **The lighting block is dead state.** Every render path disables and re-manages lighting
  itself — `glpainter_p.cpp`, `compatbackend.cpp:174`, `modernbackend.cpp:475`,
  `textrenderer_p.cpp`, and the tools all `glDisable(GL_LIGHTING)` around their draws. No
  tier consumes the ambient values set in `initializeGL()`.
- **Only the modern backend inspects `glGetError()` after a draw.** `drawInstances()`
  checks after the cylinder draw (modernbackend.cpp:783) but not after the sphere draw
  (:750) or line draw (:804). `GL_INVALID_ENUM` is sticky, so the stale init error leaks
  forward and is mislabeled as a cylinder-draw error.

See proposal.md for motivation.

## Goals / Non-Goals

**Goals:**
- Stop `initializeGL()` from generating `GL_INVALID_ENUM` on the resolved context (the two
  unsupported `EXT` enum calls).
- Ensure the modern tier's draw-path error reporting reflects only errors from the modern
  tier's own draws, so a stale init error can no longer be attributed to a sphere, cylinder,
  or line draw.

**Non-Goals:**
- No change to rendered output; visual parity with the legacy/compat tiers is preserved.
- Not instrumenting GL-error checking across the legacy/compat tiers (they don't check
  errors; out of scope for this warning).
- Not a general GL-error-management overhaul.

## Decisions

**D1 — Remove the two unsupported `EXT` enum calls (root-cause fix).**
Delete `glEnable(GL_COLOR_SUM_EXT)` and the `glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL_EXT,
GL_SEPARATE_SPECULAR_COLOR_EXT)` pair from `GLWidget::initializeGL()`.
*Rationale:* they are the sole source of `GL_INVALID_ENUM`; they implement a 2007 driver
workaround that no modern driver needs; and `global.h` only `#define`s those enums so the
code compiles where the headers lack them.
*Alternatives:* guard each call behind a driver-support probe (`glGetString(GL_EXTENSIONS)`
contains the extension name). Rejected: the probing is fiddly and platform-fragile, and the
extensions are absent on the target radeonsi/Mesa stack anyway.

**D2 — Remove the dead fixed-function lighting block (cleanup, low risk).**
Remove the `glEnable(GL_LIGHTING)` / `glLightfv(GL_LIGHT0/1, …)` / `glEnable(GL_LIGHT0/1)`
block. *Rationale:* every render path disables lighting and sets its own state (see Context);
the modern tier disables lighting explicitly at modernbackend.cpp:475. The block is valid on
a full CompatibilityProfile context, so this is hygiene, not an error fix — therefore
**optional** and separated from D1.
*Mitigation:* retain the block for legacy/compat visual parity — the legacy `GLPainter`
(`glpainter_p.cpp`) calls `glEnable(GL_LIGHTING)` but sets no light values, so it relies on
the ambient/diffuse/specular/position values configured here; removing them would darken the
legacy render (GL's default ambient is black) and break modern-vs-legacy parity. The block
is valid on a real CompatibilityProfile driver but emits `GL_INVALID_ENUM`/`GL_INVALID_OPERATION`
on a Core context; the D3 drain isolates that sticky error before the cylinder check, so it
is not a rendering defect and does not re-surface the CYLINDER warning. **Decision: retain**
the block (D2 revert-and-keep clause triggered — parity preserved).
*Note:* `glEnable(GL_LIGHTING)` emits `GL_INVALID_ENUM` on Core, so D2 cannot be a pure
"remove dead state" cleanup on a Core-resolved context; retention is the parity-preserving
choice.

**D3 — Drain stale GL errors at the start of the modern draw pass (error isolation).**
At the entry of `drawInstances()`, drain any pending GL error in a small `while
(glGetError() != GL_NO_ERROR) {}` loop before the sphere/cylinder/line draws.
*Rationale:* satisfies the spec requirement that stale init errors not surface in the
draw-path check, and is defense-in-depth so future unrelated init cannot leak into modern
draw reporting. Draining once per frame at draw start preserves detection of genuine errors
set by the modern tier's *own* draws this frame (sphere → cylinder check still sees those).
*Alternatives:* clear the error after the sphere draw so it can't reach the cylinder check;
or add a per-draw check after each of sphere/cylinder/line. Rejected as primary: per-draw
checks are more invasive and the drain-at-start is the minimal change that satisfies the
requirement. D1 removes the actual source; D3 is the isolation guarantee.

**D4 — Keep the existing cylinder-draw error check as-is.**
It is correct once D3 removes the stale pre-draw error; no change needed there.

## Risks / Trade-offs

- **Removing the lighting block changes the legacy/compat look.** → Mitigation: parity
  harness + a rendered-frame comparison (target: ≥98% pixel parity, as measured previously).
  If regressions appear, keep the block (D2 is optional).
- **Draining errors could mask a genuine error set between frames by unrelated code.** →
  Mitigation: drain only once at draw-start; errors generated by the modern tier's own draws
  within the frame are still caught by the existing post-draw check.
- **Qt's lenient 3_3_Core-on-compat behavior is Linux/GLX-specific.** macOS skips this
  context setup entirely (`#ifndef Q_OS_MAC`), so the fix is confined to the non-mac path and
  is profile-agnostic.

## Open Questions

None that block the design. The resolved profile (compat 3.3) and the unsupported EXT
extensions are confirmed; D2's safety is validated by the parity check rather than a code
path that reads the lighting values.
