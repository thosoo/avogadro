## 1. Tether state and capture (design D2/D3/D5)

- [ ] 1.1 Extend the worker's live-molecule read to also copy the bond list (partner pairs per atom)
  under the existing bounded-retry read lock, and add worker-owned tether state (incident partner
  indices + bohr targets, previous-tick drag index) to `XtbEngine`; MSVC-safe plain vectors; verify
  by build
- [ ] 1.2 Capture tether targets at the drag-start transition: `d0` = press length in bohr, capped at
  the covalent-radii sum when the press length exceeds 1.3x the covalent-radii sum (OpenBabel
  element radii); skip degenerate (near-zero-length) bonds; verify by build + a targeted headless
  test asserting the captured targets for a known molecule geometry
- [ ] 1.3 Clear/rebuild tether state on drag end, topology re-establishment, quiesce, and `setup()`;
  verify by build + existing engine tests green (`xtbenginetest`)

## 2. Tether evaluation (design D1/D4/D6)

- [ ] 2.1 Add the tether constants to the `kXtb*` block (force constant 1000 kJ/mol/A^2 in
  Hartree/bohr^2, repair threshold 1.3) and apply the tether energy/gradient in `evaluateOnce()` on
  the trial coordinates beside the dragged-atom gradient zeroing, guarding the degenerate
  (zero-distance) case; verify by build
- [ ] 2.2 Headless-vector regression guard: a session with no live molecule source and a drag index
  set evaluates unchanged (no tether, no crash); verify with a new `xtbenginetest` case
- [ ] 2.3 Hard-pull keeps the molecule connected: drag an incident atom of a small xTB molecule far
  beyond bonding distance and hold; assert the partner's distance to the dragged atom decreases over
  successive evaluations and no failure latch occurs during the held pull; verify with a new
  `xtbenginetest` case (skip-without-library convention)
- [ ] 2.4 Release contracts the bond: release the hard-pulled drag; assert the released atom rejoins
  coverage and the bond distance returns within bonded range within a bounded evaluation budget;
  verify with a new `xtbenginetest` case
- [ ] 2.5 Pre-broken bond heals on grab: start from a molecule whose bond exceeds the strain
  threshold, grab the atom, move it slightly, and assert the bond distance trends back toward the
  covalent-radii-sum target deterministically; verify with a new `xtbenginetest` case
- [ ] 2.6 Bond removed mid-drag: delete an incident bond (topology change) while a drag is active and
  assert exactly one re-establishment with no stale tether (no tethered force attributable to the
  removed bond); verify with a new `xtbenginetest` case

## 3. Tool/backend invariants

- [ ] 3.1 Drag-authority check: with multiple incident bonds, assert the dragged atom's published
  position equals the pointer position every tick under an extreme pull (tether acts only on
  partners); verify by extending the existing `draggedAtomPinnedDuringOptimization`-style test
- [ ] 3.2 Force-field backend regression: run the existing AutoOpt force-field drag tests and confirm
  fix-marker drag behavior is unchanged (no tether involvement); verify with `autooptreprotest` /
  tool tests green

## 4. Full verification

- [ ] 4.1 Run the xTB engine suites: `ctest -R "xtb" --output-on-failure` (engine, physics, runtime
  tests; physics tests skip without the library) — all green
- [ ] 4.2 Run the full ctest suite; no regressions in previously passing tests
- [ ] 4.3 Manual check: on GFN2-xTB, flick-pull and slow-pull a terminal atom and a ring atom (e.g.
  the O–H of ethanol, then a ring C–H of benzene) — molecule follows the pull, bonds survive,
  release retracts; confirm HUD strain report still appears during heavy stretch and clears
- [ ] 4.4 Push the branch and verify the Windows installer workflow compiles the change and the
  offscreen ctest leg is green (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`)
