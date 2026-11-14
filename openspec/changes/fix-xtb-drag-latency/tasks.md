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
  - **Implemented as `xtbPacingRateGfnff` on the GFN-FF backend**, where the rate assertion is
    both achievable and discriminating: GFN-FF water steps cost ~5 ms, so the timer (not the
    worker) bounds the rate — ~33 evals/s at 30 ms pacing vs ~10/s at the old 100 ms. Verified
    discriminating in both directions (passes 5/5 at 30 ms; fails 3/3 at 100 ms with ~9.9/s
    measured), with the drag engaged to defer the converged latch (D1/D3) and a post-release
    counter-stop assertion for the idle latch.
  - On **GFN2 water** the assertion remains genuinely unachievable (worker-bound at ~5–7 evals/s
    regardless of the timer; each interactive-accuracy L-BFGS step costs ~130–250 ms), so GFN2 is
    covered by 1.2 code inspection plus the latch/cap suites. See the D4 note in design.md.
  - Suite-side adjustment (no assertion weakened): this test drives a GFN-FF
    session through a drag-to-convergence lifecycle, and that lifecycle leaves
    process-global xTB state that breaks the NEXT GFN2 topology
    re-establishment in the same process — measured A/B: the pre-existing
    `xtbEraseDuringRunSurvivorsMove` fails 3/5 with this test before it and
    0/5 without (same tree); a GFN-FF session WITHOUT the drag-to-convergence
    lifecycle does not trigger it; widening the erase test's QTRY window to
    30 s did not help (publication stops, not slows). Fix: `xtbPacingRateGfnff`
    is declared LAST so no session-driving test follows it. This exposes a
    pre-existing engine-level global-state issue (GFN-FF drag-to-convergence
    lifecycle poisoning subsequent GFN2 re-establishment in one process) —
    out of scope for this pacing change, recorded as an engine follow-up.

## 3. Full verification

- [x] 3.1 Run the full ctest suite locally; no regressions in previously passing tests
- [ ] 3.2 Manual feel check on a small molecule (drag responsiveness qualitatively improved vs the
  100 ms pacing) and on a mid-size molecule (no input freeze; CPU bounded by interactive accuracy;
  idle after convergence)
- [ ] 3.3 Push the branch and verify the Windows installer workflow compiles the change and the
  offscreen ctest leg is green (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`)
