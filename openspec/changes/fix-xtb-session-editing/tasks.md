## 1. Crash fix (D1) — press-before-move must be safe

- [x] 1.1 Guard the pin step in `XtbEngine::evaluateOnce`: skip pinning (and the accompanying accuracy switch assumptions) when `dragPos.size() < 3`; verify by building `libavogadro` and running `xtbenginetest`
- [x] 1.2 Enforce the write-side invariant at every assignment to `m_draggedIndex` (a press without an authored position never publishes an index): audit `setDraggedAtom` / `notifyDragPosition` / any tool path; verify by code inspection notes in the PR plus 1.3
- [x] 1.3 Add the engine-level crash regression to `xtbenginetest`: water session started, worker running, `setDraggedAtom(0)` with no position, request a tick, pump to completion — pre-fix this faults at the null vector read (cdb frame `evaluateOnce+0x18b`); post-fix it passes and coordinates still publish; verify with `ctest -R xtbenginetest`

## 2. Live-molecule re-baseline (D2/D3)

- [x] 2.1 Add the optional molecule source to `XtbEngine` (`setupFromMolecule(Molecule*)`, cleared on quiesce) while keeping the headless vector setup for tests; verify build + existing `xtbenginetest` green
- [x] 2.2 Implement the per-tick worker read: bounded-retry `tryLockForRead` with `std::this_thread::yield` and stop-bail, named-constructor RAII holder (MSVC rule), copy positions Å→bohr under the lock; verify by code inspection against the `AutoOptThread::update` pattern and a passing build
- [x] 2.3 Implement topology fingerprint (count + atomic numbers) with worker-side re-establishment (destroy/recreate the four xTB objects under `m_mutex`, re-validate elements, reset optimizer/counters) exactly once per topology change, routing failure to the failed-setup latch; verify with the 2.6 test
- [x] 2.4 Implement external-position adoption: epsilon diff (1e-6 bohr, dragged atom excluded) → reset L-BFGS history, clear `m_initialEvalDone`, zero eval counter, clear the converged latch; verify with the 2.6 test
- [x] 2.5 Dissolve the position side channel: delete `notifyDragPosition` and `m_dragPositionBohr`; reduce `setDraggedAtom` to index-only masking (gradient zeroing, write-back skip, interactive accuracy); update `AutoOptTool` mouse handlers and remove the D1-transition guards that no longer have a position channel to guard; verify build + `autooptreprotest` green
- [x] 2.6 Add re-baseline tests to `xtbenginetest` (molecule-source mode): add-atom mid-session → exactly one re-establishment and new atom evaluated; remove-atom → survivors keep publishing; element change → new element evaluated; external move (including after convergence) → fresh phase adopted, no snap-back; verify with `ctest -R xtbenginetest`

## 3. Failed-evaluation containment (D4)

- [x] 3.1 Add the post-singlepoint environment check and finiteness check (energy, gradient, coordinates) in `evaluateOnce`; failed ticks publish nothing and latch the phase idle-with-failure via a new `evaluationFailed(reason)` signal; verify build
- [x] 3.2 Surface the failure in the AutoOpt HUD via the existing setup-failure plumbing pattern (reason text in the on-screen status); Stop/Start and topology re-establishment clear it; verify by tool-level test
- [x] 3.3 Add the containment test: drive a failing/non-finite evaluation, assert no publish (`takeLatestCoords` returns false), latch engaged, and recovery after Stop/Start; verify with `ctest -R xtbenginetest`

## 4. Write-back + tool hardening (D5)

- [x] 4.1 Simplify the AutoOpt xTB timer path: keep `requestTick`/`takeLatestCoords`, keep the size guard as a safety net with a one-shot diagnostic (no longer silently permanent), keep the dragged-atom skip and write-lock discipline; drop non-finite snapshots defensively; verify build + `autooptreprotest` green. (Superseded by the single-writer decision: the tool no longer writes atoms, so the engine's snapshot-aware `writeBackToMolecule` is the sole molecule writer; the tool timer is a repaint pump only.)
- [x] 4.2 Add tool-level scenarios to `autooptreprotest` with synthesized events: draw-atom during xTB → write-back continues and no atom freezes; erase during xTB → survivors keep moving; undo/redo during xTB → no snap-back; verify with `ctest -R autooptreprotest`

## 5. Full verification

- [x] 5.1 Run the full affected test suite locally: `cmake --build build --parallel && ctest --test-dir build -R "xtbenginetest|xtbphysicstest|xtbruntimetest|autooptreprotest" --output-on-failure`; all green
- [x] 5.2 Run the full ctest suite; no regressions in previously passing tests
- [x] 5.3 Push the branch and trigger the Windows installer workflow (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`); verify the MSVC build compiles the new code and the offscreen ctest leg (including the new tests) is green. (Committed 57d5175e4 to master, pushed to origin/master; the Windows Installer workflow run 43432287293 auto-triggered on push and both `build-installer` jobs passed — MSVC Build + Test green, including the new xTB engine/tool tests.)
