# Tasks: fix-orbital-status-column

## 1. Delegate paint fix (D1)

- [x] 1.1 In `ProgressBarDelegate::paint` (`libavogadro/src/extensions/surfaces/orbitaltablemodel.h`), add `opt.state |= QStyle::State_Horizontal;` after `opt.initFrom(...)` and verify the existing suites still pass: `cmake --build build --target orbitalselectiontest && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R orbitalSelection --output-on-failure`

## 2. Bounds-safe model mutators (D3)

- [x] 2.1 Add 1-based bounds guard to `setOrbitalProgressRange`, `incrementStage`, `setOrbitalProgressValue`, `finishProgress`, `resetProgress`, `setProgressToZero` in `orbitaltablemodel.cpp` (early return, no `dataChanged` when `orbital < 1 || orbital > m_orbitals.size()`) and make `clearOrbitals()` return early on an empty list; verify by compiling the surfaces plugin and running `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R orbital --output-on-failure`

## 3. First-fill HOMO scroll (D2)

- [x] 3.1 In `OrbitalWidget::fillTable` (`orbitalwidget.cpp`), defer the HOMO scroll with `QTimer::singleShot(0, ui.table, …)` that re-resolves the HOMO row through `m_sortedTableModel`, re-validates it, and calls `scrollTo(PositionAtCenter)`; keep the existing in-call scroll; verify later fills still land on HOMO and the first fill now does (test in 4.x)

## 4. Regression tests (D4)

- [x] 4.1 Add `orbitalstatustest` executable to `libavogadro/tests/CMakeLists.txt` following the `orbitalselectiontest` pattern (compiles surfaces plugin sources, `QT_QPA_PLATFORM=offscreen`, registered with ctest) and verify it builds: `cmake --build build --target orbitalstatustest`
- [x] 4.2 Implement the delegate-paint test (design D4 assertions 1–2): render the status delegate at a known cell rect into a `QImage`, assert all non-background pixels lie inside the cell rect and at least one non-background pixel (label) lies inside; run and confirm green with the fix and red when the flag line from 1.1 is reverted (validates the test bites)
- [x] 4.3 Implement model-bounds tests (design D4 assertions 3–4): spy `dataChanged` while applying each progress mutator with an out-of-range orbital (verify no emission, process survives) and call `clearOrbitals()` on an empty model (verify no removal notification); also verify in-range updates still emit exactly one `dataChanged`
- [x] 4.4 Implement first-fill HOMO test (design D4 assertion 5): fresh widget, `fillTable` with 120 orbitals, process events, assert the HOMO view row is inside the viewport (scrollbar value > 0 and HOMO row visible)
- [x] 4.5 Run the full new suite plus neighboring suites: `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R "orbital|surface" --output-on-failure` and confirm all green

## 5. Review and CI

- [x] 5.1 Self-review the diff against design D1–D4 (flag placement, guard semantics, timer context object anchoring to `ui.table`) and remove the throwaway probe artifacts if any were staged
- [ ] 5.2 Trigger the Windows installer workflow (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`) and verify the build and test suite pass on MSVC per repo policy before opening a PR
