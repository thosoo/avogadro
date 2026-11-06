# Design: fix-orbital-status-column

## Context

All three defects were reproduced offscreen with a throwaway probe that
compiles the real widget/model/delegate sources (see `Apply notes` for the
harness recipe). Evidence collected:

- **Delegate**: `ProgressBarDelegate::paint` (`orbitaltablemodel.h`)
  initializes `QStyleOptionProgressBar` with `initFrom()` and never sets
  `QStyle::State_Horizontal`. On Qt 6.4.2 Fusion, a bare
  `drawControl(CE_ProgressBar)` without that flag renders the chunk at a
  rect unrelated to `opt.rect` (measured: `opt.rect=(300,100,154x29)` →
  chunk pixels at `QRect(0,297,54x32)`) and never draws the label; with the
  flag, the chunk fills exactly `opt.rect` and the label renders. The real
  table showed empty status cells plus blue chunk fragments stamped on
  unrelated rows that persist after settling (captured on a live X11
  presentation path, so it is not an offscreen artifact).
- **First-fill scroll**: `fillTable` ends with
  `scrollTo(mapFromSource(HOMO), PositionAtCenter)`. A gdb breakpoint on
  `QTableView::scrollTo` proves the call happens with a *valid* index
  (row 69, col 0) yet the scrollbar never moves; an identical replay call
  in the same event-loop turn succeeds (scrollTop 66). The vertical header
  and scrollbar range are stale during the failing call (measured
  immediately after: `count=120, max=111, sectionPos(69)=2070`) — the
  clear + 120-insert storm + `ResizeToContents` + `sort` cascade leaves
  the view in a pending-layout state that the first scroll call only
  flushes as a side effect. Only the FIRST fill is affected; every later
  fill scrolls correctly. Qt's own `scrollToBottom()` guards this exact
  case (`if (d->delayedPendingLayout) { executePostedLayout();
  updateGeometries(); }`), but `QTableView::scrollTo` has no such guard in
  Qt 6.4.2.
- **Bounds safety**: `w.updateProgress(1000, 50)` on a 120-row table
  aborts with `QList::operator[]: index out of range` under assertions and
  is an out-of-bounds heap write in release. Reachability today is latent
  (the extension's generation guard and disabled widget currently prevent
  stale calls), but the mutators are public API on a widget-lifetime
  object, so any future caller regresses to UB. `clearOrbitals()` on an
  empty model calls `beginRemoveRows(0, -1)` — Qt 6 tolerates this
  silently as an empty range (verified: no warning, no crash), so this is
  hygiene, not corruption.

## Goals / Non-Goals

**Goals:**
- Status column renders real bars with labels on every tier of Qt style
  handling used by the supported platforms.
- Scrolling never deposits paint outside the painted cell.
- First fill lands the view on the HOMO row like later fills.
- Progress mutators are memory-safe for any orbital number.
- Offscreen regression coverage for the paint path.

**Non-Goals:**
- Replacing the table model/proxy machinery or the fill sequence with a
  model reset (`beginResetModel`) — behavior-neutral refactor, deferred.
- Redesigning the pending-orbital indication (the 0%-bar-for-queued look);
  only the rendering fidelity is in scope.
- Any change to the calculation queue, threading, or surface pipeline.

## Decisions

### D1 — Delegate fix: set `State_Horizontal` on the style option
`opt.state |= QStyle::State_Horizontal;` after `initFrom()` in
`ProgressBarDelegate::paint`.

- *Alternative: copy the view's state (`opt.state = o.state`)* — tested
  and rejected: the item view's `QStyleOptionViewItem.state` does not
  carry `State_Horizontal`, so the vertical path is still taken.
- *Alternative: embed real `QProgressBar` widgets per row* — rejected:
  memory churn per update storm, and model-view style conventions prefer
  delegates.
- *Alternative: custom paint (self-drawn groove/chunk/label)* — rejected:
  platform-native look lost, more code, no need.
- Keep `opt.minimum = 1` as-is: a queued orbital renders an empty groove
  with a "0%" label, which reads correctly.
- MSVC note: this is a header-only inline change in a class compiled into
  the Windows installer job; no MSVC-specific construct is added (a plain
  `|=` on a public enum flag).

### D2 — First-fill HOMO scroll: defer the scroll until layout settles
After populating in `fillTable`, schedule the HOMO scroll with a
zero-delay, single-shot, view-anchored mechanism (`QTimer::singleShot(0, …)`
capturing the view) instead of relying on the in-call `scrollTo`. The
deferred call re-resolves the HOMO index through the proxy and scrolls to
center only if the row still exists (document could have switched in
between — re-check via the existing widget state; the widget is torn down
with its parent so a plain lambda over `ui.table` is safe). The existing
in-call `scrollTo` is kept: it is correct on later fills and the deferred
call is a no-op when the view is already at HOMO (value-set clamp).

- *Alternative: force layout synchronously before scrolling
  (`executePostedLayout` is private Qt API)* — rejected: private-symbol
  dependence.
- *Alternative: replace remove-all/insert-storm fill with a single model
  reset and scroll after* — attractive but touches the churn behavior the
  selection tests exercise; deferred (see Non-Goals).
- This preserves the "only intentional behavior" constraint of the
  existing fills: no behavior change on fills 2..N beyond a redundant
  scroll.

### D3 — Bounds-safe progress mutators
Each of `setOrbitalProgressRange`, `incrementStage`,
`setOrbitalProgressValue`, `finishProgress`, `resetProgress`,
`setProgressToZero` returns early when
`orbital < 1 || orbital > m_orbitals.size()` (orbital numbers are 1-based)
without emitting `dataChanged`. `clearOrbitals()` returns early when the
list is empty (skipping the invalid `(0, -1)` range). Silent ignore (no
warning) matches the stale-callback convention of the extension (N.1).

- *Alternative: `Q_ASSERT` + no guard* — rejected: release builds would
  still corrupt the heap; the assert build aborts, which is worse than
  ignoring an update for a row that cannot exist.
- `resetProgress` currently has no callers; it is hardened anyway to keep
  the invariant uniform across the mutator family.

### D4 — Offscreen paint regression test
New headless test executable (mirroring the `orbitalselectiontest` CMake
pattern: compiles the plugin sources directly, `QT_QPA_PLATFORM=offscreen`)
that renders the status delegate into a `QImage` at a known cell rect and
asserts, style-independently:
1. **No paint outside the cell**: every non-background pixel in the image
   lies within `opt.rect` (catches the rotated-chunk regression).
2. **Label is drawn**: at least one non-background pixel exists inside the
   cell (catches the empty-groove regression).
Pixel-class assertions avoid depending on Fusion's highlight-blue value,
so the test is valid under any QPA/style.

Additional unit coverage in the same executable:
3. Progress mutators ignore orbital numbers beyond the row count
   (spy `dataChanged`: no emission; process stays alive — the pre-fix
   behavior aborts under `QT_FORCE_ASSERTS`).
4. `clearOrbitals()` on an empty model emits no removal notification.
5. First-fill HOMO landing: build the widget, `fillTable` with N orbitals,
   process events, assert the scrollbar places the HOMO row inside the
   viewport.

## Risks / Trade-offs

- [Delegate flag changes bar metrics under non-Fusion styles (Windows
 vista11, macOS)] → The flag selects the horizontal path all these styles
  expect for a horizontal bar; the old behavior was the pathological path
  everywhere. Manual smoke on Windows via the installer workflow before
  merge (repo policy for shared UI code).
- [Deferred scroll could fire after a document switch] → The lambda
  re-resolves HOMO through the live proxy and scrolls only if found; the
  widget (and its children) are destroyed with the dock, so the timer is
  anchored to a dead object → use a context object (`QTimer::singleShot(0,
  ui.table, …)`) so the call is dropped if the table is gone.
- [Paint test could be brittle across Qt point releases] → Assertions are
  structural (pixels inside/outside a rect), not pixel-value equality.
- [Bounds guards could mask a real indexing bug in the queue] → The guards
  ignore exactly the impossible-by-construction case; the existing N.1
  generation guard remains the primary correctness mechanism, and the unit
  test pins the ignore semantics.

## Migration Plan

Single-commit change; revert reverts everything. No data, settings, or API
migration. Windows installer workflow run required before merge (touched
code compiles there).

## Open Questions

None.
