## 1. Engine: defer idle latches during an active drag (D1/D2)

- [x] 1.1 Gate the converged-latch sites on drag state: the baseline-check and post-step
  convergence checks in `XtbEngine::run()` (and the capped branch) do not latch while
  `m_draggedIndex >= 0`; the session keeps ticking at interactive accuracy; verify by build +
  code inspection against design D1
- [x] 1.2 Reset the evaluation counter when the worker's re-baseline observes pointer movement
  of the dragged atom (drag-scoped budget, design D2); verify by build + code inspection
- [x] 1.3 Add the engine tests to `xtbenginetest`: hold-pull convergence deferral (evaluations
  keep advancing across a held stretched pull; converged latch engages only after release),
  cap deferral across a long drag, and release-then-idle normal convergence; verify with
  `ctest -R xtbenginetest`

## 2. Engine: elevated release evaluation + recovery diagnostics (D3/D4)

- [x] 2.1 On release, run the fresh phase's first evaluation with an elevated SCF iteration
  budget (one bounded attempt; on failure the containment latch applies unchanged); verify by
  build + code inspection against design D3
- [x] 2.2 Append the recovery path ("restart the session (Stop/Start) or edit the molecule to
  re-establish it") to `latchEvaluationFailure` diagnostics; verify by build + the 2.3 test
- [x] 2.3 Add engine tests: release at a strained geometry converges under the elevated budget
  and the session continues; release at a dissociated geometry latches exactly once with the
  recovery-path text and publishes nothing; verify with `ctest -R xtbenginetest`

## 3. Tool: drag-time bond-strain HUD report (D5)

- [x] 3.1 In `AutoOptTool`'s HUD path, while a drag is active compute the dragged atom's
  distance to its bonded partners and append a strain line when the distance exceeds
  1.3 × (sum of covalent radii); clear at release or on return in range; verify by build +
  code inspection against design D5
- [x] 3.2 Add tool tests to `autooptreprotest`: hold-and-pull keeps write-back publishing
  across the held pull (no freeze); strain line appears when dragging beyond bonding range and
  clears at release; verify with `ctest -R autooptreprotest`

## 4. Full verification

- [x] 4.1 Build and run the affected suite locally: `cmake --build build --parallel && ctest
  --test-dir build -R "xtbengineTest|autooptreproTest" --output-on-failure`; all green
- [x] 4.2 Run the full ctest suite; no regressions in previously passing tests
- [ ] 4.3 Push the branch and verify the Windows installer workflow compiles the new code and
  the offscreen ctest leg is green (`gh workflow run 168745168 --ref <branch> --repo
  thosoo/avogadro`)
