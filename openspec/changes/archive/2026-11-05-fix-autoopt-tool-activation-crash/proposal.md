# Proposal: fix AutoOpt tool activation crash (uninitialized tool-lifetime pointers)

## Why

The AutoOpt tool crashes intermittently on some machines when the user activates it and
starts an optimization (field report: Debian 13 / Quadro FX 3800 / Mesa nouveau — a
Core-profile-only GL stack where the renderer honestly degrades and the active-tool
overlay pass never runs). Static root-cause analysis found two unguarded tool-lifetime
pointer states, each a reachable segfault:

1. `AutoOptTool::m_glwidget` is declared without an initializer (`autoopttool.h:203`)
   and is missing from the constructor's init list (`autoopttool.cpp:55-58`). It is
   assigned only in `paint()` (line 273) and the mouse handlers (142/183/200). On a
   Core-profile context the tool-overlay pass is skipped (per the
   `gpu-rendering-pipeline` honest-degradation behavior), so `paint()` never runs and
   `m_glwidget` stays indeterminate until a canvas mouse event happens to fire.
   `enable()` dereferences it unchecked (`autoopttool.cpp:429/431/437/438`) — pressing
   Start without first touching the canvas calls through a garbage pointer.
   `disable()` (487) and `timerEvent()` (501/525/536/538) have the same exposure.
2. When no force field is available (the same class as the DrawTool D7 defect already
   scoped in `add-draw-tool-click-extend`), the constructor bails *before* creating
   `m_thread`, yet `paint()` (245: `m_thread->snapshot()`) and `mouseReleaseEvent()`
   (191: `m_thread->setPendingFixAtom(0)`) dereference `m_thread` unconditionally —
   any mouse release with the tool active crashes on a force-field-less deployment.

Both defects are intermittent-by-construction (they depend on heap contents, GL profile,
and interaction order), which is why every headless harness stays green while real GUI
sessions die.

## What Changes

- Initialize `m_glwidget = nullptr` (in-class default and/or constructor init list).
- Null-guard every `m_glwidget` dereference in `enable()`, `disable()`, and
  `timerEvent()`: bail gracefully (no session, log/status diagnostic) when no view has
  been associated yet, instead of dereferencing.
- Null-guard `m_thread` in `paint()` and `mouseReleaseEvent()` so a missing force field
  degrades to "tool inert" (mirroring the DrawTool D7 semantics: skip marker
  bookkeeping when there is no force field) instead of crashing.
- No behavior change in the common path (force field present, view associated): the
  optimization session, HUD, snapshot reporting, and drag flows are untouched.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `auto-optimize-interaction`: adds a requirement that AutoOpt tool UI interactions
  (start/stop, mouse events, paint) are safe in every tool-lifetime state — before any
  painted frame, on renderer tiers that skip the tool overlay, and when no force field
  implementation is available.

## Impact

- **Code**: `libavogadro/src/tools/autoopttool.h` (member initializers),
  `libavogadro/src/tools/autoopttool.cpp` (null guards in `enable()`, `disable()`,
  `timerEvent()`, `paint()`, `mouseReleaseEvent()`).
- **Tests**: `libavogadro/tests/autooptreprotest.cpp` gains headless cases (start before
  any painted frame; mouse release with no force field) — the test binary already
  compiles `autoopttool.cpp` directly.
- **No changes**: rendering pipeline/tiers, force-field or threading model, other tools.
  The fix converts crashes into graceful no-ops; verified behavior of a fully
  initialized session is unchanged.
