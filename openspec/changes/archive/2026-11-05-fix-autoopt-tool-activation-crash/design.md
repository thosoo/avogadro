# Design: fix AutoOpt tool activation crash

## Context

Two reachable null/garbage dereference states exist in `AutoOptTool` (see proposal.md —
Why for the field evidence and the affected machine profile):

1. `m_glwidget` (`autoopttool.h:203`, no initializer; absent from the ctor init list,
   `autoopttool.cpp:55-58`). Assigned only by `paint()` (273) and the mouse handlers
   (142/183/200). The renderer's honest-degradation behavior on Core-profile-only GL
   stacks (see `gpu-rendering-pipeline` spec) skips the active-tool overlay pass, so on
   the affected machine `paint()` never runs. `enable()` (429/431/437/438),
   `disable()` (487), and `timerEvent()` (501/525/536/538) dereference it unchecked.
2. `m_thread` is created only after the constructor's force-field lookup succeeds; on
   the failure path the constructor returns early (77-79) with `m_thread == 0`, while
   `paint()` (245) and `mouseReleaseEvent()` (191) dereference `m_thread`
   unconditionally.

The headless harness cannot see either path: the live-GUI AutoOpt tests skip on the
offscreen platform, and force-field lookups already fail in test binaries (the tool is
constructed in `autooptreprotest` and survives because neither `paint()` nor a canvas
mouse release runs with the tool active).

## Goals / Non-Goals

**Goals:**
- No crash for any tool-lifetime ordering: start before first paint, activation on
  overlay-skipping renderers, canvas mouse events with no force field, stop without a
  session.
- Behavior of a fully-initialized session is byte-for-byte unchanged.
- Minimal, localized diff in `autoopttool.{h,cpp}` plus headless regression tests.

**Non-Goals:**
- No `Tool` API change (no widget-plumbing refactor such as a `Tool::setWidget` hook).
- No change to the renderer's overlay gating itself (owned by `gpu-rendering-pipeline`).
- No attempt to render the AutoOpt HUD on Core-profile contexts.
- No change to the optimization session, worker threading, or snapshot model.

## Decisions

### D1: Initialize `m_glwidget` to nullptr
Add `m_glwidget(0)` to the constructor's init list (matching the file's existing style;
an in-class `= nullptr` default would be equivalent). This converts the indeterminate
pointer into a checkable one.

### D2: Guard the dereferences; bail gracefully instead of dereferencing
- `enable()`: if `m_glwidget` is null, emit `qWarning` and return without starting the
  session (no timer, no worker, no undo command). The user's next canvas interaction
  associates the widget and Start then works normally.
- `disable()` (487) and `timerEvent()` (501/525/536/538): null-check `m_glwidget`
  before `update()`/`molecule()` use. `timerEvent` can only run while a session is
  active, but the guard costs nothing and removes the ordering assumption.
- Also guard `m_comboFF` in `enable()`'s force-field lookup (415): it is created in
  `settingsWidget()`, which is only guaranteed to exist after the dock was built.
- Alternative considered — plumb the active `GLWidget` into tools at activation (a
  `Tool::setWidget` hook): rejected; it is a cross-tool API change that belongs in its
  own change. The guards make the ordering explicit and safe without it.

### D3: Treat a missing force field as "tool inert" (D7 semantics for AutoOpt)
Mirror the DrawTool D7 decision already specified in `add-draw-tool-click-extend`:
when `m_forceField` is null (and therefore `m_thread` is null), skip force-field
bookkeeping instead of dereferencing:
- `paint()`: return early (before the `m_thread->snapshot()` read) when `m_thread` is
  null. With no force field the HUD block is unreachable anyway (`m_running` can never
  become true), so the early return only removes the crash.
- `mouseReleaseEvent()`: skip `m_thread->setPendingFixAtom(0)` when `m_thread` is null.
- The constructor's existing guards (start button disabled when `m_forceField` is null)
  already keep the user from starting a session; these two guards cover the paths that
  fire without the user pressing Start.

### D4: Headless regression tests in `autooptreprotest`
The test binary already compiles `autoopttool.cpp` directly (with
`AVOGADRO_TEST_NOPLUGIN`), so the tool can be constructed and driven headlessly:
- Construct a fresh `AutoOptTool`, invoke `enable()` before any `paint()`/mouse call →
  must not crash and must not report running.
- Invoke `mouseReleaseEvent()` on a fresh tool → must not crash (in CI the force-field
  lookup fails, so this also exercises the `m_thread == 0` path).
- Existing tests (17 + 2 live-GUI skips) must stay green.

### D5: Field verification on the affected machine
Re-run the instrumented-launch runbook from the diagnosis session (breakpoints on
`AutoOptTool::paint` and `AutoOptTool::enable` printing `m_glwidget`, plus core dumps
armed): activate + Start without touching the canvas must not crash; the log documents
whether the overlay pass runs on that context (answering the one open field question)
and captures a backtrace if anything still fails.

## Risks / Trade-offs

- [Guards could mask a genuine initialization-order bug] → every guard emits `qWarning`
  with the function name, so field logs expose any unexpected bail-out instead of
  failing silently.
- [Start appears clickable but no-ops until the view is touched] → acceptable: with the
  overlay pass skipped the tool has no earlier signal that a view exists; the
  diagnostic tells the user, and one canvas interaction fixes it. A `Tool::setWidget`
  API (rejected above) would remove even this, at the cost of a cross-tool refactor.
- [CI force-field availability differs from developer machines] → the new tests must
  pass in *both* states (FF null in CI, FF present locally); write them so the
  assertion is "no crash + not running", not "FF missing".

## Migration Plan

Single self-contained fix; no data or settings migration. Rollback = revert the commit.

## Open Questions

- Whether the active-tool overlay pass really never runs post-activation on the
  affected box (field experiment pending link stability). The answer does not change
  the fix — the guards remove the undefined behavior either way — but it documents
  which scenario users were actually hitting.
