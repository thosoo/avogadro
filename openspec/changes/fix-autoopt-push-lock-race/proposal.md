## Why

Drawing a bond between two existing carbons while an AutoOpt session runs crashes the
application intermittently. The mechanism is confirmed: since dac66e87c the AutoOpt worker
reads the live molecule under the read lock at tick rate (20 Hz), but the undo-stack push
path applies command `redo()`/`undo()` bodies (add/remove atoms, hydrogen adjustment storms)
with **no lock held** — the writer side of the molecule lock protocol was never enforced.
Confirmed experimentally: with "adjust hydrogens" disabled (which removes the
removeAtom/addAtom churn from the stroke-release path) the crash disappears.

Two adjacent defects in the same surface are included: DrawTool's temporary fix/ignore
force-field markers use 0-based indices against OpenBabel's 1-based convention (fixing the
wrong atom, or none at index 0), and a failed force-field `Setup` leaves a 20 Hz
retry-livelock (each tick's `setup()` resets `m_stop`).

## What Changes

- New `Avogadro::UndoStack` (QUndoStack subclass) whose `push()` holds the molecule write
  lock while the command applies, with a debug-only assert that no molecule lock is held on
  entry. One class covers **all 14 push sites in 3 files** (7 in GLWidget, 6 in MainWindow,
  1 in CrystallographyExtension) via the single shared stack instance.
- Worker-side read acquisition hardened to a `tryLockForRead` retry honoring `m_stop`, so
  `disable()`/`abort()` cannot deadlock against a blocked worker read (oracle hazard H3).
  Wrapping `QUndoStack::undo()`/`redo()` themselves is **explicitly deferred** until this
  hardening exists (it does, after this change) — but not done here.
- DrawTool stroke fix/ignore markers use `index() + 1` (1-based OpenBabel convention).
- Failed force-field `Setup` terminates the tick session deterministically instead of
  retrying forever.
- Extras (audit/hardening tasks, same surface): empty-molecule Start repro test (reported
  crash, still undiagnosed at HEAD); `AutoOptCommand` partial-copy/assignment audit; stop
  re-resolving `m_forceField` from the GUI thread every tick outside `m_mutex`.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `auto-optimize-interaction`: new requirements — GUI-side molecule mutations (including
  undo/redo command application via `QUndoStack::push`) serialize with worker reads through
  the molecule write lock; drag fix/ignore markers reference the intended atom under the
  1-based convention; a failed force-field setup ends the session deterministically.

## Impact

- New: `libavogadro/src/undostack.{h,cpp}` (QUndoStack subclass).
- `avogadro/src/mainwindow.cpp` — instantiate `Avogadro::UndoStack` (single shared instance,
  already shared with GLWidget via `setUndoStack` and extensions via `undoStack()`).
- `libavogadro/src/tools/drawtool.cpp` — marker index fix.
- `libavogadro/src/tools/autoopttool.{h,cpp}` — worker read-acquisition retry, livelock fix,
  `m_forceField` resolution cleanup.
- `libavogadro/tests/autooptreprotest.cpp` — new regression scenarios (stroke release with
  hydrogen churn during run, undo-delete during run, quiesce-while-write-held,
  push-outside-GLWidget, empty-molecule start).
- Risk: holding the write lock during `push()` serializes command signals against worker
  ticks (worker tick latency during a locked redo — accepted, correct serialization; signal
  re-entrancy under the lock was verified safe: the reachable slot chain contains no
  blocking lock acquisition and no event-loop re-entry).
