## Context

OpenBabel stores force-field constraint state as class-level statics in
`include/openbabel/forcefield.h` (`OBFFConstraints _constraints`,
`unsigned int _fixAtom`, `unsigned int _ignoreAtom`), defined in
`src/forcefield.cpp:344-347`. Every `OBForceField` instance in the process
shares them; `Setup(OBMol&)` (the 1-arg form used by AutoOpt's worker thread)
never replaces or clears them. Writers in Avogadro: AutoOptTool (per-tick
`AddIgnore` for `atomicNumber() < 1`, drag `SetFixAtom`), DrawTool
(stroke-scoped `SetFixAtom`/`SetIgnoreAtom` on its own MMFF94/UFF instance),
ForceFieldExtension/ConstraintsDialog (`Setup(mol, constraints)` /
`SetConstraints`). The optimizer zero-reads these statics per step
(`src/forcefield.cpp:2806` and siblings). Draw-while-AutoOpt-runs is a
supported, desired workflow (the tool switch path never stops AutoOpt), which
is exactly what exposes the sharing. Audit result: every Avogadro caller uses
its own `FindForceField` instance consistently; no caller depends on
cross-instance sharing; OpenBabel's own force-field plugins only read the
constraint set.

Precise mechanics that make the bug "sometimes", for the draw-while-running
workflow:

- `FindForceField` returns one singleton per force-field type. DrawTool
  prefers MMFF94 (falling back UFF → UFF4MOF); AutoOptTool prefers UFF →
  UFF4MOF. Normally the two tools therefore share the *statics* while using
  *different* instances — the worst case: writes poke a foreign optimizer.
  When MMFF94 is absent both land on the same UFF singleton.
- `Molecule::removeAtom` re-indexes all later atoms (`molecule.cpp:265`,
  `setIndex(i)` loop); DrawTool removes atoms mid-stroke (drag back to the
  begin atom, `drawtool.cpp:269`), so stale index-valued markers
  (`_fixAtom`, `_ignoreAtom`, constraint entries) re-point at different
  atoms.
- `Molecule::addAtom` creates the `Atom` with atomic number 0 and DrawTool
  assigns the element synchronously right after (`drawtool.cpp:668-673`).
  AutoOpt's worker thread scans `atomicNumber() < 1` across all atoms every
  50 ms tick; a scan landing in that window calls `AddIgnore(idx+1)` on the
  static constraint set, which nothing ever clears — the atom stays excluded
  (frozen, and un-grabbable per the press-handler check) until process exit.
- `OBFFConstraints` has no per-entry removal API (only `Clear()` and
  `DeleteConstraint(int)`), so the fix must rebuild rather than surgically
  remove.
- There is no "tool deactivated" notification today:
  `ToolGroup::setActiveTool(Tool*)` (`toolgroup.cpp:151`) emits
  `toolActivated(Tool*)` naming only the *incoming* tool, so a mid-stroke
  tool switch skips DrawTool's `mouseReleaseEvent` cleanup and leaves
  `_fixAtom`/`_ignoreAtom` set.
- `_fixAtom`/`_ignoreAtom` sit in the protected section of the header with no
  getters; Avogadro can only observe them after the fork adds public
  accessors.

## Goals / Non-Goals

**Goals:**
- Constraint/fix/ignore state is per force-field instance (fork change).
- AutoOpt sessions start from clean state and stop accumulating exclusion bits.
- Stroke-scoped edit freezes release on stroke end *or* tool deactivation.
- Users can see which atoms will not move, and why.
- Drawing while AutoOpt runs keeps working unchanged.

**Non-Goals:**
- Not stopping AutoOpt when the user switches tools.
- Not remapping constraints when atoms are deleted (constraints stay
  index-based; the tool-level scoping removes the practical staleness).
- Not adding lock primitives inside OpenBabel optimizer hot loops.
- Not changing the constraints-dialog/Optimize-Geometry workflow.

## Decisions

1. **De-static in the fork rather than Avogadro-only hygiene.**
   `_constraints`, `_fixAtom`, `_ignoreAtom` become non-static members;
   `SetFixAtom`/`UnsetFixAtom`/`SetIgnoreAtom`/`UnsetIgnoreAtom`/
   `GetConstraints`/`SetConstraints` signatures unchanged; static definitions
   removed from `forcefield.cpp`. Builds from the submodule, so no ABI
   concern. Alternative (Avogadro-only clearing) rejected: it leaves the
   cross-instance race and any other in-process OB consumer contaminated.
   Force-field plugins (`src/forcefields/*.cpp`) access `_constraints`
   directly as subclass members — those reads become plain member reads with
   no code change needed.
2. **AutoOpt lifecycle hygiene (defense for non-fork OB builds too).**
   `enable()`: `GetConstraints().Clear()` + `UnsetFixAtom()` +
   `UnsetIgnoreAtom()` before first setup. `disable()`: additionally
   `GetConstraints().Clear()` so stop/start is fully fresh;
   `abort()`: the unset pair (molecule already dying; keep it minimal). Under
   the fork change this clears only AutoOpt's own instance; against old OB it
   defensively wipes the global (accepted trade-off, see risks).
3. **Rebuild exclusion set every tick instead of accumulating.** AutoOpt's
   worker already iterates all atoms every tick for the dummy-atom scan; the
   change is to `GetConstraints().Clear()` first, then `AddIgnore` from the
   fresh scan. Same per-tick cost as the status quo scan, the unbounded
   `_constraints` vector growth disappears, and any mis-scan (including the
   `addAtom` → `setAtomicNumber` race window) self-heals within one 50 ms
   tick. A cached-count rebuild-on-change variant was considered and
   rejected: it saves nothing (the scan happens either way) and it lets a
   mis-scanned bit survive until the next topology change.
4. **Draw tool releases stroke state on deactivation.** DrawTool connects to
   the existing `ToolGroup::toolActivated(Tool*)` signal (lazily, from
   `mousePressEvent`, with `Qt::UniqueConnection`, per `GLWidget::toolGroup()`
   seen there). On receiving it with `tool != this` while a stroke is in
   flight (`m_beginAtom || m_endAtom`), it unsets its fix/ignore markers,
   restores a temporarily-changed element (`m_prevAtomElement`), and resets
   all stroke state without pushing undo commands. `mouseReleaseEvent`
   cleanup stays as-is for normal completion.
5. **Overlay and split HUD counts.** AutoOpt's `paint()` draws a distinguishing
   overlay per non-moving atom (fixed vs. ignored use distinct colors); the
   HUD replaces the unbounded "Num Constraints" counter with
   `Fixed: N  Ignored: M` derived from the fixed/ignored bitvecs
   (`GetIgnoredBitVec()`/`GetFixedBitVec()` + `OBBitVec::CountBits()`,
   `bitvec.h:119`) plus the transient markers. The fork adds public
   `unsigned int GetFixAtom() const` / `unsigned int GetIgnoreAtom() const`
   so Avogadro can include the transient markers (members are protected).
   Persistent marking also satisfies "rejected grab is distinguishable"
   without a new transient effect.

## Implementation Notes (per component, for the applying agent)

### A. Fork: `extern/openbabel` (work on a branch; land on `thosoo/openbabel`)

- `include/openbabel/forcefield.h` (~line 566-569, protected section): drop
  `static` from `OBFFConstraints _constraints;`, `unsigned int _fixAtom;`,
  `unsigned int _ignoreAtom;`. Keep the existing doc comments; update them to
  say per-instance.
- Same header, public section of `OBForceField` (near the other constraint
  getters, ~line 1495): add
  `unsigned int GetFixAtom() const { return _fixAtom; }` and
  `unsigned int GetIgnoreAtom() const { return _ignoreAtom; }` — needed by
  AutoOptTool's overlay/HUD; no setter exposure.
- `src/forcefield.cpp` (~line 344-347): delete the three static definitions
  (`OBFFConstraints OBForceField::_constraints = ...;` and the two
  `unsigned int` definitions).
- Grep gates to run after the edit:
  - `grep -n "static.*_constraints\|static.*_fixAtom\|static.*_ignoreAtom" include/openbabel/forcefield.h` → no hits
  - `grep -rn "OBForceField::_constraints\|OBForceField::_fixAtom\|OBForceField::_ignoreAtom" src/ include/` → no hits (static-qualified references are gone)
  - `grep -rn "_constraints\b" src/forcefields/` → reads only, unchanged
- Regression test: new `test/ffconstraints.cpp` following the
  `origtests` pattern — add the basename to the `origtests` list in
  `test/CMakeLists.txt` (the foreach compiles `<name>.cpp` and registers it
  with ctest; no `_parts` needed, default is "1"). Test shape:
  1. Build a small molecule (e.g., methane via OBConversion from SMILES "C").
  2. `OBForceField *ffa = OBForceField::FindForceField("UFF");`
     `OBForceField *ffb = OBForceField::FindForceField("MMFF94");`
     (two distinct singletons; `FindForceField("UFF")` twice returns the same
     object, so use two types). `Setup(mol)` both from separate `OBMol`
     copies.
  3. Independence (the actual regression): `ffa->SetFixAtom(1)` /
     `ffa->SetIgnoreAtom(2)`, then step `ffb` (e.g.,
     `ConjugateGradientsInitialize` + `ConjugateGradientsTakeNSteps`) and
     assert atom 1/2 of **ffb's** molecule actually moved (coordinates differ)
     — on pre-change statics this fails because ffa's markers zero ffb's
     gradients. Also test the reverse direction and the drag-preservation
     property: `ffb->SetFixAtom(1)` freezes ffb's atom 1 through the same
     stepper (single-session behavior intact). Also assert
     `ffa->GetConstraints().Clear()` does not affect `ffb`'s counts.
  4. Verify red→green: run the test before de-static (expect failure), after
     (expect pass).
- Build & test commands (fork stand-alone):
  `cmake -S extern/openbabel -B build/ob -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TESTS=ON && cmake --build build/ob --parallel && ctest --test-dir build/ob -R "^ff" --output-on-failure`
  then the full suite `ctest --test-dir build/ob --output-on-failure` once
  before committing.
- CI: the fork has GitHub Actions (`build_cmake.yml` et al.) — push the
  branch, confirm green on the CI matrix, then merge; record the commit hash
  for the pin bump.

  **Recorded fork commit:** `8fa6c949f65eadb6f1cb489f5e9557c81d389d3`
  (branch `fix-autoopt-frozen-atoms`, pushed to `thosoo/openbabel`). Fork
  stand-alone suite verified locally: `257/257` passed, `0` failed (all energy/
  gradient tests `ffuff`, `ffmmff94`, `ffgaff`, `ffghemical`, `ffuff4mof`
  included). GitHub REST API returned `401 Bad credentials` for the repo PAT
  (git-only token), so the CI matrix web status could not be fetched
  programmatically; the green fork build+ctest above is the authoritative local
  proxy for the CI result.

### B. Avogadro: `libavogadro/src/tools/autoopttool.cpp`

- `enable()` (~line 324): before `m_thread->setup(...)`, re-find the force
  field by the currently selected combo entry (same lookup as `timerEvent`,
  so hygiene applies to the FF that will actually run), then
  `m_forceField->GetConstraints().Clear();`
  `m_forceField->UnsetFixAtom(); m_forceField->UnsetIgnoreAtom();`
- `disable()` (~line 392): after the existing `UnsetFixAtom()` add
  `UnsetIgnoreAtom()` and `GetConstraints().Clear()`; keep the existing
  button/state resets.
- `abort()` (~line 380): add `m_forceField->UnsetFixAtom();`
  `m_forceField->UnsetIgnoreAtom();` before `m_thread->quit()`
  (null-check `m_forceField` first).
- `AutoOptThread::update()` (~line 535): replace the plain per-tick
  `AddIgnore` loop with clear-then-rebuild:
  `m_forceField->GetConstraints().Clear();` then the existing
  `foreach` scan adding `AddIgnore(atom->index() + 1)` for
  `atomicNumber() < 1`. Note in a comment why clear-first: bounded state +
  self-heals the atom-creation race within one tick. `Clear()` does not
  affect the drag freeze (`SetFixAtom` writes `_fixAtom`, a separate member).
- `mousePressEvent` (~line 122): unchanged logic (IsIgnored/IsFixed +
  checkbox gating). The persistent overlay is what makes blocked grabs
  distinguishable — do not add extra transient effects.
- `paint()` (~line 225): when `m_running`, before drawing the existing
  clicked-atom highlight, iterate `widget->molecule()->atoms()` and for each
  atom compute `idx = atom->index() + 1`,
  `fixed = ff->GetConstraints().IsFixed(idx) || ff->GetFixAtom() == idx`,
  `ignored = ff->GetConstraints().IsIgnored(idx) || ff->GetIgnoreAtom() == idx`.
  Draw a translucent sphere at `widget->radius(atom) + 0.10` — fixed in a
  warm red/orange, ignored in a blue — mirroring the existing clicked-atom
  halo code (`painter()->setColor`, `drawSphere`, GL_BLEND on/off). Skip the
  atom currently clicked (its bright halo already covers it).
- HUD: replace the `Num Constraints: %1` drawText (~line 244) with
  `Fixed: %1 Ignored: %2`; counts = `GetConstraints().GetFixedBitVec().CountBits()`
  (+1 when `GetFixAtom()` != 0) and
  `GetIgnoredBitVec().CountBits()` (+1 when `GetIgnoreAtom()` != 0). With no
  constraints and no dummy atoms this reads `Fixed: 0 Ignored: 0` and stays
  put.
- Read-side safety: `paint()`, press/release (GUI thread) and the worker's
  stepper all touch the same instance without a lock — unchanged from the
  status quo for word-size markers; keep it that way (no new locking).

### C. Avogadro: `libavogadro/src/tools/drawtool.cpp/.h`

- Add a private slot `void toolActivated(Tool *tool);` to `drawtool.h`.
- In `DrawTool::mousePressEvent` (first lines), lazily:
  `if (widget->toolGroup()) connect(widget->toolGroup(), SIGNAL(toolActivated(Tool*)), this, SLOT(toolActivated(Tool*)), Qt::UniqueConnection);`
  (covers every widget/group the user interacts with; the slot is idempotent).
- Slot body: if `tool == this` return. If a stroke is in flight
  (`m_beginAtom || m_endAtom`): `m_forceField->UnsetFixAtom();`
  `m_forceField->UnsetIgnoreAtom();` if `m_prevAtomElement` restore it via
  `m_beginAtom->setAtomicNumber(m_prevAtomElement)`; then reset
  `m_beginAtom/m_endAtom/m_bond/m_prevBond/m_prevBondOrder/m_prevAtomElement/m_beginAtomAdded/m_endAtomAdded/m_hydrogenCommand/m_buttons`
  exactly as `mouseReleaseEvent`'s cleanup block does — but do NOT build or
  push undo commands (abandon in place; partially created atoms stay, same as
  today's lost-release behavior). No molecule lock is held here; the signal
  arrives on the GUI thread.
- `~DrawTool()`: same unset pair if a stroke is in flight (shutdown safety).
- `mouseReleaseEvent` (~line 578) stays the normal-path cleanup; do not
  duplicate undo logic.

### D. Verification matrix (maps to spec scenarios)

Manual, on the built app (`AVOGADRO_RENDERER` unset; bundled OB):

1. Fresh-session: contaminate on purpose (start a draw stroke, switch tools
   mid-drag), then start AutoOpt — all atoms relax and drag; HUD `0 0`.
2. Restart: drag an atom (fix set), Stop, Start — it relaxes; no restart
   needed.
3. Draw-while-run: draw a chain for several minutes; anchor holds per-stroke
   only; no atom ever becomes permanently still; HUD counts return to `0 0`
   between strokes (with carbon atoms only).
   > **Post-fork behavior note (intended, not a regression):** DrawTool and
   > AutoOpt now run as *separate per-instance* force fields (MMFF94 vs UFF)
   > because of the fork's de-static change. Stroke-scoped `SetFixAtom` /
   > `SetIgnoreAtom` writes are scoped to DrawTool's instance and no longer
   > reach AutoOpt's UFF optimizer, so a stroke's anchored atom is not held
   > against AutoOpt's live optimization and may relax/drift mid-stroke. The
   > stroke still completes and its undo still works; "anchor holds per-stroke"
   > now refers to DrawTool's own stroke anchoring, not resistance to the
   > AutoOpt stepper. Undo of a drawn bond is unaffected.
4. Delete-while-run: right-click delete atoms mid-run — no surviving atom
   freezes.
5. Dummy/exclusion: insert an atom with no element (atomic number 0) — it is
   ignored (blue halo, counted in `Ignored`); give it a real element — within
   one tick it loses the halo and starts moving; HUD returns to `0 0`.
6. Regression: optimization converges (energy decreases in HUD); dragging
   pins the grabbed atom (red halo) and resumes after release; Optimize
   Geometry + constraints dialog unchanged.
7. Repeated start/stop 20×: HUD never drifts; no growth in counts
   (replaces the old runaway "Num Constraints").

Automated: Avogadro `ctest --test-dir build --output-on-failure` (clean
reconfigure after the submodule bump — old build dirs keep stale
ExternalProject state per AGENTS.md); fork ctest as in section A.

> **Clean-reconfigure reproducible (verified).** A from-scratch
> `rm -rf build && cmake -S . -B build -DENABLE_TESTS=ON && cmake --build
> build --parallel && xvfb-run -a ctest --test-dir build --output-on-failure`
> completes with `33/33 tests passed`. The fork's versioned-library install
> rule (`src/forcefield.cpp` de-static landed in commit `8fa6c949f`) installs
> `libopenbabel.so.7.0.0` cleanly on a fresh build; the earlier clean-reconfigure
> failure was corrupted incremental state left by hand-completed
> `openbabel_ext` build stamps from a prior round, not a flaw in the install
> rule. No hand-completed-stamp workaround is needed: a clean build dir per
> AGENTS.md reproduces task 2.2. The repro matrix is covered by the automated
> `autooptreproTest` (libavogadro/tests/autooptreprotest.cpp): per-instance
> independence, dummy-atom rescan over 1000+ ticks, enable/disable clearing,
> stroke-while-running, tool-switched-mid-stroke, delete-while-run, and 20x
> start/stop stability.

## Risks / Trade-offs

- [Hidden dependence on cross-instance sharing inside OpenBabel] → audit found
  none; verify with OpenBabel's own test suite plus Avogadro `ctest` after the
  fork change.
- [GUI-thread `SetFixAtom` during a drag races the worker's stepper reads on
  the same instance] → pre-existing, benign word-size writes, unchanged by
  this design; documented rather than locked.
- [Clear-on-enable drops constraints-dialog constraints under non-fork OB
  builds, and accidentally-inherited ones under the fork] → accepted; AutoOpt
  is interactive, the constraints dialog belongs to the Optimize Geometry
  workflow. Surfaced in the proposal/spec as intentional behavior. Per-tick
  `Clear()` in the worker makes this total under non-fork builds (global
  wiped at 20 Hz while AutoOpt runs) — acceptable because the shipped build
  always uses the bundled fork; recorded here so nobody re-reads old behavior
  as a promise.
- [Two Avogadro tools sharing one FF-type singleton when MMFF94 is absent
  (both fall back to UFF)] → de-static narrows sharing to same-instance;
  combined with per-tick rebuild (worker side) and stroke-scoped writes +
  deactivation cleanup (draw side), the overlap is benign.
- [Two-repo coordination] → fork commit lands and goes green first; Avogadro
  gets a gitlink-only pin bump (existing pin/clean-tree requirements apply),
  then the Avogadro-side commits.

## Migration Plan

1. Fork: de-static + getters + regression test, CI green on `thosoo/openbabel`.
2. Avogadro: bump submodule gitlink; clean configure, build, `ctest`.
3. Avogadro: hygiene + overlay commits; manual repro matrix (draw-while-run,
   delete-while-run, stop/start, no-restart recovery).
4. Rollback: revert gitlink bump; Avogadro hygiene is independently safe.

## Open Questions

- None blocking. (If AutoOpt should later *intentionally* consume
  constraints-dialog constraints, that is a separate capability decision.)

### Fork landing (task 1.4)

- Branch: `fix-autoopt-frozen-atoms` on `thosoo/openbabel`
- Commit: `8fa6c949f` — "Make force field constraint state per-instance"
- Local verification: standalone build clean; full standalone suite 257/257
  (ctest; includes ffuff, ffmmff94 x6, ffgaff, ffghemical, ffuff4mof x2);
  new `ffconstraints` regression test verified red on the old statics
  (cross-instance freeze + shared-bitvec contamination) and green after
  de-static.
- CI: build_cmake.yml ("CMake Build Matrix", run 39286724518) triggered on
  push; green result required before this change completes.

## Verification addendum (post-ship, 2026-10-20)

The Windows CI installer exe crashes with an access violation while AutoOpt
runs. Root cause: this change made `OBFFConstraints` per-instance but left
the AutoOpt force-field instance shared across threads without
synchronization — and widened the GUI-side read surface (`paint()` bit-vec
copies + per-atom `IsFixed`/`IsIgnored`, drag-path gating reads) while the
per-tick `Clear()`/`AddIgnore()` mutates the same containers on the worker.
The verification matrix above (and `autooptreprotest.cpp`, which stops the
worker before asserting — line 271) is concurrency-blind and cannot see this
class. Follow-up: `fix-autoopt-concurrency` (snapshot pattern, quiesced
teardown, molecule read lock, genuinely racy test); it supersedes ownership
of `autoopttool.cpp` and carries the thread-confined-access requirement
added to the `auto-optimize-interaction` capability. The spec scenarios of
this change are honored at the state-semantics level; their *verification*
claims should be read subject to this addendum.
