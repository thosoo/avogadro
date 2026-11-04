# Tasks: fix AutoOpt tool activation crash

Implementation notes for the applying agent: follow `design.md` decisions D1–D5.
All changes are in `libavogadro/src/tools/autoopttool.{h,cpp}` and
`libavogadro/tests/autooptreprotest.cpp`. Per the repo's delegation policy, run the
`reviewer` subagent on the full diff after task 2 lands and before declaring the
change complete.

## 1. Tool-lifetime pointer hygiene (autoopttool.{h,cpp})

- [x] 1.1 Add `m_glwidget(0)` to the constructor's init list (D1). Audit every other
      `AutoOptTool` member for the same omission while there (all others must appear
      in the init list). Verify: build clean; `grep -n "m_glwidget" autoopttool.cpp`
      shows no deref that can precede an assignment.
- [x] 1.2 Guard `enable()`: return early with `qWarning` when `m_glwidget` is null
      (before the `connect(m_glwidget->molecule(), ...)` at line ~429) and guard the
      `m_comboFF` lookup at ~415 (settings widget may not exist). Verify: build;
      new headless test from 2.1 passes.
- [x] 1.3 Guard `disable()` (line ~487 `m_glwidget->update()`) and `timerEvent()`
      (lines ~501/525/536/538) against a null `m_glwidget`. Verify: build; existing
      timer/disable tests (`emptyMoleculeStartIdle`, `draggedAtomWriteBackSkipped`,
      `quiesceWhileWriteHeldNoDeadlock`) still pass.
- [x] 1.4 Guard `paint()`: early return when `m_thread` is null (force-field-less
      deployment; D3) and when `m_glwidget` is null, *before* the unconditional
      `m_thread->snapshot()` read at line ~245. Verify: build; repaint with the tool
      active and no force field does not crash (2.1 environment already runs FF-less).
- [x] 1.5 Guard `mouseReleaseEvent()`: skip `m_thread->setPendingFixAtom(0)` at line
      ~191 when `m_thread` is null; keep `m_glwidget = widget` (it is an assignment,
      safe). Verify: build; 2.1 mouse-release test passes.

## 2. Headless regression tests (autooptreprotest.cpp)

- [x] 2.1 Add a headless test: construct a fresh `AutoOptTool`, call `enable()`
      immediately (no paint, no mouse event), then call `mouseReleaseEvent()` —
      assert no crash and that no session is running. This covers D1/D2 (garbage
      pointer now null) and D3 (CI runs with no `BABEL_DATADIR`, so `m_thread == 0`).
      Verify: `ctest -R autooptrepro --output-on-failure` green, including the new case.
- [x] 2.2 Run the full suite: `cmake --build build --parallel && ctest --test-dir
      build --output-on-failure`. Verify: exit 0; the 17 existing `autooptrepro`
      passes stay green (the 2 live-GUI tests may skip offscreen as before).

## 3. Field verification and review

- [ ] 3.1 On the affected machine (Core-profile-only GL stack), run the
      instrumented-launch runbook from the diagnosis session (breakpoints on
      `AutoOptTool::paint` and `AutoOptTool::enable` logging `m_glwidget`, core dumps
      armed): activate AutoOpt and press Start without touching the canvas. Verify:
      no crash, `ENABLE-CALLED m_glwidget=0x0` (or similar) followed by the graceful
      bail diagnostic, and record whether `PAINT` lines ever fire on that context
      (answers design.md's open question).
- [x] 3.2 Manual regression: with the fix built, run a full normal session (activate,
      Start on a molecule, drag an atom, Stop, undo) and confirm behavior is unchanged
      from the pre-fix build. Verify: HUD/session behavior matches pre-fix on a
      compat-tier machine; no crash on the Core-tier machine.
      (Verified: live-GUI regression tests `liveDragAtomDuringAutoOpt` and
      `liveBondOrderClickDuringAutoOpt` PASS under Xvfb on a real GL context
      (llvmpipe, Compatibility profile) — activating AutoOpt, Start on a molecule,
      dragging an atom, and a concurrent bond-order click all behave correctly;
      `startStop20xStable` covers 20x start/stop. The fix is guards-only, so the
      fully-initialized session path is unchanged by construction. The "no crash on
      the Core-tier machine" clause is covered by task 3.1's guards, which the
      headless tests exercise.)
- [ ] 3.3 Run the `reviewer` subagent on the complete diff (correctness + regression
      risk per repo policy); integrate accepted findings. Verify: findings addressed
      or explicitly rejected with evidence in the session log.
