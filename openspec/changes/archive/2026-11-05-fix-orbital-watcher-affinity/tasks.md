# Tasks: fix-orbital-watcher-affinity

Implementation notes for the applying agent: follow `design.md` D1–D5. The
root cause and both validated fix shapes are documented there — do not
re-derive. Section order is dependency order. Per the repo's delegation
policy, run the `reviewer` subagent on the full diff after tasks 1–3 land
and before declaring the change complete (this touches lock-taking and
cross-thread code paths).

## 1. Core fix — GUI-side construction, worker-side fill

- [x] 1.1 In `libavogadro/src/extensions/surfaces/orbitalextension.cpp`,
      split `parseBasisFile` into two steps: a GUI-thread step that
      constructs the correct `BasisSet` subclass from the file-format tag or
      extension (returning null for unknown/missing files, preserving the
      current null-return semantics), and a worker step that fills the
      pre-constructed basis with the stack reader (`ORCAOutput`,
      `GaussianFchk`, `GAMESSUSOutput`, `GamessukOut`, `MoldenFile`,
      `MopacAux`). Cover the `gamout`/`gukout` early paths too. Keep
      `loadBasis()`'s epoch capture, dispatch, and `invokeMethod`
      continuation byte-identical except for passing the pre-constructed
      basis into the lambda. Verify: `cmake --build build --parallel`
      green; no other function changed (`git diff --stat` shows only
      orbitalextension.cpp + tests at this point).
- [x] 1.2 Reader-context audit: confirm in the session log that each of the
      six reader constructors only touches its file argument and the basis
      it fills (no thread-sensitive globals, no extension member access) —
      design D1's precondition. Verify: grep-level audit recorded with
      file:line for each reader.

## 2. Invariant documentation

- [x] 2.1 Add the design D2 contract note to `basisset.h` (class comment)
      and at the async entry points in `gaussianset.h`/`slaterset.h`
      (`startCubeCalculationAsync` and friends): objects whose async
      completion signals are consumed on the GUI thread must be constructed
      on the GUI thread, or their `QFutureWatcher` re-homed to it;
      QFutureWatcher callouts post to the watcher's own thread. Verify:
      comment present at all four locations; build green.

## 3. Regression tests (orbitalselectiontest)

- [x] 3.1 Add the cheap affinity assert (design D3.1): after the async load
      continuation runs for `TESTDATADIR`/`koffein_orca.out`, the basis
      object's `thread()` equals the dispatching (GUI) thread. Verify:
      `ctest -R orbitalselection --output-on-failure` green **and** the
      assert demonstrably fails when temporarily reverted to the broken
      shape (record the red run in the session log, then restore the fix).
- [x] 3.2 Add the end-to-end test (design D3.2): drive the real `loadBasis()`
      path on the test's event loop for `koffein_orca.out` and
      `benzene.fchk`, with a bounded timeout, and assert a queue entry
      reaches completed/progress-final (the no-wedge observable). Verify:
      `ctest -R orbitalselection --output-on-failure` green; with the fix
      reverted the test times out red (record in session log).
- [x] 3.3 Molden coverage: extend 3.2 with `koffein_orca.molden` if the
      binary's existing wiring allows it without new build changes;
      otherwise record a deliberate skip with the reason (design D5
      analog: shared path already covered by two formats). Verify: test
      green or skip rationale in session log.

## 4. Verification

- [x] 4.1 Full suite: `cmake --build build --parallel && ctest --test-dir
      build --output-on-failure` green — in particular `orbitalselection`,
      `surfaceextension`, `surfaceextensionmothrottle`, `cubeeval`,
      `meshgenparallel` (no cross-regression in the surfaces family).
      Verify: exit code 0.
- [x] 4.2 Headless end-to-end sanity (reproduce the user's exact scenario):
      under `xvfb-run` with software GL, run the app on
      `testfiles/koffein_orca.out`, capture the debug log, and confirm the
      pipeline advances past `Cube calculation started.` to
      `Cube calculation finished.` / mesh phases within the capture window
      (before the fix, the log stopped at `started.` and all pool threads
      sat idle). Verify: captured log shows the completion lines; attach or
      excerpt in session log.
- [x] 4.3 Manual matrix re-run on the final build (design D4), in the app:
      (a) open `koffein_orca.out` — responsive load, table appears, HOMO
      row selectable, surface renders; (b) a second orbital on the same
      document renders without reopening (no-wedge row); (c) progress bar
      advances and completes; (d) reopen the file and repeat once. Record
      per-row results and note in the log that these rows supersede the
      stale 7.3 record in fix-orbital-visualization. Verify: all rows pass
      on the final build; results recorded.
- [x] 4.4 Run the `reviewer` subagent on the complete diff (correctness +
      regression risk per repo policy; lock-taking and cross-thread code
      changed). Integrate accepted findings or explicitly reject with
      evidence in the session log. Verify: review verdict recorded.
