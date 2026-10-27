# Tasks

## 1. endFrame state restoration

- [x] 1.1 `modernbackend.cpp` `endFrame`: after the sphere instanced draw,
      release `m_sphereGeom.program`, disable vertex attributes 2–4, and reset
      their divisors to 0. Verify: compiles under GCC/Clang and MSVC.
- [x] 1.2 `endFrame`: after the cylinder/line instanced draws, release
      `m_cylinderGeom.program`, disable vertex attributes 2–5, and reset their
      divisors to 0. Verify: compiles; the early-return path is unchanged.
- [x] 1.3 Local verification: `cmake --build build -j8 && ctest --test-dir
      build --output-on-failure` green (default platform). Verify: local run.
- [x] 1.4 Windows legs: both workflows green on the fix commit. Verify:
      `gh run list` on the pushed commit.
- [ ] 1.5 User-machine acceptance: with a white background — the axes glyph
      renders in its corner (red/green/blue), the debug HUD text persists after
      the first atom is drawn, and the black triangle ring at the screen center
      is gone. Verify: user-reported outcome.

## 2. Bookkeeping

- [x] 2.1 Cross-reference the finding in `fix-drawtool-stale-atom-crash`
      bookkeeping if the user's crash repro used the same build (the two issues
      are independent; the overlay artifact was display-only and did not cause
      the drawtool AV).
