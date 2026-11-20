## Why

Multiple bonds (double bonds in ball-and-stick) render with their second cylinder offset in a
direction that has nothing to do with the bond's local chemistry. On the modern tier the offset is
`axis.unitOrthogonal()` (`scenebuilder.cpp`), which Eigen resolves to `(-y, x, 0)` — a vector in the
**world xy plane** — so any double bond whose axis does not lie in that plane gets its cylinder pair
straddling *perpendicular* to the local molecular plane (e.g., a benzene ring loaded in any other
orientation shows the pair sticking out of the ring: the reported "90° tilt"). On the legacy tier the
offset is `axis × globalBestFitPlaneNormal` (`glpainter_p.cpp` → `cylinder_p.cpp`), which only
coincides with the local ring plane when the whole molecule is flat. Neither tier looks at the sp2
substituent geometry that defines where the second line belongs.

## What Changes

- **Local-plane offset direction**: the multi-bond spread direction is computed from the bond's own
  substituents — neighbors of both bonded atoms — so the cylinder pair lies in the bond's local sp2
  plane, perpendicular to the bond axis, on every rendering tier. Fallbacks: terminal bonds use the
  other atom's substituents; bonds with no substituents keep today's behavior.
- **Shared helper**: one small geometry helper computes the offset normal from atom positions and
  neighbor positions; both the modern path (snapshot tessellator → `SceneBuilder::ln`/`appendBond`)
  and the legacy path (bsdyengine → `Painter::drawMultiCylinder` → `Cylinder::drawMulti`, which
  already accepts a `planeNormalVector` and is simply fed the wrong one) use it, so the tiers agree.
- **Arrangement unchanged**: the two-cylinder symmetric ±shift straddle stays as-is (user decision:
  keep two cylinders; no one-sided Kekulé inner-line convention). The angular-vs-collinear triple
  bond divergence noted in `scenebuilder.cpp` stays out of scope.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities

- `gpu-rendering-pipeline`: add a requirement under visual parity — multi-bond spread geometry SHALL
  follow the bond's local substituent plane and SHALL be consistent across rendering tiers (modern
  and legacy produce the same spread plane for the same bond), deterministic for identical input.

## Impact

- `libavogadro/src/rendering/scenebuilder.{h,cpp}` — `ln`/`appendBond` accept an offset-plane normal
  (default preserves current behavior for degenerate input).
- `libavogadro/src/rendering/snapshottessellator.cpp` — computes the local normal from snapshot
  atoms/bonds and passes it down.
- `libavogadro/src/painter.h`, `libavogadro/src/glpainter_p.{h,cpp}`, `libavogadro/src/python/painter.cpp`
  — optional plane-normal parameter on `drawMultiCylinder`/`drawMultiLine` (default = current
  behavior); `libavogadro/src/cylinder_p.cpp` consumes the caller-supplied normal instead of the
  widget's global molecule normal for the multi path.
- `libavogadro/src/engines/bsdyengine.cpp`, `libavogadro/src/engines/wireengine.cpp` — compute the
  per-bond normal from the live molecule and pass it through.
- Tests: new unit tests for the helper (benzene in a non-xy world plane, terminal C=O, degenerate
  no-neighbor bonds) and a scene-level assertion that second cylinders lie in the sp2 plane;
  existing gpu/parity tests stay green. MSVC-safe header changes only (no aggregate init).
