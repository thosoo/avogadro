# Proposal: fix-orbital-watcher-affinity

## Why

Opening an ORCA output file no longer freezes the UI (the fix-orbital-visualization goal was met), but **no orbital is ever displayed**: the async basis parse introduced in commit `8cb5b8f24` constructs the OpenQube `GaussianSet` on a `QtConcurrent` pool thread, which orphans its internal `QFutureWatcher<void> m_watcher` on a thread that has no event loop. Every subsequent cube calculation completes (`future.isFinished() == true`, values computed), but the watcher's `finished` notification is posted into the dead pool-thread queue and never delivered — so `OrbitalExtension::calculateCubeDone` never runs, the first orbital wedges the calculation queue (its mutex is held until a completion that never fires), and no mesh is ever generated. The GUI stays fully responsive, which makes the failure silent. Root cause, mechanism, and both fix shapes were reproduced and validated headlessly against `testfiles/koffein_orca.out` during exploration (2026-11-04).

## What Changes

- **Parse-result construction returns to the GUI thread.** `OrbitalExtension::parseBasisFile` no longer constructs the OpenQube `BasisSet` subclass inside the `QtConcurrent::run` worker; the correct subclass is constructed on the GUI thread and the worker only fills it via the existing `(filename, BasisSet*)` reader constructors (`ORCAOutput`, `GaussianFchk`, `GAMESSUSOutput`, `GamessukOut`, `MoldenFile`, `MopacAux`, plus the `gamout`/`gukout` early paths). The parse itself remains off the GUI thread — the freeze fix is preserved.
- **Watcher-affinity invariant documented and pinned by test.** A OpenQube `BasisSet` whose `QFutureWatcher`-driven async entry points are consumed on the GUI thread must be constructed (or have its watcher re-homed) on the GUI thread; an end-to-end regression test in `orbitalselectiontest` loads a real ORCA file through the async path and asserts the calculation completes.
- **Queue can no longer wedge permanently.** With notifications flowing again, the calculation queue drains as designed. The invariant that a missed completion cannot permanently hold `m_runningMutex` is covered by the same regression test (queue completes without any document switch).
- **Honest re-verification of the fix-orbital-visualization manual matrix.** Task 7.3 rows ("when it completes, it renders automatically") were marked complete on a build where they are unobservable; this change re-runs those rows on the final build and records the result.
- **No changes** to the cube-calculation slab engine, mesh generation, SurfaceExtension (its basis parse is still synchronous and its watcher therefore GUI-affine — it works today), rendering, or the OpenBabel pin.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `surface-visualization`: adds a requirement that orbital/surface completion notifications and progress indication must be delivered regardless of which thread performed the file parse (guarding the watcher-affinity invariant that the async parse broke), plus an end-to-end scenario: opening an ORCA output and selecting an orbital produces a rendered surface, and the calculation queue drains without any document switch.

## Impact

- **Modified:** `libavogadro/src/extensions/surfaces/orbitalextension.cpp` (`parseBasisFile` split into GUI-side construction + worker-side fill; `loadBasis` continuation unchanged), `libavogadro/tests/orbitalselectiontest.cpp` + its `CMakeLists.txt` entry (end-to-end regression test; OpenQube/Qt6::Concurrent already linked).
- **Unchanged but documented:** `libavogadro/src/extensions/surfaces/openqube/gaussianset.{h,cpp}` (the orphaned-watcher mechanism lives here; the invariant is documented at the async entry points), `slaterset` (same defect class, same fix covers MOPAC `.aux` via the shared extension path).
- **No changes:** rendering tiers, AutoOpt, DrawTool, camera, OpenBabel submodule, build system beyond the test registration.
- **Risk if unfixed:** all orbital formats (ORCA, Gaussian fchk, GAMESS, molden, MOPAC aux) silently produce no surfaces while remaining fully responsive — a regression users cannot self-diagnose.
