# Proposal: fix-orbital-status-column

## Why

When an ORCA output is opened and orbitals are precalculated, the orbital
table's Status column shows only empty boxes instead of progress bars
(users expect populated status for prerendered orbitals), and scrolling
the table paints blue progress-bar fragments over unrelated cells that
persist until the rows scroll off-screen. Root cause (reproduced and
validated offscreen at both the widget and primitive level):
`ProgressBarDelegate::paint` builds its `QStyleOptionProgressBar` with
`initFrom()` only and never sets `QStyle::State_Horizontal`, so Qt 6.4's
Fusion style takes the *vertical* bar path — the percentage label is never
drawn, and the progress chunk is drawn through a rotated painter that lands
far outside the cell rect. Two further defects were confirmed while
diagnosing: the table never scrolls to the HOMO row on the first fill
(the user lands at the top of the descending-sorted list, far from the
precalculated region), and the model's progress-update methods index
`m_orbitals[orbital - 1]` without bounds checks (an out-of-range orbital
number aborts under assertions and is silent heap corruption in release).

## What Changes

- `ProgressBarDelegate::paint` sets the horizontal state flag on the style
  option so the status column renders real progress bars (groove, chunk,
  percentage text) and chunk pixels stay inside the cell rect on every tier
  of Qt style handling.
- The orbital table scrolls to the HOMO row on the first table fill after a
  document load, not only on subsequent fills: the scroll is retried once
  the view's pending layout settles instead of relying on a single
  `scrollTo` call that provably no-ops against a stale header state.
- `OrbitalTableModel`'s progress-update methods
  (`setOrbitalProgressRange`, `incrementStage`, `setOrbitalProgressValue`,
  `finishProgress`, `resetProgress`, `setProgressToZero`) reject orbital
  numbers without a corresponding row instead of writing through a
  dangling pointer; `clearOrbitals()` skips the invalid `(0, -1)` removal
  range on an already-empty model.
- Add a headless regression test that renders the status delegate offscreen
  and asserts chunk pixels stay inside the cell rect and the percentage
  label is drawn — the paint path had no test coverage (the recent
  fix-orbital-visualization suite passed 44/44 while this bug shipped).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `surface-visualization`: new requirements for the orbital table's status
  column rendering (populated bars with labels, no out-of-cell paint during
  scroll), for landing the table view on the HOMO row after the first fill,
  and for bounds-safe progress updates from the calculation queue.

## Impact

- `libavogadro/src/extensions/surfaces/orbitaltablemodel.h` — delegate
  paint fix; bounds-safe model mutations.
- `libavogadro/src/extensions/surfaces/orbitalwidget.cpp` — first-fill
  HOMO scroll behavior in `fillTable`.
- `libavogadro/tests/` — new offscreen paint regression test; bounds-check
  unit test (mirrors the orbitalselectiontest harness in
  `libavogadro/tests/CMakeLists.txt`).
- No API, packaging, or build-dependency changes. Windows/MSVC: touched
  code is compiled by the Windows installer job, so the Windows leg must be
  exercised before merge per repo policy.
