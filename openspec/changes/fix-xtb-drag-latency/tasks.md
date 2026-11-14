## 1. Pacing interval (D1/D3)

- [x] 1.1 Change the xTB session pacing timer in `AutoOptTool` from `startTimer(100)` to
  `startTimer(30)`, and update the surrounding comment (which documents the 100 ms pacing and the
  worker-side write-back rationale) to reflect the new interval; verify by build + code inspection
  against design D1/D3
- [x] 1.2 Confirm the collapse semantics still hold under the faster interval: a tick requested while
  an evaluation is in flight neither queues work nor restarts evaluation (`XtbEngine::requestTick`
  sets only `m_tickPending`); verify by build + code inspection, and by
  `ctest -R xtbengineTest` green

## 2. Latch-semantics regression guard (D3)

- [x] 2.1 Run `ctest -R "xtbengineTest|autooptreproTest" --output-on-failure`; all green — in
  particular the converged-stops-evaluations, cap, drag-deferral, and release-recovery tests
  (they pin the idle/latch behavior the faster pacing must not disturb)
- [x] 2.2 Add a tool-level pacing check to `autooptreprotest` if the existing harness supports it
  cheaply: on a small distorted molecule, the evaluation counter advances at more than ~15/s while
  unconverged, and stops advancing after convergence (generous tolerance for CI load; skip the test
  if it cannot be made non-flaky and rely on 1.2 inspection instead); verify with
  `ctest -R autooptreproTest`
  - **SKIPPED via the task's escape hatch.** A pacing-rate test was prototyped and found *not
    achievable* on water, not merely flaky: a GFN2-xTB water singlepoint at interactive accuracy
    takes ~130–250 ms per L-BFGS step, so the worker (not the timer) is the bottleneck at ~5–7
    evals/s regardless of the 30 ms interval. A ">15/s" assertion always fails and any achievable
    threshold passes under the old 100 ms pacing too (no discrimination). Relied on 1.2 code
    inspection (`requestTick` sets only `m_tickPending` → no queue/restart) plus the manual feel
    check (task 3.2). See the D4 note in design.md. `ctest -R autooptreproTest` is green (35/35).

## 3. Full verification

- [x] 3.1 Run the full ctest suite locally; no regressions in previously passing tests
- [ ] 3.2 Manual feel check on a small molecule (drag responsiveness qualitatively improved vs the
  100 ms pacing) and on a mid-size molecule (no input freeze; CPU bounded by interactive accuracy;
  idle after convergence)
- [ ] 3.3 Push the branch and verify the Windows installer workflow compiles the change and the
  offscreen ctest leg is green (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`)
