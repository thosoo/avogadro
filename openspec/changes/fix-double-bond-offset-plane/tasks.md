## 1. Shared helper (D1/D2/D4)

- [ ] 1.1 Add the spread-normal helper (e.g. `libavogadro/src/rendering/bondspread.h`): given both
  bonded atoms' positions and the neighbor positions, return the normalized in-plane direction
  perpendicular to the bond axis via accumulated `substituent × axis` cross products, with the D2
  fallback chain (no substituents / degenerate → empty or fallback vector, deterministic); verify by
  build
- [ ] 1.2 Unit-test the helper: benzene ring built in the world xz plane → returned normal lies in
  the ring plane (⊥ axis and ⊥ ring normal within tolerance); terminal carbonyl (one substituted
  atom) → normal in the substituent plane; isolated diatomic and collinear-degenerate input →
  deterministic fallback, no crash; same input twice → identical output; verify with the new test
  target (or the existing rendering test target) via ctest

## 2. Modern tier plumbing (D3)

- [ ] 2.1 Extend `SceneBuilder::ln`/`appendBond`/`appendLine` with an optional spread-normal
  parameter (default = empty → current `unitOrthogonal` behavior preserved); verify by build +
  existing scene-builder tests green
- [ ] 2.2 In `snapshottessellator.cpp`, build a per-build neighbor pass over snapshot atoms/bonds and
  pass each multiple bond's spread normal from the shared helper down to `ln`; verify by build +
  code inspection against design D3
- [ ] 2.3 Scene-level test: tessellate a snapshot of the xz-plane benzene and assert every double
  bond's second cylinder instances are displaced within the ring plane (offset direction within
  tolerance of the helper's output); verify with ctest

## 3. Fixed-function tier plumbing (D3)

- [ ] 3.1 Add an optional plane-normal parameter to `Painter::drawMultiCylinder`/`drawMultiLine` and
  the `GLPainter` overrides (default preserves the current widget-normal behavior so qtaim and the
  Python painter binding stay source-compatible); `Cylinder::drawMulti` consumes the caller-supplied
  normal; MSVC-safe signatures (no aggregate initialization); verify by build + code inspection
- [ ] 3.2 `bsdyengine.cpp` and `wireengine.cpp` compute the per-bond spread normal from live molecule
  positions via the shared helper and pass it through their `drawMultiCylinder`/`drawMultiLine`
  calls; verify by build
- [ ] 3.3 Tier-parity test: for the same test bond, the legacy path with the supplied normal and the
  modern path produce the same spread plane (offset directions parallel within tolerance); verify
  with ctest

## 4. Full verification

- [ ] 4.1 Run the affected suites: `ctest -R "scenebuilder|snapshot|painter|gpu" --output-on-failure`
  (plus whatever targets the new tests landed in); all green, including pick-id and overlay-parity
  tests that touch `SceneBuilder::appendBond`
- [ ] 4.2 Run the full ctest suite; no regressions in previously passing tests
- [ ] 4.3 Manual check: benzene displayed at an arbitrary orientation shows double bonds in the ring
  plane on the default tier, and `AVOGADRO_RENDERER=legacy` shows the same spread plane (tiers agree)
- [ ] 4.4 Push the branch and verify the Windows installer workflow compiles the change and the
  offscreen ctest leg is green (`gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`)
