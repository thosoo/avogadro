# Tasks

## 1. Draw tool state lifetime fix

- [x] 1.1 `drawtool.h`/`drawtool.cpp`: convert `m_beginAtom`, `m_endAtom`,
      `m_prevBond`, `m_bond` from raw pointers to ids; resolve via
      `atomById`/`bondById` at each use with null checks. Verify: compiles
      under GCC/Clang (local build green); MSVC confirmed by the Windows CI
      legs in task 1.4.
- [x] 1.2 Null-guard the two crash chains (mousePressEvent line ~236,
      mouseReleaseEvent line ~567) and audit every remaining
      `atomById(...)->` / `bondById(...)->` chain in drawtool.cpp; missing
      objects reset the affected state. Also: cross-event state is abandoned
      (not dereferenced) when a held id no longer resolves or the molecule was
      replaced between events; transient fix/ignore markers are unset on
      abandon; `m_beginMolecule` is a `QPointer`. Verify: code review against
      the cdb faulting instruction (`cmp [rax+38h], 1`, rax=0) plus a new
      `staleOrReplacedMoleculeAbandonsStroke` test exercising the abandon path.
- [x] 1.3 Local verification: `cmake --build build -j8 && ctest --test-dir
      build --output-on-failure` green (default platform; offscreen artifacts
      documented in the archived A/B). Verify: local run — 34/34 tests pass,
      including the new `autooptreprotest::staleOrReplacedMoleculeAbandonsStroke`.
- [ ] 1.4 Windows legs: both workflows green on the fix commit. Verify:
      `gh run list` on the pushed commit.
- [ ] 1.5 User-machine repro: draw/undo/redraw loop with "adjust hydrogens" on
      — no crash across at least 20 strokes (the previous faulting sequence).
      Verify: user-reported outcome.

## 2. Bookkeeping

- [x] 2.1 Note the crash attribution in `fix-modern-tier-revision-churn`
      (drawing instability has a second, independent component: this tool-state
      bug) so the churn change keeps its display-stability scope.
