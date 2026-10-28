# Tasks: DrawTool click-to-extend

Implementation notes for the applying agent: follow `design.md` decisions
D1–D8. File references are exact. Section order is dependency order — the
stroke-state hygiene fixes (2.x) restructure the same `mouseReleaseEvent`
the gesture wiring (3.x) extends, so land them first. Per the repo's
delegation policy, run the `reviewer` subagent on the full diff after tasks
2–4 land and before declaring the change complete (CUDA/Qt/templates are not
touched here, but the change modifies lock-taking tool code that must not
regress AutoOpt coexistence).

## 1. Placement helper (pure geometry)

- [ ] 1.1 Create `libavogadro/src/tools/placement.h` / `placement.cpp`:
      `SketchHyb` enum, `sketchHybridization(Atom*, Molecule*)` implementing
      design D3 (≥1 triple bond or ≥2 double bonds → Sp; exactly 1 double →
      Sp2; else Sp3) from the molecule's bond orders, and
      `placeSubstituent(origin, heavyNeighbors, hyb, bondLength,
      preferredDir = zero)` implementing the D2 case table with the guarded
      normalize ladder (norm < 1e−6 → preferredDir → cardinal axes → +X).
      Every returned position must be finite. Bond orders ≥ 3 (incl. metal–
      metal quadruple bonds) map to Sp per D3. Verify: compiles as part of
      the drawtool plugin (`drawtool_SRCS` in
      `libavogadro/src/tools/CMakeLists.txt:30` gains `placement.cpp`).
- [ ] 1.2 Add a `covalentBondLength(int elementA, int elementB)` helper
      (design D4): `OBElements::GetCovalentRad(A) + GetCovalentRad(B)`,
      1.5 Å fallback when either radius is 0. Verify: unit test asserts
      C+C → 1.54 ±0.02 Å and an unknown element pair → 1.5.
- [ ] 1.3 New headless unit test `libavogadro/tests/placementtest.cpp`
      registered as `placement` in the `tests` list
      (`libavogadro/tests/CMakeLists.txt`); compile
      `../src/tools/placement.cpp` directly into the test (same pattern as
      the `renderertier` entry, since tools are not exported from
      libavogadro). Cases, using hand-built positions (no GL, no widget):
      - sp3 1-heavy-nbr: new bond ≈109.47° ±0.5° to the axis
      - sp2 1-heavy-nbr: ≈120° ±0.5°
      - sp 1-heavy-nbr: exactly linear (180°)
      - 2-heavy-nbr sp3: ≈109.47° to both existing bonds
      - 3-heavy-nbr (three ideal tetrahedral directions): returns the exact
        4th vertex
      - order-4 (quadruple) bond on the origin atom → Sp, linear result
      - heavy-nbr-with-hydrogens ignored: same result whether or not H
        positions are included in `heavyNeighbors` (the caller filters; the
        helper never sees H) — assert caller-side filter logic separately if
        the helper is position-agnostic
      - length: |new bond| == requested bondLength
      - determinism: two identical calls → bitwise-equal positions; no NaN
        for degenerate inputs (zero vectors, collinear neighbors, symmetric
        sums). Verify: `ctest -R placement --output-on-failure` green.

## 2. Stroke-state hygiene (pre-existing bugs, design D7/D8)

- [ ] 2.1 Guard every `m_forceField` dereference in drawtool.cpp with
      `if (m_forceField)` (design D7): press SetIgnoreAtom (~266, ~424),
      move Unset/SetFixAtom (~337, ~365–366), release cleanup (~647–648).
      The destructor and `toolActivated()` guards already exist. Verify:
      build; `ctest -R autooptrepro --output-on-failure` still green (its
      binary may or may not resolve a force field, so also) full grep:
      no unguarded `m_forceField->` remains anywhere in the file.
- [ ] 2.2 Restructure `mouseReleaseEvent` so every exit reaches the common
      cleanup block (design D8.1): the right-click managed-H early return
      (~652), the right-click X–H bond early return (~664), and the
      `m_prevBond` X–H restore early return (~596) must now fall through to
      (or perform) the same reset + marker unsets before returning. The
      command semantics of each path (no deletion, order restored) are
      unchanged. Verify: build; `ctest -R drawcommand` and
      `-R addremovehydrogens` green; manual — with adjust hydrogens ON,
      right-click an H: it is not deleted and the NEXT left click behaves
      normally (not swallowed).
- [ ] 2.3 Fix the lock-failure exit of `mouseReleaseEvent` (design D8.2):
      when `tryLockForWrite()` fails and stroke state exists, release the
      force-field markers and null the stroke pointers (no molecule access
      needed) before returning 0; when no stroke state exists, return 0 as
      today. Verify: build; code review confirms no path leaves stroke
      state set without cleanup; full suite green.

## 3. DrawTool gesture wiring

- [ ] 3.1 `drawtool.h`: add `bool m_clickExtendCandidate = false;` and a
      small private helper declaring the synthesis used by release (design
      D5.3). Verify: builds.
- [ ] 3.2 `drawtool.cpp` press path (atom-hit branch, ~line 221): add the
      `else` setting `m_clickExtendCandidate = true` — no mutation, no
      force-field call. The `element != m_element` alchemy branch stays
      byte-identical. ALSO reset `m_clickExtendCandidate = false`
      unconditionally at press, next to `m_movedSinceButtonPressed = false`
      (line ~200, after the in-flight-stroke early-return guard) — the
      release cleanup and `toolActivated()` resets are not reachable on all
      paths (early returns, guarded block), so press-time reset is the only
      reliable one. Verify: build; manual smoke — click N with C selected
      still turns the N into C, and an alchemy click is never followed by an
      extension.
- [ ] 3.3 `drawtool.cpp` `mouseMoveEvent`: at the top of the left-button
      branch add the BondCentricTool threshold
      (`manhattanLength() > 2 → m_movedSinceButtonPressed = true`). Verify:
      build; a normal drag still stretches a bond.
- [ ] 3.4 `drawtool.cpp` `mouseReleaseEvent`, left-button branch BEFORE the
      `if(m_beginAtomAdded || m_bond)` assembly (design D5.3): when
      `m_clickExtendCandidate && !m_movedSinceButtonPressed` and
      `m_beginAtom` validates against the molecule
      (`molecule->atomById(m_beginAtom->id())` non-null), and the hydrogen
      guard passes (`!(m_addHydrogens && m_beginAtom->isHydrogen())`):
      gather heavy-neighbor positions (skip H's), compute hyb (1.1) and
      length (1.2), create the end atom at `placeSubstituent(...)`, set its
      element, `m_endAtomAdded = true`, and `m_bond = addBond(...)` honoring
      `m_bondOrder()`. Let the existing assembly run unchanged. Verify:
      `cmake --build build --parallel && ctest --test-dir build
      --output-on-failure` green.
- [ ] 3.5 Reset `m_clickExtendCandidate` in the `mouseReleaseEvent` cleanup
      block and in `toolActivated()` (belt-and-suspenders; the unconditional
      press-time reset from 3.2 is the authoritative one). Verify: build;
      extending, then switching tools mid-anything, leaves no stale candidate
      (next click behaves normally).

## 4. Documentation

- [ ] 4.1 Update the DrawTool tooltip in the constructor to document all
      gestures per design D5.5 (click atom = extend or element change; drag =
      draw bond; right-click = delete). Grep the repo for other
      user-facing draw-tool gesture docs (`docs/`, `README`, `.qhc`-adjacent
      help) and update any that enumerate gestures. Verify: tooltip text
      mentions the click gesture; `git grep -i "click and drag"` shows no
      stale draw-tool references outside changelogs.

## 5. Verification

- [ ] 5.1 Manual matrix (headless harness cannot drive GLWidget mouse
      events; do this in the app), each on the default tier:
      - click C with C selected on methane (adjust hydrogens ON): new C
        appears at a tetrahedral angle, one H removed; single undo restores
        4 H; redo leaves 3 H + bond
      - same on ethane: angle visibly not linear; repeated click-extends
        build a staggered chain
      - ethylene / acetylene test files if present under `testfiles/`:
        120° / 180° extensions
      - click N with C selected: element change, no new atom
      - select O, click O: new O bonded
      - bond order 2 selected, click C: double bond created
      - empty-space click, bond click, drag-from-atom, drag-onto-atom,
        right-click: all behave exactly as before
      - adjust hydrogens ON, click an H: nothing happens
      - right-click a managed H mid-stroke, then left-click another atom:
        the second click works as a fresh gesture (hygiene 2.2)
      Verify: all rows match `specs/molecule-sketching/spec.md` scenarios.
- [ ] 5.2 AutoOpt coexistence: start an AutoOpt session on a small molecule
      (fixed/ignored counts 0), click-extend two new atoms, confirm the
      optimizer moves them within a tick or two, on-screen fixed/ignored
      counts stay 0/0 after the clicks, and no atom freezes that was not
      intentionally constrained. Verify: matches the
      "Sketching coexists with a running optimization" requirement.
- [ ] 5.3 Full suite: `cmake --build build --parallel && ctest --test-dir
      build --output-on-failure` green (no regressions in `autooptrepro`,
      `drawcommand`, `addremovehydrogens`). Verify: exit code 0.
- [ ] 5.4 Run the `reviewer` subagent on the complete diff (correctness +
      regression risk per repo policy); integrate accepted findings.
      Verify: review findings addressed or explicitly rejected with evidence
      in the session log.
