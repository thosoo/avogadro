## 1. Tether state and capture (design D2/D3/D5)

- [x] 1.1 Extend the worker's live-molecule read to also copy the bond list (partner pairs per atom)
  under the existing bounded-retry read lock, and add worker-owned tether state (incident partner
  indices + bohr targets, previous-tick drag index) to `XtbEngine`; MSVC-safe plain vectors; verify
  by build
- [x] 1.2 Capture tether targets at the drag-start transition: `d0` = press length in bohr, capped at
  the covalent-radii sum when the press length exceeds 1.3x the covalent-radii sum (OpenBabel
  element radii); skip degenerate (near-zero-length) bonds; verify by build + a targeted headless
  test asserting the captured targets for a known molecule geometry
- [x] 1.3 Clear/rebuild tether state on drag end, topology re-establishment, quiesce, and `setup()`;
  verify by build + existing engine tests green (`xtbenginetest`)

## 2. Tether evaluation (design D1/D4/D6)

- [x] 2.1 Add the tether constants to the `kXtb*` block (force constant 1000 kJ/mol/A^2 in
  Hartree/bohr^2, repair threshold 1.3) and apply the tether energy/gradient in `evaluateOnce()` on
  the trial coordinates beside the dragged-atom gradient zeroing, guarding the degenerate
  (zero-distance) case; verify by build
- [x] 2.2 Headless-vector regression guard: a session with no live molecule source and a drag index
  set evaluates unchanged (no tether, no crash); verify with a new `xtbenginetest` case
- [x] 2.3 Hard-pull keeps the molecule connected: drag an incident atom of a small xTB molecule far
  beyond bonding distance and hold; assert the partner's distance to the dragged atom decreases over
  successive evaluations and no failure latch occurs during the held pull; verify with a new
  `xtbenginetest` case (skip-without-library convention)
- [x] 2.4 Release contracts the bond: release the hard-pulled drag; assert the released atom rejoins
  coverage and the bond distance returns within bonded range within a bounded evaluation budget;
  verify with a new `xtbenginetest` case
- [x] 2.5 Pre-broken bond heals on grab: start from a molecule whose bond exceeds the strain
  threshold, grab the atom, move it slightly, and assert the bond distance trends back toward the
  covalent-radii-sum target deterministically; verify with a new `xtbenginetest` case
- [x] 2.6 Bond removed mid-drag: delete an incident bond (topology change) while a drag is active and
  assert exactly one re-establishment with no stale tether (no tethered force attributable to the
  removed bond); verify with a new `xtbenginetest` case

## 3. Tool/backend invariants

- [x] 3.1 Drag-authority check: with multiple incident bonds, assert the dragged atom's published
  position equals the pointer position every tick under an extreme pull (tether acts only on
  partners); verify by extending the existing `draggedAtomPinnedDuringOptimization`-style test
- [x] 3.2 Force-field backend regression: run the existing AutoOpt force-field drag tests and confirm
  fix-marker drag behavior is unchanged (no tether involvement); verify with `autooptreprotest` /
  tool tests green

## 4. Full verification

- [x] 4.1 Run the xTB engine suites: `ctest -R "xtb" --output-on-failure` (engine, physics, runtime
  tests; physics tests skip without the library) — all green
- [x] 4.2 Run the full ctest suite; no regressions in previously passing tests
- [ ] 4.3 Manual check: on GFN2-xTB, flick-pull and slow-pull a terminal atom and a ring atom (e.g.
  the O–H of ethanol, then a ring C–H of benzene) — molecule follows the pull, bonds survive,
  release retracts; confirm HUD strain report still appears during heavy stretch and clears
- [x] 4.4 Push the branch and verify the Windows installer workflow compiles the change and the
  offscreen ctest leg is green (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`)

## 5. Bond-graph invariance (design D7/D8 — partner invariance, FF drag feel;
implemented as a SHAKE-style bond-graph projection after the floors-only design measured
insufficient against sustained drags, releases, and no-drag relaxation)

- [x] 5.1 Extend drag-start capture (same bounded-retry read-lock pass): tether targets for every
  drawn bond (press length; repair cap stays scoped to the dragged atom's incident bonds);
  verify by build
- [x] 5.2 Add the projection: a SHAKE-style `projectBondGraph` on the optimizer's line search
  (projector hook in `LBfgs`) that clamps every drawn bond into [0.5, 1.3]x the covalent-radii
  sum and pushes every non-bonded pair out of new-bond range (1.5x cov-sum, 2.0x H-H pairs),
  moving only non-dragged atoms; gradient sign consistency verified by the isolated-contact
  guard test catching the wrong-sign pull; verify by build
- [x] 5.3 Projection lifecycle: session-wide (every evaluation, drag or not), reading the
  current numbers/bond graph (setup/re-establishment update it immediately); tether stays
  drag-scoped; verify by build + existing engine tests green (including the headless `setup()`
  path gaining the bond list parameter)
- [x] 5.4 H2 split-off regression: ethane hard pull (perpendicular/far drag), assert minimum H–H
  distance stays well above new-bond range, partner-set invariance, session keeps publishing,
  no failure latch; projection load-bearing: the same scenario with the projection disabled must
  break the invariant (or latch); verify with new `xtbenginetest` cases (skip-without-library
  convention)
- [x] 5.5 Isolated-atom projection: drag an unbonded atom into another atom and assert the
  projection prevents new-bond range contact while the atom keeps tracking the pointer; plus a
  no-drag session test proving plain relaxation cannot collapse an unbonded H-H pair into H2;
  verify with new `xtbenginetest` cases
- [ ] 5.6 Full verification: `ctest -R "xtb"` green, full ctest suite green (xvfb; offscreen breaks
  GL-context tests), `autooptreprotest` green; push branch and verify the Windows installer
  workflow compiles the change with the offscreen ctest leg green
- [ ] 5.7 Manual check (extends 4.3): on GFN2-xTB an xTB drag feels like a force-field drag —
  neighborhood follows the pull, bonds hold, no atom swaps partners, release retracts; tune the
  two stiffness constants if feel is off
