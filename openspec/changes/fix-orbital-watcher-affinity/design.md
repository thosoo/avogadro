# Design: fix-orbital-watcher-affinity

## Context

`fix-orbital-visualization` commit `8cb5b8f24` (D3, "responsive orbital load")
moved the basis parse off the GUI thread:

```cpp
// orbitalextension.cpp loadBasis() — worker parses, continuation resumes GUI
m_basisLoadFuture = QtConcurrent::run([this, fileName, format, generation]() {
  OpenQube::BasisSet *basis = parseBasisFile(fileName, format);   // ← constructs
  QMetaObject::invokeMethod(this, [this, generation, basis] { ... }, Qt::QueuedConnection);
});
```

`parseBasisFile` constructs the OpenQube `BasisSet` subclass inside the worker
(`new GaussianSet` / `new SlaterSet` + reader constructor). Every OpenQube
basis class owns a **parentless `QFutureWatcher<void> m_watcher` member**
(`gaussianset.h:232`, `slaterset.h:161`). A QObject's thread affinity is fixed
at construction and `moveToThread` only relocates *child* objects — so the
watcher permanently belongs to the `QtConcurrent` pool thread, which has no
event loop. QFutureWatcher delivers its callouts by posting events to the
watcher's own thread; those events are never retrieved.

Consequences, all reproduced headlessly (probe + real app under Xvfb,
`testfiles/koffein_orca.out`, 2026-11-04):

- The cube future completes and the OpenQube cube is filled
  (`isFinished() == true`, finite values) — but `finished` is never delivered.
- `OrbitalExtension::calculateCubeDone` never runs → no mesh, no render.
- `GaussianSet::calculationComplete` (same watcher) never runs → the cube
  write-lock taken in `startCubeCalculationAsync` is never released, and
  `OrbitalExtension::m_runningMutex` (locked in `checkQueue`) is held until a
  document switch. Every later click silently no-ops at `tryLock()`.
- The GUI never blocks: the failure is fully silent.
- `qube` (CLI, synchronous `waitForFinished`) and `cubeevaltest`
  (GUI-thread-constructed synthetic basis) both pass — which is why the 44-test
  suite and the CLI smoke checks did not catch this. The watcher-affinity bug
  is only reachable when the object is constructed off-thread.

Probe matrix (all in /tmp against the built `OpenQube`):

| Variant | Result |
|---|---|
| Sync GUI-thread parse (pre-8cb5b8f24 shape) | ✅ completes |
| Async parse, extension's exact flow | ❌ hang reproduced |
| Async parse + `basis->moveToThread(gui)` from worker | ❌ still hangs (watcher not a child, not moved) |
| Async parse + `gs->watcher().moveToThread(gui)` from worker | ✅ completes |
| Async parse, construct `GaussianSet` on GUI, `ORCAOutput(file, gs)` fill on worker | ✅ completes |

`SurfaceExtension` still calls `LoadBasisSet` synchronously on the GUI thread
(`surfaceextension.cpp:266`) — its watcher is GUI-affine and MO/density
surfaces work today; it is the mirror image and the remaining GUI-side parse
freeze.

## Goals / Non-Goals

**Goals:**
- Restore end-to-end orbital display (parse → cube → mesh → render) for every
  format OrbitalExtension accepts, while keeping the parse off the GUI thread.
- Make the watcher-affinity invariant explicit where the async entry points
  are defined, so the next threading change cannot silently regress it.
- Headless regression coverage that fails on exactly the broken shape.

**Non-Goals:**
- Rewriting the OpenQube async machinery (slab evaluation, cube-copy worker,
  epoch guards) — all verified working; only construction affinity changes.
- Asynchronousizing `SurfaceExtension::loadBasis` (a separate, future change;
  see Risks for the trap note).
- A generalized "QObject affinity auditor" tooling or static analysis.
- AutoOpt, DrawTool, rendering tiers, OpenBabel pin.

## Decisions

### D1: Construct on the GUI thread, fill on the worker (fix shape A)

`parseBasisFile` is split: the GUI thread (in `loadBasis`, before dispatch)
constructs the correct `BasisSet` subclass by format tag/extension; the worker
lambda then only constructs the stack reader (`ORCAOutput orcaout(fileName,
gs)` etc.) that fills it. Every reader already takes `(const QString&, BasisSet*)`
and does no member access on the extension, so the split is mechanical and the
worker still touches no extension state. The epoch/generation guard and the
`invokeMethod` continuation are unchanged.

Validated by probe: pipeline completes with the parse on the worker.

**Alternative considered — shape B (`gs->watcher().moveToThread(gui)` in the
worker)**: also validated, smaller diff, but it encodes the invariant as an
imperative line that must be repeated for every future basis class and is easy
to omit (exactly how this class of bug is born). Shape A makes the invariant
structural: an object whose watcher is consumed on the GUI thread is never
born anywhere else. Shape B is kept as the documented fallback if a reader
turns out to require worker-side construction.

### D2: Document the invariant at the async entry points

The class-level comment on `BasisSet`/`GaussianSet`/`SlaterSet` async entry
points gains a short contract note: *objects of this class whose async
completion signals are consumed on the GUI thread must be constructed on (or
have their `QFutureWatcher` re-homed to) the GUI thread; QFutureWatcher callouts
post to the watcher's own thread.* No code change in `gaussianset`/`slaterset`
beyond comments.

### D3: Regression test drives the real async path in `orbitalselectiontest`

Two layers, both in the existing `orbitalselectiontest` binary (already links
`OpenQube` + `Qt6::Concurrent`, already compiles `orbitalextension.cpp`, with
`TESTDATADIR` available):

1. **Cheap invariant assert**: after the async load continuation runs for
   `testfiles/koffein_orca.out`, the basis object's `thread()` equals the GUI
   thread at dispatch time. This alone would have caught the regression.
2. **End-to-end**: run the real `loadBasis()` on the test's event loop against
   `koffein_orca.out` (and, cheaply, `benzene.fchk`), wait with a bounded
   timeout (QtTest `QTRY_*` over the event loop), and assert the queue reaches
   a completed calculation (progress final state) — the observable the spec
   scenario names. A timeout asserts failure, mirroring the manual wedge.

The existing selection-semantics hooks (`AVOGADRO_TEST_ORBITAL_SELECTION`) are
reused rather than bypassed: the new test exercises the un-hooked parse path
that the 44-test suite never touched.

### D4: Honest re-verification replaces the stale manual-matrix record

`fix-orbital-visualization` task 7.3 rows ("when it completes, it renders
automatically") are unobservable on the shipped build; this change's tasks
re-run the full matrix on the final build and record per-row results in the
session log, so the archive record reflects reality. The reopen-vs-new-change
decision was made in exploration: this fix ships as its own change while
`fix-orbital-visualization` remains complete-but-unarchived; its archive gate
should be re-checked against this change's evidence.

### D5: MOPAC aux coverage follows the shared path, not a fixture

`testfiles/` has no `.aux` fixture, so no end-to-end MOPAC scenario is
claimable. `MopacAux`/`SlaterSet` go through the same split
(`parseBasisFile` aux branch), and D2's comment covers the class; the shared
path is exercised by the fchk/molden/ORCA tests. If an `.aux` fixture is added
later, the spec scenario set can grow without a spec change (the requirement
already generalizes over "orbital information" files).

## Risks / Trade-offs

- [Fix reverts to a GUI freeze] → The parse stays on the worker; only
  construction moves (O(basis) allocation, microseconds). D3's end-to-end test
  runs with the real `QtConcurrent` dispatch, and the existing responsiveness
  tests still guard the large-file path.
- [Reader constructors assume worker context] → Audited: all six readers only
  touch their file argument and the basis they fill; none uses
  thread-sensitive globals. The fchk/molden end-to-end tests exercise two of
  the six on the new split.
- [Silent regression recurs on a future basis class] → D2's invariant comment
  at the class + async entry points; D3's affinity assert is cheap to extend.
- [Fixing this masks the SurfaceExtension trap] → Its sync parse still freezes
  the GUI for large bases when the Surfaces dialog is used. Deliberately out of
  scope, but D2's invariant note explicitly warns that asyncifying it requires
  the same construction rule.
- [Queue-wedge residue from the bug (locked cube, held mutex)] → Only reachable
  transiently in a broken build; document-switch teardown already unlocks and
  deletes safely (`MeshGenerator` uses `tryLockForRead`, no hang path). The
  fixed build never wedges; the D3 timeout test pins the no-wedge behavior.

## Migration Plan

Single self-contained fix; no data or settings migration. Rollback = revert
the commit.

## Open Questions

None material.
