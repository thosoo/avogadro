# Tasks

## 1. Draw tool state lifetime fix

- [x] 1.1 `drawtool.h`/`drawtool.cpp`: convert `m_beginAtom`, `m_endAtom`,
      `m_prevBond`, `m_bond` from raw pointers to ids; resolve via
      `atomById`/`bondById` at each use with null checks. Verify: compiles
      under GCC/Clang (local build green); MSVC confirmed by the Windows CI
      legs in task 1.4.
- [x] 1.2 Null-guard the crash chains and audit every unresolved deref in
      drawtool.cpp; missing objects reset/abandon the affected state. Also:
      cross-event state is abandoned (not dereferenced) when a held id no
      longer resolves or the molecule was replaced between events; transient
      fix/ignore markers are unset on abandon; `m_beginMolecule` is a
      `QPointer`; `molecule->atom(index)->isHydrogen()` in `mouseMoveEvent`
      (lines ~346/354) is null-checked, since `atom()` returns null for an
      out-of-range or churn-stale index and that path was not part of the
      id-conversion. Verify: cdb on the reporting machine (Quadro P3200) at
      commit 59ab882 faulted again at `cmp [rax+38h],1` inside
      `GLWidget::mouseMoveEvent` -> drawtool `mouseMoveEvent`; the stack
      pinpointed the `existingAtom->isHydrogen()` chain, which this guard
      fixes. Commit bd5705269. A new
      `staleOrReplacedMoleculeAbandonsStroke` test exercises the abandon path.
- [x] 1.3 Local verification: `cmake --build build -j8 && ctest --test-dir
      build --output-on-failure` green (default platform; offscreen artifacts
      documented in the archived A/B). Verify: local run — 34/34 tests pass,
      including the new `autooptreprotest::staleOrReplacedMoleculeAbandonsStroke`.
- [x] 1.4 Windows legs: both workflows green on the fix commit. Verify:
      `gh run list` on the pushed commit — HEAD bd5705269 (second fix):
      Qt6 CI #40552905861 `completed success` with `100% tests passed, 0 tests
      failed out of 34` (incl. autooptreproTest); Windows Installer
      #40552906078 `watch --exit-status` = 0 with Build + Create installer +
      Test legs green (log publication delayed by an artifact-upload infra
      hang, same as the first commit's legs). First commit 59ab882d6 also had
      both legs green (#40507761729, #40507761707).
- [ ] 1.5 User-machine repro: draw/undo/redraw loop with "adjust hydrogens" on
      — no crash across at least 20 strokes (the previous faulting sequence).
      Verify: user-reported outcome. The first repro (commit 59ab882) crashed
      at a third path (`existingAtom->isHydrogen()` in `mouseMoveEvent`), now
      fixed in bd5705269. User must reinstall the latest Windows Installer and
      re-run the repro to confirm no crash.

## 2. Bookkeeping

- [x] 2.1 Note the crash attribution in `fix-modern-tier-revision-churn`
      (drawing instability has a second, independent component: this tool-state
      bug) so the churn change keeps its display-stability scope.
