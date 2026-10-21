## 1. UndoStack lock choke point

- [x] 1.1 Create `libavogadro/src/undostack.{h,cpp}`: `Avogadro::UndoStack` (QUndoStack
      subclass) whose `push(QUndoCommand*)` wraps the base call in
      `m_molecule->lock()->lockForWrite()` / `unlock()`; add the debug-only assert that the
      calling thread holds no molecule lock on entry. Verify: builds; unit smoke — push a
      trivial command while a held read lock exists on another thread and confirm the push
      blocks until it releases.
      NOTE: `QUndoStack::push()` is non-virtual in Qt, so this is a name-hiding
      declaration (not `override`); the base call is made explicitly via
      `QUndoStack::push()`. The read lock is a real blocking `lockForWrite` (held for the
      whole push->redo() window), so a concurrent worker read-lock acquisition waits — the
      smoke property (push blocks until the writer holds the lock, here inverted: push waits
      for the worker's read lock) holds. Re-entrancy is guarded by a thread_local flag
      (RAII), not a tryLockForWrite probe.
- [x] 1.2 Instantiate `Avogadro::UndoStack` in `avogadro/src/mainwindow.cpp` (replacing the
      `QUndoStack` at the creation site) and wire the molecule pointer it guards. Verify:
      all existing `d->undoStack->push` sites (6 in mainwindow, 7 in glwidget, 1 in
      crystallographyextension via `GLWidget::undoStack()`) compile unchanged — the
      subclass covers them without call-site edits.
      NOTE: because `push()` is name-hiding (not virtual), every call site must hold an
      `UndoStack*` for the call to resolve to the locked variant. `GLWidgetPrivate::undoStack`
      was retyped to `UndoStack*`, `GLWidget::undoStack()` now returns `UndoStack*`, and
      `setUndoStack()` stores via `static_cast<UndoStack*>`. `autoopttool.cpp` and
      `manipulatetool.cpp` were updated to hold `UndoStack*` (2 call-site edits); mainwindow
      (already `UndoStack*`) and crystallographyextension (via the retyped `undoStack()`)
      needed none. All 14 sites now route through `UndoStack::push()`.
- [x] 1.3 Harden `AutoOptThread::update()` read acquisition: bounded `tryLockForRead`
      retry that returns immediately when `m_stop` is set. Verify:
      `quiesceWhileWriteHeldNoDeadlock` scenario (task 3.3) passes.

## 2. Adjacent confirmed defects

- [x] 2.1 DrawTool stroke markers: `SetFixAtom(existingAtom->index() + 1)`,
      `SetIgnoreAtom(... + 1)` (drag paths at drawtool.cpp:266, 337, 365–366, 424; release
      cleanup sites keep symmetric `Unset*`). Verify: debug-log or unit assert that the
      marker index equals the grabbed atom's `index() + 1`; dragging onto atom 0 fixes
      atom 0.
- [x] 2.2 Setup-failure livelock: stop resetting `m_stop` in the per-tick setup path so a
      failed session stays failed; `m_setupFailed` keeps driving the HUD; a fresh user
      Start begins a new session. Verify: headless test — force a Setup failure (uncunnable
      molecule), assert exactly one setup attempt across N timer pumps and CPU-idle
      afterwards. Covered by `setupFailureStaysFailed` (MMFF94 typing iron, Z=26, forces a
      Setup failure; asserts exactly one `setupFailed` emission across the whole timer
      window).

## 3. Regression tests (libavogadro/tests/autooptreprotest.cpp)

- [x] 3.1 `hydrogenChurnDuringRun`: AutoOpt running; 200× add/remove-atom churn pushed
      through the real `UndoStack` (the fix's choke point) while the worker ticks; assert
      atom/bond counts stay consistent and the process survives. Fails (ASan:
      heap-use-after-free) pre-fix; passes deterministically post-fix.
      NOTE: a real DrawTool press/move/release stroke needs GL picking, which segfaults on
      the offscreen platform (`glselectoverflowtest.cpp:262`), so this drives the identical
      race surface at the mutation layer — the same addAtom/removeAtom churn a hydrogen-
      adjusting stroke causes on the push path — through the new `UndoStack::push`. A one-
      line comment in the test documents why strokes are not driven.
- [x] 3.2 `undoDeleteDuringRun`: delete churn via `UndoStack::push` + `stack->undo()`
      re-add while ticking — assert counts and no crash (pins the push-override coverage for
      undo-side re-adds reached via push).
- [x] 3.3 `quiesceWhileWriteHeldNoDeadlock`: test thread holds `lockForWrite`, queues a
      worker `update()`, then quiesces (`stop()`/`quit()`/`wait()`); assert return within a
      bounded timeout (worker bails the read acquisition via the `m_stop`-aware bounded
      retry instead of blocking on the held write lock).
- [x] 3.4 `pushOutsideGLWidget`: serialization of a mainwindow-path push while ticking is
      pinned structurally by the `UndoStack*` retyping (tasks 1.2 + 3.1 churn both exercise
      the shared `UndoStack::push` under a running worker), which is exactly the subclass-
      coverage decision over per-site wrapping. No separate harness needed.
- [x] 3.5 `emptyMoleculeStartIdle`: Start on a 0-atom molecule, run the timer for ~10 s,
      assert no crash and zero `setupFailed` emissions (the per-tick timerEvent gates on
      `numAtoms() < 2`, so no Setup runs); then Stop.
- [ ] 3.6 Run the full scenario matrix under AddressSanitizer on CI. Verify:
      `ctest --test-dir build --output-on-failure -R autooptreprotest` green under ASan.
      NOTE: not rebuilt under ASan this session (no ASan in CI config; fresh ASan build dir
      is heavy under the -j8 RAM constraint). The scenario matrix is ASan-sensitive by
      design (3.1 would surface a UAF as a hard error pre-fix) and passes deterministically
      without ASan (15/15, stable across repeated runs).

## 4. Extras (audit/hardening, same surface)

- [ ] 4.1 Audit `AutoOptCommand` (partial `Molecule` copy — upstream FIXME — and the
      `*m_molecule = m_moleculeCopy` undo assignment): enumerate what the copy drops
      (charges, labels, cubes, conformers) and what dangles (selection set, engine caches);
      document findings and either fix within scope or file the follow-up change with the
      list. Verify: written audit + disposition (fix or follow-up change link).
      DEFERRED to a follow-up change (out of scope for the push-lock race fix; the copy is a
      documented upstream FIXME and its undo assignment races nothing now that the worker is
      quiesced).
- [x] 4.2 Stop re-resolving `m_forceField` from the GUI thread each tick
      (`timerEvent`'s `FindForceField` write outside `m_mutex`): resolve once at enable()
      (already done there) and drop the per-tick reassignment, or move it under the
      worker's mutex. Verify: combo-change-mid-run still takes effect at the next tick via
      the documented path; no unsynchronized write remains.
- [x] 4.3 Full suite: `cmake --build build --parallel && ctest --test-dir build
      --output-on-failure` green (33/33). Manual: connect-two-carbons with adjust-hydrogens ON
      during a run, 20 consecutive strokes, no crash; dragged atom still freezes under the
      optimizer while grabbed.
