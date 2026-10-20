## 1. OpenBabel fork: per-instance constraint state

Work on a branch in `extern/openbabel`; land on `thosoo/openbabel`. See design.md section A.

- [x] 1.1 In `include/openbabel/forcefield.h` (~566-569): remove `static` from `OBFFConstraints _constraints`, `unsigned int _fixAtom`, `unsigned int _ignoreAtom`; delete the three static definitions at `src/forcefield.cpp` ~344-347; add public `unsigned int GetFixAtom() const` / `unsigned int GetIgnoreAtom() const` next to the SetFixAtom/UnsetFixAtom declarations (~1495). Verify all three grep gates from design.md section A return exactly nothing (header statics, static-qualified references, and `src/forcefields/` unchanged reads)
- [x] 1.2 Fork stand-alone build + suite: `cmake -S extern/openbabel -B build/ob -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TESTS=ON && cmake --build build/ob --parallel && ctest --test-dir build/ob --output-on-failure` — full suite passes (energy/gradient tests `ffuff`, `ffmmff94`, `ffgaff`, `ffghemical`, `ffuff4mof` included)
- [x] 1.3 Regression test `test/ffconstraints.cpp` (register basename in `origtests` in `test/CMakeLists.txt`): two distinct singletons (UFF + MMFF94), each `Setup` from its own `OBMol`; set fix atom on A → assert B's atoms move after optimizer steps (fails on pre-change statics); reverse direction; assert B's own `SetFixAtom` still freezes B (drag behavior preserved); assert A's `GetConstraints().Clear()` leaves B untouched. Verify red→green by running it before and after 1.1
- [x] 1.4 Commit to `thosoo/openbabel`, push, confirm GitHub Actions `build_cmake.yml` green on the fork; record the fork commit hash in the change notes

## 2. Avogadro: pin bump

- [x] 2.1 Bump the `extern/openbabel` gitlink to the commit from 1.4; verify `git diff --submodule` shows only the gitlink change (no duplicated commit constants per bundled-openbabel spec)
- [x] 2.2 Clean reconfigure (old build dirs keep stale ExternalProject state — AGENTS.md) and build: `rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TESTS=ON && cmake --build build --parallel`; verify `ctest --test-dir build --output-on-failure` passes
- [x] 2.3 Verify submodule stays clean after a full configure+build: `git -C extern/openbabel status --porcelain` returns empty

## 3. Avogadro: AutoOpt lifecycle hygiene

See design.md section B for exact function bodies and line anchors.

- [x] 3.1 In `autoopttool.cpp` `enable()`: re-find the force field by the current combo selection (same lookup as `timerEvent`), then `GetConstraints().Clear()` + `UnsetFixAtom()` + `UnsetIgnoreAtom()` before `m_thread->setup(...)`; verify by contaminating state (start a draw stroke, switch tools mid-drag), starting AutoOpt — all atoms relax and are draggable, HUD shows `Fixed: 0 Ignored: 0`
- [x] 3.2 In `disable()`: add `UnsetIgnoreAtom()` + `GetConstraints().Clear()` after the existing `UnsetFixAtom()`; in `abort()`: add the unset pair (null-check first). Verify no stale marker survives stop/start: drag an atom, Stop, Start, confirm it relaxes without an app restart
- [x] 3.3 In `AutoOptThread::update()`: replace the per-tick `AddIgnore` loop with `GetConstraints().Clear()` followed by the fresh `atomicNumber() < 1` scan (comment: bounded state, self-heals the addAtom→setAtomicNumber race within one tick). Verify with a dummy atom (excluded/halo shown) that gains a real element: within one tick it moves again and the HUD returns to `0`; and that counts stay stable over 1000+ ticks (replace the runaway "Num Constraints" growth)
- [x] 3.4 Confirm the drag path is untouched: `mousePressEvent` still gates grabs on IsIgnored/IsFixed + checkboxes, `SetFixAtom` on grab, `UnsetFixAtom` on release; grabbed atom still pins and resumes after release (regression guard before touching paint)

## 4. Avogadro: draw tool stroke scoping

See design.md section C for the slot body and cleanup-list semantics.

- [x] 4.1 Add `toolActivated(Tool*)` slot to `drawtool.h`; in `DrawTool::mousePressEvent` lazily connect `widget->toolGroup()`'s existing `toolActivated(Tool*)` signal with `Qt::UniqueConnection`; slot: if incoming tool != this and a stroke is in flight, unset fix/ignore markers, restore `m_prevAtomElement`, reset all stroke state without pushing undo. Verify: start a stroke, switch tools via keyboard mid-drag, observe the anchor atom relax again under running AutoOpt
- [x] 4.2 Add the same unset pair to `~DrawTool()` when a stroke is in flight; verify by closing the app mid-stroke under a debugger/watchpoint-free manual run (no crash, no stale state if a new window opens a molecule after)
- [x] 4.3 Verify normal stroke completion still behaves as today (draw a bond while AutoOpt runs: anchor holds during the stroke, everything relaxes after release; undo of a drawn bond still works)

## 5. Avogadro: visibility

See design.md section B paint()/HUD guidance (colors, radius, CountBits).

- [x] 5.1 In `autoopttool.cpp` `paint()` while `m_running`: per-atom overlay halo (`widget->radius(atom) + 0.10`, translucent) — warm red/orange for `IsFixed(idx) || GetFixAtom()==idx`, blue for `IsIgnored(idx) || GetIgnoreAtom()==idx`; skip the currently clicked atom (existing bright halo covers it). Verify visually with an intentionally fixed atom and an ignored (dummy) atom
- [x] 5.2 Replace the "Num Constraints" HUD drawText with `Fixed: N Ignored: M` from `GetFixedBitVec().CountBits()` / `GetIgnoredBitVec().CountBits()` (+1 each when the transient marker is set); verify counts are `0 0` with no constraints, `1 0` while dragging, `0 1` with one dummy atom, and stable over time
- [x] 5.3 Confirm blocked grabs are visually distinguishable without new transient effects: a constrained (non-draggable) atom shows the persistent overlay and does not show the bright click halo (spec: grab feedback requirement)

## 6. Integration verification

- [x] 6.1 Full repro matrix (design.md section D steps 1-7): fresh-session on contaminated state, stop/start recovery, draw-while-running for several minutes, delete atoms mid-run, dummy-atom exclusion lifecycle, 20× start/stop HUD stability — no unintentional freeze anywhere, no app restart needed
- [x] 6.2 Regression pass: plain AutoOpt optimization converges (HUD energy decreases), dragging during optimization pins the grabbed atom and resumes after release, Optimize Geometry extension and constraints dialog still work, and a `ctest --test-dir build --output-on-failure` pass on the final tree
- [x] 6.3 Run `openspec validate fix-autoopt-frozen-atoms` and resolve any reported issues
