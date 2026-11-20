## ADDED Requirements

### Requirement: Multi-bond spread follows local substituent geometry
The spread direction of a multiple bond's parallel cylinders (and lines) SHALL be
derived from the bond's local substituent geometry — the neighbors of the two bonded
atoms — so the pair lies in the bond's local sp2 plane, perpendicular to the bond
axis, rather than in a direction determined by the world coordinate frame or by a
whole-molecule best-fit plane. The computed spread SHALL be deterministic for
identical molecule state: the same geometry produces the same spread direction on
every invocation. Bonds whose atoms have no substituent neighbors (or only degenerate
geometry) SHALL fall back to a deterministic direction without crashing. The spread
plane SHALL be consistent across rendering tiers: the modern tier and the fixed-
function tiers SHALL spread the same multiple bond into the same plane, so a scene
renders with the same multi-bond geometry regardless of the active tier. The
arrangement of the cylinders (the symmetric ±shift straddle for double bonds) SHALL
be unchanged by this requirement.

#### Scenario: Ring double bonds lie in the ring plane at any orientation
- **WHEN** a benzene-like ring whose plane is not aligned with any world coordinate
  plane is displayed with double bonds on the modern tier or on the fixed-function
  tiers
- **THEN** each double bond's two cylinders are offset perpendicular to the bond axis
  within the ring plane, not sticking out of the molecular plane

#### Scenario: Terminal double bond uses its substituent plane
- **WHEN** a terminal double bond is displayed where one bonded atom has substituent
  neighbors and the other does not (for example a carbonyl on a chain end)
- **THEN** the cylinder pair lies in the plane defined by the substituted atom's
  neighbors and the bond axis

#### Scenario: Tiers agree on the spread plane
- **WHEN** the same molecule with multiple bonds is rendered once on the modern tier
  and once on a fixed-function tier with the same camera
- **THEN** each multiple bond's second cylinder appears in the same plane relative to
  the molecule on both tiers

#### Scenario: Deterministic spread for identical input
- **WHEN** the same molecule with multiple bonds is rendered twice without changes
- **THEN** every multiple bond's spread direction is identical between the two
  renders

#### Scenario: Degenerate bonds do not crash
- **WHEN** a multiple bond's atoms have no other neighbors (an isolated diatomic with
  bond order 2 or more) or positions are degenerate
- **THEN** the bond renders with a deterministic fallback spread direction and the
  process does not crash
