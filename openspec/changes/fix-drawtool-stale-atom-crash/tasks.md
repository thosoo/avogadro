# Tasks

## 1. Draw tool state lifetime fix

- [ ] 1.1 `drawtool.h`/`drawtool.cpp`: convert `m_beginAtom`, `m_endAtom`,
      `m_prevBond`, `m_bond` from raw pointers to ids; resolve via
      `atomById`/`bondById` at each use with null checks. Verify: compiles
      under GCC/Clang and MSVC.
- [ ] 1.2 Null-guard the two crash chains (mousePressEvent line ~236,
      mouseReleaseEvent line ~567) and audit every remaining
      `atomById(...)->` / `bondById(...)->` chain in drawtool.cpp; missing
      objects reset the affected state. Verify: code review against the cdb
      faulting instruction (`cmp [rax+38h], 1`, rax=0).
- [ ] 1.3 Local verification: `cmake --build build -j8 && ctest --test-dir
      build --output-on-failure` green (default platform; offscreen artifacts
      documented in the archived A/B). Verify: local run.
- [ ] 1.4 Windows legs: both workflows green on the fix commit. Verify:
      `gh run list` on the pushed commit.
- [ ] 1.5 User-machine repro: draw/undo/redraw loop with "adjust hydrogens" on
      — no crash across at least 20 strokes (the previous faulting sequence).
      Verify: user-reported outcome.

## 2. Bookkeeping

- [ ] 2.1 Note the crash attribution in `fix-modern-tier-revision-churn`
      (drawing instability has a second, independent component: this tool-state
      bug) so the churn change keeps its display-stability scope.
