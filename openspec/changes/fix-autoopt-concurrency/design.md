## Context

Crash mechanism (confirmed by code read; Windows CI installer exe faults when
AutoOpt runs):

- Worker thread, `AutoOptThread::update()` (`autoopttool.cpp` ~585-660, every
  50 ms tick, under `m_mutex`): `GetConstraints().Clear()` — which in the
  fork clears `std::vector<OBFFConstraint> _constraints` and five `OBBitVec`s
  (`extern/openbabel/src/forcefield.cpp`, `OBFFConstraints::Clear()`) — then
  `AddIgnore(idx)` per dummy atom (bit sets on `_ignored`), then
  `Setup(mol)` + optimizer steps (rebuild `_interactions` and friends).
- GUI thread, `AutoOptTool::paint()` (`autoopttool.cpp` ~242-296, every
  repaint — at least once per tick because `finished()` calls
  `m_glwidget->update()`, never under `m_mutex`):
  `m_forceField->Energy(false)` (iterates `_interactions` — races the
  worker's `Setup()` rebuild), `GetFixedBitVec()`/`GetIgnoredBitVec()`
  (**return `OBBitVec` by value** = deep copy of `std::vector<uint32_t>
  _set`), `CountBits()`, `GetFixAtom()`/`GetIgnoreAtom()`, and per-atom
  `IsFixed(idx)`/`IsIgnored(idx)` (`BitIsSet` — bounds math + array read).
- GUI thread teardown, `abort()` (~440) / `disable()` (~452):
  `UnsetIgnoreAtom()` + `GetConstraints().Clear()` while a queued or running
  `update()` may still be in flight — `m_thread->quit()` does not interrupt
  an already-running slot.
- GUI thread drag path, `mousePressEvent` (~150-175) / `mouseReleaseEvent`
  (~180-190), while `m_running`: `GetConstraints().IsIgnored(idx)` /
  `IsFixed(idx)` gating reads (**container reads** — `BitIsSet` against the
  `_ignored`/`_fixed` word vectors, same UAF class as the paint reads) and
  `SetFixAtom(idx)` / `UnsetFixAtom()`. The latter two are plain
  `unsigned int` stores on `OBForceField::_fixAtom`
  (`forcefield.cpp:368-375`) — memory-safe when racy, but still live-FF
  calls, so this change's own spec requirement ("only the worker SHALL call
  into the live force-field instance") must cover them too, not just
  `paint()`.
- Worker `OBMol mol = m_molecule->OBMol()` plus the dummy rescan
  (`m_molecule->atoms()`): **no molecule lock**, racing GUI-thread
  `addAtom`/`removeAtom`/`setPos` during draw strokes (writers use
  `tryLockForWrite`).

Three distinct UAF vectors, one root cause (unsynchronized cross-thread
force-field access): (1) constraint bit-vecs/`_constraints` mutation vs
by-value copies + `BitIsSet` reads; (2) `_interactions` rebuild in `Setup()`
vs `Energy(false)` in paint; (3) live-molecule lists vs `OBMol()` copy.

Why it shipped: `m_block` serializes `update()`↔`finished()` but *not*
`update()`↔`paint()`, so the overlap is continuous during a run; and the
repro tests (`autooptreprotest.cpp`) call `thread.update()` synchronously on
the test thread — line 271 even documents *"stops the worker so no background
update() races the assertions"* — making the suite structurally blind to this
bug class. MSVC release + Windows heap faults on UAF; glibc/Linux dev builds
tend to absorb it, which is why local testing and 32/32 ctest passed.

## Goals / Non-Goals

**Goals:**
- GUI thread never calls into the live force field while running; all
  reporting flows through an immutable worker-published snapshot.
- Teardown quiesces the worker before touching shared state.
- Worker molecule reads hold the molecule read lock.
- A test that reproduces the race class by construction (real threads, real
  concurrency).

**Non-Goals:**
- No OpenBabel fork changes, no submodule bump (the per-instance constraint
  fix from `fix-autoopt-frozen-atoms` stands).
- Not redesigning the optimizer loop, tick rate, or `AutoOptCommand` undo
  path.
- Not adding locks inside OpenBabel types (`OBFFConstraints` stays a
  single-threaded-by-convention type; confinement is Avogadro's job).
- Not fixing the duplicated `destroyed()` connections beyond the optional
  `Qt::UniqueConnection` hygiene task.

## Decisions

1. **Snapshot struct + dedicated small mutex (the core fix).**
   In `AutoOptThread` (`autoopttool.h` ~56-88, next to `m_mutex` at line 87):
   ```cpp
   struct ConstraintSnapshot {
     OpenBabel::OBBitVec fixed, ignored;
     unsigned int fixAtom = 0, ignoreAtom = 0;
     double energyKJ = 0.0;   // already unit-converted
     bool valid = false;
   };
   QMutex m_snapshotMutex;
   ConstraintSnapshot m_snapshot;
   // thread-safe accessor:
   ConstraintSnapshot snapshot() const {
     QMutexLocker locker(&m_snapshotMutex);
     return m_snapshot;
   }
   ```
   - Worker side, end of `update()` **under `m_mutex`** (after the stepper
     switch, before `m_mutex.unlock()`): build a local `ConstraintSnapshot`
     — copies via the existing by-value getters, `GetFixAtom()`,
     `GetIgnoreAtom()`, energy via `m_forceField->Energy(false)` with the
     same `KCAL_TO_KJ` conversion paint does today — then take
     `m_snapshotMutex` just long enough to store it. On the `Setup` failure
     path, publish `{valid = false}` so the HUD degrades to hiding instead
     of showing stale numbers.
   - Consumer side, `paint()` (~242-296): take a local copy once per frame
     via `snapshot()`; use it for the energy text, `dE` vs `m_lastEnergy`,
     the "Fixed: N Ignored: M" line, and the per-atom halo loop's
     fixed/ignored tests (use the snapshot's bit-vecs + fix/ignore atoms
     instead of `GetConstraints()`). **Delete every live-FF call from
     `paint()`**: `Energy(false)`, `GetConstraints()`, `GetFixAtom()`,
     `GetIgnoreAtom()`, and the `m_forceField` null-check guarding that
     block becomes a snapshot `valid` check.
   - Mutex hold times are microseconds (struct copy; `OBBitVec` copies are
     small word vectors), so 60 fps paints never wait on optimizer work.
   - *Alternatives rejected:* `paint()` taking `m_mutex` — blocks a frame up
     to a full tick (10-50 ms+), visible hitches; queued-signal updates of
     GUI members — fine for the HUD line but the halo loop needs per-atom
     bit tests every frame anyway, so a snapshot it must be.
2. **Quiesced teardown ordering in `disable()`/`abort()`.**
   New order in both: `m_running = false; killTimer(...); m_thread->stop();`
   `m_thread->quit();` `if (m_thread->isRunning()) m_thread->wait();` — and
   only then `UnsetFixAtom()/UnsetIgnoreAtom()/GetConstraints().Clear()`.
   `run()` is `moveToThread(this) + exec()`, so `quit()` defers until the
   in-flight `update()` slot returns, and `wait()` therefore bounds GUI
   blocking to one tick's work — acceptable on teardown paths. Harden
   `update()` with an `if (m_stop) return;` early-out at the top so a stop
   request skips a queued tick's whole cycle. Note `abort()` is invoked
   from `Molecule::destroyed` — with `wait()` ordering, the worker can no
   longer be dereferencing `m_molecule` when the GUI thread proceeds.
   *Alternative rejected:* asynchronous "clear on worker" via queued call —
   more states to reason about for no benefit at teardown frequency.
3. **Molecule read lock around worker-side molecule reads.**
   In `update()`: null-check `m_molecule` first (currently an unguarded
   deref), then `m_molecule->lock()->lockForRead()` around **both** the
   `OBMol()` copy and the dummy-atom rescan; release **before** `Setup()`
   (the optimizer works on the private copy only). Hold time ~1-2 ms per
   tick. Writers (`DrawTool` events) use `tryLockForWrite`, so they delay at
   most one lock hold; `renderModern`'s `tryLockForRead` coexists
   (reader-reader). The lifetime argument: `m_molecule` can only dangle
   after `destroyed()` → `abort()`, and decision 2 now guarantees the worker
   is parked before the GUI thread returns from `abort()`.
4. **A test that actually races.**
   Extend `libavogadro/tests/autooptreprotest.cpp` (or sibling
   `autooptconcurrencytest`): real `QThread` (`thread.start()`,
   `setup(mol, ff, ...)`), then for N iterations queue `update()` via
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` while the test
   loop hammers `thread.snapshot()` at high rate and, interleaved, exercises
   `disable()`/`enable()` and a molecule mutation (add/remove atom) — the
   three race pairs from Context. Assertions: no crash (implicit), snapshot
   invariants (e.g. `ignored` bits ⊆ atom count at publish time), and
   teardown completes without deadlock. Document: this test is the
   regression net for the class the current suite cannot see; optionally add
   a TSAN-enabled ctest target on the Linux CI job (`-fsanitize=thread`) and
   run the existing autoopt suite under it once.
5. **Drag-path confinement (mousePress/mouseRelease).**
   Two kinds of GUI-thread force-field touch happen during an active drag:
   - *Gating reads* (`IsIgnored`/`IsFixed` on the clicked atom, ~160-167):
     replace with the snapshot (`m_thread->snapshot()`, taken once per event
     like paint does). Semantics unchanged: the snapshot is ≤ 1 tick stale,
     which matches the drag's existing one-tick lag.
   - *Marker writes* (`SetFixAtom` on press at ~170, `UnsetFixAtom` on
     release at ~186): route through a worker-applied pending value instead
     of calling the FF directly. `AutoOptThread` gains
     `void setPendingFixAtom(unsigned idx)` (stores under `m_mutex`) and a
     `kNoPendingFix = UINT_MAX` sentinel member; `update()`, under
     `m_mutex` right after the null/stop checks, compares-and-applies:
     pending != sentinel → `SetFixAtom(pending)` (or `UnsetFixAtom()` for
     0), then resets to sentinel. One ≤ 50 ms latency on marker effect —
     imperceptible, and it makes the worker the only FF caller, as the spec
     requires.
   - *Alternatives rejected:* leaving `SetFixAtom` as a documented-benign
     int store — formally racy and a standing exception to the new
     requirement the next auditor will trip over; std::atomic in the fork —
     non-goal (no fork changes in this change).
6. **`finished()`'s `GetCoordinates(mol)` stays (documented safe).**
   `m_block` + the queued `finished` delivery strictly serialize
   `update()`↔`finished()`: the next `update()` cannot be queued until
   `finished()` sets `m_block = false`, and it is queued only after. Add a
   comment there; do not restructure.

## Risks / Trade-offs

- [`wait()` on the GUI thread during teardown blocks up to one tick] →
  bounded by the steps spinner; typical ticks are single-digit ms; only on
  disable/abort, never per-frame.
- [Snapshot adds one more shared mutable member — a future bug magnet] →
  keep ALL snapshot access behind the `snapshot()` accessor; add a comment
  forbidding direct `m_snapshot` reads outside the mutex.
- [Energy semantics change subtly: paint now shows last-tick energy instead
  of a freshly computed one] → imperceptible at 20 Hz ticks and *more*
  correct (matches what the optimizer actually minimized); verify HUD text
  looks unchanged.
- [The racy test may be flaky under loaded CI] → assertions are
  invariant-based, not timing-based; no sleeps; if a TSAN job proves noisy,
  keep the functional racy test and run TSAN on a schedule, not per-PR.
- [File ownership overlaps the unarchived `fix-autoopt-frozen-atoms`] → that
  change is complete; land this after archiving it (or rebase trivially —
  this change rewrites the same regions).
- [New worker-side molecule read lock (~1-2 ms at 20 Hz) marginally raises
  `renderModern()`'s `tryLockForRead()` failure probability while a writer
  is waiting] → harmless post-
  `fix-modern-tier-frame-presentation` (its task 2.2 presents the retained
  frame on lock failure); on trees WITHOUT that change, expect a possible
  rare blank frame during draw strokes — land frame-presentation first if
  both are in flight. No artifact conflict either way (disjoint files).

## Verification (maps to spec scenarios)

1. Idle-running soak (spec: Idle-running HUD does not crash): built app,
   Start AutoOpt on a small molecule, hands off keyboard/mouse, ≥ 5 min
   (600+ ticks × repaints) — no crash. This is the reported Windows crash;
   verify on the Windows CI-built exe specifically.
2. Repaint during tick (spec: Repaint during an in-flight optimization
   tick): resize/drag the window continuously during a run — HUD updates
   smoothly, no stalls from optimizer work (snapshot mutex hold-time check).
3. Teardown mid-tick (spec: Tool switch or document close while running):
   script or rapid manual: Start → immediately switch tool / close document,
   20× — no crash, no deadlock, clean re-Start afterwards.
4. Draw/delete during run (spec: Drawing or deleting during a run): stroke
   bonds and delete atoms while running — no crash, no ghosting beyond the
   documented one-tick lag.
5. Automated: full `ctest --test-dir build --output-on-failure`; the new
   racy test passes; optionally TSAN run of the autoopt suite clean.

## Migration Plan

Single commit on top of (and after archiving) `fix-autoopt-frozen-atoms`;
no pin, config, or workflow changes. Rollback = revert.

## Open Questions

- None blocking. (If the Windows crash dump surfaces a faulting module
  *other* than the force-field/OBBitVec paths — e.g. a Qt-internal or GL
  frame — revisit before implementing; the mechanism here is the
  highest-probability candidate, not yet dump-confirmed.)
