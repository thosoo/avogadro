## Context

AutoOpt runs a worker thread (`AutoOptThread`) that, every ~50 ms tick, acquires the
molecule read lock, copies the molecule to a private OpenBabel OBMol, scans it, releases
the lock, then runs force-field setup/optimization on the private copy (dac66e87c). The
lock only excludes writers that hold the write lock. Audit of the writer side found the
undo-stack push path mutates the molecule with no lock at all:

```
DrawTool::mouseReleaseEvent   tryLockForWrite → build command → unlock → return command
GLWidget::mouse*Event         d->undoStack->push(command)          ← no lock held
  → command redo()/undo()     addAtom/removeAtom/addBond, QList::removeAt,
                              atom->deleteLater(), revision++      ← races worker reads
```

Two concrete UAF instantiations: (a) `Molecule::OBMol()` iterating `m_atomList` while a
GUI-side `removeAt`/`push_back` shifts the list; (b) the worker's atom scan dereferencing
an `Atom` that `deleteLater()` has freed on the GUI event loop mid-read. Push sites: 7 in
`glwidget.cpp`, 6 in `avogadro/src/mainwindow.cpp`, 1 in
`crystallographyextension.cpp:459` (via `GLWidget::undoStack()`). All share the one stack
instance created at `mainwindow.cpp:549` and distributed via `gl->setUndoStack(...)`.

Experimentally confirmed: disabling adjust-hydrogens (which removes all
removeAtom/addAtom churn from the release path) makes the crash disappear.

An oracle falsification pass (recorded below) validated the fix shape and found the trap
that shapes the sequencing (H3).

## Goals / Non-Goals

**Goals:**
- One choke point that serializes *every* command application against worker reads.
- A debug guard that turns "forgot the lock" into a loud failure instead of a rare crash.
- Fix the two adjacent confirmed defects on the same surface (marker off-by-one, setup
  retry livelock).
- Regression tests that pin the class permanently, runnable headless on CI.

**Non-Goals:**
- Wrapping `QUndoStack::undo()`/`redo()` in the write lock (deferred — see D3).
- Redesigning molecule threading (snapshot molecule, copy-on-write, message-passing).
- The modern-tier render-side findings (separate changes:
  `fix-modern-tier-bond-interaction`, `fix-modern-tier-revision-churn`).

## Decisions

1. **`Avogadro::UndoStack` (QUndoStack subclass) overrides `push()`.**
   `push()` wraps `QUndoStack::push(cmd)` in the molecule write lock and asserts, debug
   builds only, that the calling thread holds no molecule lock on entry (a self-deadlock on
   the non-recursive `QReadWriteLock` would otherwise be a hang instead of a message).
   Rationale: all 14 push sites in 3 files already route through this single shared
   instance — including the extension path — so one override covers every current and
   future caller with identical semantics. Alternatives rejected: per-command locks
   (6+ command classes × redo+undo bodies, and every future command re-opens the race);
   architectural rewrites (disproportionate).

2. **Re-entrancy under the lock is verified safe.** With the write lock held during
   `push → redo()`, the reachable slot set is: Molecule's own update slots (flag sets +
   re-emits, no locks); GLWidget's `unselectPrimitive/unselectAtom` (no lock),
   `invalidateDLs`, `sceneContentsChanged`, `update()` (schedules repaint, no event-loop
   re-entry); `updateGeometry` uses `tryLockForRead` and fails safe (one skipped geometry
   update); engine/extension slots update GUI tables without molecule locks. No
   `processEvents` or modal dialog is reachable. `deleteLater()` stays deferred (it posts
   an event; it does not run inline), and once mutations serialize with worker reads the
   worker can never hold an atom-list view spanning a deletion — both UAF instantiations
   close.

3. **Do NOT wrap `undo()`/`redo()` in this change — the deadlock trap.** `createUndoAction`
   (`mainwindow.cpp:586`) invokes `QUndoStack::undo()` directly, bypassing push, and
   `AutoOptCommand::undo()` → `disable()` → `m_thread->wait()`. If undo ran under the write
   lock, the worker could block on that lock mid-read-acquisition and `wait()` would never
   return. Instead this change hardens the worker's read acquisition (D4) so the
   quiesce-under-lock property holds; actually locking the undo/redo path becomes safe
   afterwards and is left as a deliberate follow-up.

4. **Worker read acquisition becomes stop-aware.** `AutoOptThread::update()` acquires the
   molecule read lock via a bounded retry (`tryLockForRead` + short yield) that returns
   immediately when `m_stop` is set, so quiesce cannot deadlock against a GUI writer and
   the wait-vs-tick contention class disappears.

5. **Fix/ignore markers use the 1-based convention.** DrawTool's
   `SetFixAtom(existingAtom->index())` / `SetIgnoreAtom(index)` become `index() + 1`,
   matching `AutoOptTool::setPendingFixAtom(idx)` and OpenBabel's `GetIdx()` convention.
   The 0-based form fixed the wrong atom and was indistinguishable from "no marker" at
   index 0.

6. **Setup failure ends the session.** Remove the per-tick `m_stop` reset (move session
   reset semantics out of the per-tick `setup()` path so a resumed tick cannot silently
   resurrect a failed session): after a `Setup` failure the tick loop stops, `m_setupFailed`
   drives the HUD, and only an explicit user start begins a new session.

7. **Extras (tasks, same surface):** empty-molecule Start repro test (reported crash that
   static analysis could not reproduce at HEAD — pin it headless first); `AutoOptCommand`
   partial-copy audit (its `Molecule` copy is a documented `FIXME` and its undo assignment
   replaces every `Atom*`/`Bond*` — selection-set and engine-cache hazard, worker quiesced
   so not the crash class); stop re-resolving `m_forceField` from the GUI thread each tick
   outside `m_mutex`.

## Risks / Trade-offs

- [Worker tick latency during a locked command application] → Accepted and correct: the
  worker's next read waits for the locked mutation (ms-scale). Documented here so nobody
  later "narrows" the lock scope and reopens the race.
- [Lock-during-push changes signal timing] → Verified safe (D2); the debug assert (D1)
  converts any missed re-entrancy into a loud failure.
- [Shared-file overlap with `fix-modern-tier-*` changes] → Disjoint files (tools +
  mainwindow vs rendering); no ordering constraint.

## AutoOptCommand partial-copy audit (task 4.1)

`AutoOptCommand` snapshots the molecule in its constructor (`m_moleculeCopy = *molecule`),
re-copies on `redo()`, and on `undo()` does `*m_molecule = m_moleculeCopy` over the **live**
molecule. The copy goes through `Molecule::operator=` (molecule.cpp:1738), which carries an
explicit `FIXME: Copy all the other stuff in the molecule!` and copies only atoms, bonds,
residues, conformer geometry and the unit cell. The following is dropped by the copy:

- **Partial charges** — `operator=` never copies them; `m_invalidPartialCharges` stays
  `true`, so the copy has no charges.
- **Aromaticity / perception flags** — `m_invalidAromaticity` left `true` in the copy.
- **Cubes and meshes** (electron density / solvent-accessible surfaces, `d->cubes`,
  `d->meshes` and their `QList` views) — not copied at all.
- **OpenBabel extension data** — `obmol`, `obvibdata` (vibrations), `obdosdata`,
  `obelectronictransitiondata`, `oborcaspecdata`, `oborcanearirdata`, `obconformerdata`
  are pointers left null in the copy.
- **Z-matrices, rings, energies, dipole moment** — not copied.
- **Geom caches** (`center`, `radius`, `farthestAtom`, revision) are mutable and reset.

**Dangling / side effects on undo:** the undo assignment overwrites the live molecule with
the depleted copy, so any charges, cubes, conformer metadata or perception added *after*
the AutoOpt command is destroyed, not restored. The **selection set** lives on the
GLWidget/view (not on `Molecule`) and is neither copied nor restored; **engine /
force-field caches** are likewise outside the copy. `m_molecule` still points at the live
object, so `undo()` mutates it directly.

**Disposition: DEFERRED to a follow-up change (out of scope for the push-lock race fix).**
The copy is a documented upstream FIXME and — now that the worker is quiesced before
`undo()` runs (`AutoOptCommand::undo()` → `m_tool->disable()` → `m_thread->wait()`, and the
push lock serializes all mutation) — the undo assignment **races nothing** and is not the
crash class. Fixing the copy faithfully (deep-copy charges/cubes/OpenBabel data, restore
selection + caches) is a larger, separately-scoped change; the audit above is the
follow-up brief.

## Open Questions

- None blocking. (The empty-molecule Start crash is an open *diagnosis*, tracked as a
  task here, not a design question.)
