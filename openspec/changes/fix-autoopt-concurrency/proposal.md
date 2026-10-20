## Why

`fix-autoopt-frozen-atoms` made `OBFFConstraints` per-instance (correct fix for
the frozen-atom leak) but left the AutoOpt force-field instance shared between
the worker thread (which mutates it every 50 ms tick) and the GUI thread
(which reads it on every repaint, plus during teardown) with **no
synchronization**. The resulting data race — `std::vector`/`OBBitVec`
mutation during concurrent copy/read — is a use-after-free that crashes the
Windows CI installer build with an access violation when AutoOpt runs.

## What Changes

- A per-thread-safe **constraint snapshot**: the worker publishes an immutable
  copy (fixed/ignored bit-vecs, fix/ignore atoms, energy) at the end of each
  `update()` under a dedicated small mutex; `paint()` reads only that
  snapshot. The GUI thread stops calling into the live force field entirely
  (including the `Energy(false)` call in `paint()`, which races the worker's
  `Setup()` rebuild of `_interactions`).
- **Quiesced teardown**: `disable()`/`abort()` stop the worker
  (`quit()` + `wait()`) *before* mutating or clearing force-field state, so
  tool switches and molecule close while running cannot race an in-flight
  `update()`.
- **Molecule read lock in the worker**: `AutoOptThread::update()` takes the
  molecule read lock around the `OBMol()` copy + dummy-atom rescan, closing
  the concurrent read/write against GUI-thread drawing/deleting while AutoOpt
  runs.
- A **concurrency test that actually races**: the repro suite gains a test
  that drives `update()` on a real `QThread` while concurrently hammering the
  snapshot reader and teardown paths — the existing tests serialize the
  worker, which is why they cannot see this bug class.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `auto-optimize-interaction`: adds the requirement that force-field access
  is thread-confined — the GUI thread shall never call into a force-field
  instance the worker is using, cross-thread reporting goes through
  synchronization-safe snapshots, teardown quiesces the worker first, and
  worker-side molecule reads hold the molecule read lock. (No fork/
  submodule changes — this is entirely Avogadro-side.)

## Impact

- `libavogadro/src/tools/autoopttool.h/.cpp` — snapshot struct + mutex on
  `AutoOptThread`, worker-side publish, `paint()`/`disable()`/`abort()`/
  `update()` rewrites as above. No other tools touched.
- `libavogadro/tests/autooptreprotest.cpp` (or a sibling test file) — new
  racy integration test; optionally a TSAN-enabled ctest target on the Linux
  CI job.
- No OpenBabel fork changes, no submodule bump, no workflow changes.
- Relationship to other changes: supersedes file ownership of
  `autoopttool.cpp` from the (complete, unarchived) `fix-autoopt-frozen-atoms`;
  its spec scenarios are honored, their verification was concurrency-blind.
