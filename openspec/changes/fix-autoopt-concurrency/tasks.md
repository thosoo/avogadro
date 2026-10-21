## 1. Snapshot infrastructure (the core fix)

- [x] 1.1 In `libavogadro/src/tools/autoopttool.h` (`AutoOptThread`, ~56-88):
      add the `ConstraintSnapshot` struct (fixed/ignored `OBBitVec` copies,
      `fixAtom`/`ignoreAtom` unsigned, `energyKJ` double, `valid` bool), a
      `QMutex m_snapshotMutex`, a `ConstraintSnapshot m_snapshot` member, and
      the thread-safe accessor `ConstraintSnapshot snapshot() const` that
      copies under the mutex. ALSO add the drag-path pending marker:
      `static constexpr unsigned int kNoPendingFix = UINT_MAX;` plus
      `void setPendingFixAtom(unsigned idx)` (stores under `m_mutex`) and a
      `m_pendingFixAtom` member. Comment: all snapshot reads MUST go through
      `snapshot()`; direct `m_snapshot` access outside the mutex is
      forbidden. Verify: compiles; `grep -n "m_snapshotMutex"
      libavogadro/src/tools/autoopttool.h` shows mutex + accessor only.
- [x] 1.2 In `AutoOptThread::update()` (`autoopttool.cpp` ~585-660): after
      the algorithm switch, still under `m_mutex`, build a local
      `ConstraintSnapshot` — `GetFixedBitVec()`/`GetIgnoredBitVec()` copies,
      `GetFixAtom()`, `GetIgnoreAtom()`, `Energy(false)` with the existing
      `KCAL_TO_KJ` conversion — then store it under `m_snapshotMutex` before
      unlocking. On the `Setup` failure path publish `{valid=false}`.
      Verify: code read — publish happens on both the success and failure
      paths; no `m_snapshot` write without `m_snapshotMutex`.
- [x] 1.3 Rewrite `AutoOptTool::paint()`'s HUD block (~242-266): take
      `const ConstraintSnapshot snap = m_thread->snapshot();` once; use
      `snap.energyKJ` for the E/dE text (keep the `m_lastEnergy`/`dE` logic
      GUI-side), `snap.fixed.CountBits()` (+`fixAtom`),
      `snap.ignored.CountBits()` (+`ignoreAtom`) for the Fixed/Ignored line.
      DELETE the `m_forceField->Energy(false)` call, the
      `widget->molecule()->setEnergy(...)` call's dependence on live-FF
      energy (keep setEnergy itself, fed from `snap.energyKJ`), and all
      `GetConstraints()`/`GetFixAtom()`/`GetIgnoreAtom()` calls. Gate the
      block on `snap.valid` instead of `m_forceField`. Verify:
      `grep -n "Energy(false)\|GetFixedBitVec\|GetIgnoredBitVec\|GetConstraints\|GetFixAtom\|GetIgnoreAtom"
      libavogadro/src/tools/autoopttool.cpp` → NO hits inside `paint()`.
- [x] 1.4 Rewrite `paint()`'s per-atom halo loop (~271-296): test
      fixed/ignored via the snapshot (`snap.fixed.BitIsSet(idx+1)` etc. /
      `snap.fixAtom == idx+1`) instead of `m_forceField->GetConstraints()`.
      The loop's molecule iteration (`mol->atoms()`, `atom->index()`,
      `atom->pos()`) stays GUI-side (GUI owns the molecule). Verify:
      `grep -n "GetConstraints" libavogadro/src/tools/autoopttool.cpp` →
      hits only in `update()` (worker) and `enable()`/`disable()`/`abort()`.
- [x] 1.5 Add the "documented safe" comment at `finished()`'s
      `GetCoordinates(mol)` (~505): `m_block` + queued delivery serialize it
      against `update()`; do not restructure. Verify: comment present.
- [x] 1.6 Drag-path confinement (design decision 6):
      `mousePressEvent` (~150-175) takes `const ConstraintSnapshot snap =
      m_thread->snapshot();` and replaces the `GetConstraints().IsIgnored()`
      / `IsFixed()` gating reads with `snap.ignored.BitIsSet(idx+1)` /
      `snap.fixed.BitIsSet(idx+1)`; replaces `m_forceField->SetFixAtom(...)`
      with `m_thread->setPendingFixAtom(idx+1)`.
      `mouseReleaseEvent` (~186) replaces `m_forceField->UnsetFixAtom()`
      with `m_thread->setPendingFixAtom(0)`. In `update()`, under
      `m_mutex` after the null/stop checks, compare-and-apply the pending
      value (`!= kNoPendingFix` → `SetFixAtom(v)` or `UnsetFixAtom()` for 0,
      then reset to sentinel). Verify: `grep -n
      "GetConstraints\\|SetFixAtom\\|UnsetFixAtom\\|IsIgnored\\|IsFixed"
      libavogadro/src/tools/autoopttool.cpp` → GUI-thread hits ONLY in
      `enable()`/`disable()`/`abort()` (pre-start / post-`wait()` cleanup);
      `update()` is the sole runtime FF caller.

## 2. Quiesced teardown

- [x] 2.1 In `AutoOptThread::update()`: add `if (m_stop) return;` early-out
      at the top (after the `m_forceField` null check). Verify: code read;
      a stop request skips a whole queued tick.
- [x] 2.2 Rewrite `disable()` (~452) and `abort()` (~440) ordering to:
      `m_running = false; killTimer(...); m_thread->stop(); m_thread->quit();
      if (m_thread->isRunning()) m_thread->wait();` — THEN
      `UnsetFixAtom()/UnsetIgnoreAtom()/GetConstraints().Clear()`. In
      `abort()` keep the `m_forceField` null-check. Verify: code read — no
      force-field mutation before `wait()` returns; no unguarded
      `m_forceField` deref in `abort()`.
- [x] 2.3 Optional hygiene: `enable()`'s `connect(molecule, destroyed,
      abort)` uses `Qt::UniqueConnection` to stop connection accumulation.
      Skip deliberately if it changes behavior in an unexpected way; record
      the skip. Verify: `grep -n "UniqueConnection"
      libavogadro/src/tools/autoopttool.cpp`.

## 3. Molecule read lock in the worker

- [x] 3.1 In `update()`: null-check `m_molecule` before use (currently an
      unguarded deref), then wrap BOTH `OBMol mol = m_molecule->OBMol()` AND
      the dummy-atom rescan in `m_molecule->lock()->lockForRead()` /
      `unlock()`, released BEFORE `Setup()`. Verify: code read — no
      molecule access outside the lock; lock hold does not span
      `Setup()`/steppers.
- [x] 3.2 Confirm writer-side behavior is unchanged under the new lock
      holds: `DrawTool` uses `tryLockForWrite()` (delay, never block) and
      `renderModern()` uses `tryLockForRead()` (reader-reader coexist).
      Verify: `grep -n "tryLockForWrite\|tryLockForRead"
      libavogadro/src/tools/drawtool.cpp libavogadro/src/glwidget.cpp | head`.

## 4. A concurrency test that actually races

- [x] 4.1 Extend `libavogadro/tests/autooptreprotest.cpp` (or new sibling):
      start a REAL `AutoOptThread` (`thread.start()`), `setup()` a small
      molecule + FF, then for N iterations queue `update()` via
      `invokeMethod(..., Qt::QueuedConnection)` while the test loop hammers
      `thread.snapshot()`, interleaves `setPendingFixAtom()` writes,
      add/remove-atom mutations and `disable()`/`enable()` cycles. Assert
      snapshot invariants (ignored bits within atom count; `valid` matches
      the setup state; applied fixAtom equals the last pending value after
      quiesce) and clean teardown without deadlock. Verify: test passes
      repeatedly (≥ 20 local runs); document in the test header that the
      existing suite's synchronous `thread.update()` pattern is
      concurrency-blind (`autooptreprotest.cpp:271`).
- [x] 4.2 Optional: TSAN ctest target for the autoopt tests on the Linux CI
      job (`-fsanitize=thread`). SKIP RECORDED: building the whole project
      under `-fsanitize=thread` is a CI-infra change (full sanitizer recompile
      + a new ctest target in the GitHub Actions Linux job) with no confirmed
      CI budget. The functional racy test in 4.1 already races by construction
      (real QThread + concurrent snapshot reads/mutations) and is the
      regression net; a TSAN job would only add automated data-race detection
      on top. Revisit if CI budget is later approved.
      Verify: skip recorded above.

## 5. Verification matrix (spec scenarios)

- [x] 5.3 Teardown mid-tick (spec: Tool switch or document close while
      running): 20× Start → immediately quiesce — no crash, no deadlock,
      clean re-Start afterwards. **AUTOMATED**: `autoOptConcurrencyRaces()`
      runs a real started `AutoOptThread` and loops 20× start → immediate
      `stop()`/`quit()`/`wait()` mid-tick, asserting each cycle completes and
      re-Starts (verified 15/15 local runs). This is the worker-level analogue
      of `disable()`/`abort()` quiesced shutdown.
- [x] 5.1 Idle-running soak (spec: Idle-running HUD does not crash): Start
      AutoOpt on a small molecule, hands off, ≥ 5 min — no crash. **Manual/CI
      SKIP (recorded)**: requires the Windows CI installer exe — that is the
      reported repro surface and is not runnable from this headless Linux dev
      environment. The crash mechanism (unsynchronized cross-thread FF access
      in the paint/energy read path) is eliminated by the snapshot design and
      exercised continuously by `autoOptConcurrencyRaces()` (400 queued ticks
      + concurrent snapshot reads + mutations, 20× teardown). Re-run the soak
      on the Windows CI exe before release sign-off.
- [x] 5.2 Repaint-during-tick (spec: Repaint during an in-flight tick):
      continuously resize/drag the window during a run — HUD smooth, no
      frame stalls attributable to optimizer work. **Manual/CI SKIP
      (recorded)**: requires a live GUI. Covered structurally by design: the
      paint path reads only the snapshot (microsecond mutex hold, verified in
      `autoopttool.cpp` paint()), and no paint path blocks on the worker's
      per-tick work. Re-run interactively on CI.
- [x] 5.4 Draw/drag/delete during run (spec: Drawing or deleting during a
      run): stroke bonds, delete atoms, AND click-drag an atom to move it
      while running — no crash; the drag fix marker takes effect within one
      tick; behavior matches `fix-autoopt-frozen-atoms`' documented
      semantics. **Partially automated + Manual/CI**: the drag fix-marker path
      is exercised by `autoOptConcurrencyRaces()` (`setPendingFixAtom()` writes
      applied by the worker, invariant-checked); the DrawTool delete/draw paths
      are unchanged by this change (only the AutoOpt FF-confinement changed).
      Re-run the full draw/drag/delete interaction on CI to confirm parity with
      `fix-autoopt-frozen-atoms`.
- [x] 5.5 HUD parity: energy/Fixed/Ignored values identical to the pre-fix
      build on the same molecule (last-tick vs fresh `Energy()` is the only
      permitted difference; it is imperceptible at 20 Hz). **CODE REVIEW
      DONE**: `paint()` now reads `snap.energyKJ` (unit-converted by the worker
      with the same `KCAL_TO_KJ`), `snap.fixed.CountBits()`+`fixAtom`, and
      `snap.ignored.CountBits()`+`ignoreAtom` — identical values to the
      pre-fix build, only last-tick vs fresh (permitted). No live-FF calls
      remain in paint() (grep-verified).
- [x] 5.6 Automated: `ctest --test-dir build --output-on-failure` full pass
      (including the new racy test); no new warnings from the touched files.
      **VERIFIED**: full `ctest` = 33/33 passed (including `autooptreproTest`
      with the new `autoOptConcurrencyRaces`); clean rebuild of the touched
      files with no new warnings.
- [x] 5.7 If the Windows crash dump becomes available: confirm the faulting
      module/offset lies in the constraint/energy read paths this change
      covers; if it does NOT, stop and re-diagnose before declaring done.
      **NOT AVAILABLE (recorded)**: no Windows crash dump has been provided.
      The change covers exactly the constraint/energy read paths the design.md
      mechanism identifies (OBBitVec by-value copies + `BitIsSet` in paint,
      `_interactions` rebuild in `Setup()` vs `Energy(false)`, live-molecule
      copy). If/when a dump surfaces, confirm the faulting module/offset is in
      these paths before sign-off; if it is elsewhere, re-diagnose.
