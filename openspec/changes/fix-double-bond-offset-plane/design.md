## Context

Two independent spread-direction computations exist, and neither consults the bond's substituents:

- **Modern tier** (`libavogadro/src/rendering/scenebuilder.cpp`, `SceneBuilder::appendBond`): the
  offset is `axis.normalized().unitOrthogonal() * qAbs(shift)`. Eigen's 3-vector `unitOrthogonal`
  returns `(-y, x, 0)` for any axis with significant x or y (`OrthoMethods.h`), i.e., a vector in the
  **world xy plane** — so the spread plane depends on which world plane the bond axis happens to sit
  in. A benzene ring in any other orientation gets its double-bond pairs straddling the ring *normal*
  — the reported 90° tilt.
- **Fixed-function tiers** (`glpainter_p.cpp:drawMultiCylinder` → `cylinder_p.cpp:Cylinder::drawMulti`):
  the spread is `axis × widget->normalVector()`, where `normalVector()` is the molecule's **global
  best-fit plane** normal (`molecule.cpp:computeGeomInfo`, PCA over all atoms). Correct only when the
  local ring plane coincides with the global plane; diverges with out-of-plane bulk.

Both `Cylinder::drawMulti` and the modern spread loop already take a direction parameter/argument —
the plumbing exists; the *value* fed in is wrong. The wire path (`drawMultiLine`, `wireengine.cpp`,
`SceneBuilder::appendLine`) mirrors the cylinder path.

Data sources differ per call site: the modern snapshot tessellator (`snapshottessellator.cpp`) sees
snapshot atom positions + bonds; the legacy engines (`bsdyengine.cpp`, `wireengine.cpp`) see the live
molecule. Both have enough information to gather a bond's neighbor positions.

Constraints: the symmetric ±shift straddle arrangement is kept (user decision: two cylinders, no
one-sided Kekulé inner line, so the *sign* of the spread normal is irrelevant for order 2). The
angular-vs-collinear triple-bond divergence between tiers is pre-existing and out of scope. MSVC rules
apply: no aggregate initialization of classes with user-provided special members; virtual signature
changes touch `Painter`, `GLPainter`, and the Python painter binding (keep the old signature working
via a defaulted overload so the binding and any external callers stay source-compatible).

## Goals / Non-Goals

**Goals:**
- Spread direction lies in the bond's local sp2 plane on every tier, deterministic for identical
  input, with safe fallbacks.
- One shared computation so the tiers agree by construction.
- No change to the straddle arrangement, radii, shift constant, or pick behavior.

**Non-Goals:**
- Ring-perception-based one-sided (inner-line) double bond arrangement.
- Triple/quadruple bond angular arrangement unification (pre-existing modern/legacy divergence,
  noted in `scenebuilder.cpp`; separate change if ever wanted).
- Bond order re-perception, valence models, or any chemistry perception beyond immediate neighbors.
- Robustness when the substituent geometry is itself degenerate.

## Decisions

**D1 — Shared helper: substituent cross-product normal.** One function, e.g.
`bondSpreadNormal(bondBeginPos, bondEndPos, neighborPositions)` returning a unit vector in the sp2
plane perpendicular to the bond axis: for each substituent position *p* (neighbors of either bonded
atom, excluding the partner), accumulate `normalize(p - atomPos) × axis`; normalize the sum. Using
the *cross product with the axis* (rather than fitting a plane to substituents) keeps the result
perpendicular to the axis by construction. Location: a small header alongside the rendering code
(e.g., `libavogadro/src/rendering/bondspread.h`) so both the rendering path and the engines can
include it without new link dependencies.

**D2 — Fallback chain.** No substituents at all (isolated multiple-bonded diatomic): fall back to
today's behavior (`unitOrthogonal` on the modern tier; the existing widget normal on the legacy tier
— acceptable because an isolated diatomic has no "plane" for the spread to violate). A single
substituent: `substituent × axis` is already well-defined. Degenerate (zero-length cross sums, or
substituents collinear with the axis): fall through the same fallback. All fallbacks deterministic.

**D3 — Plumbing per tier.**
- *Modern*: `SceneBuilder::ln`/`appendBond`/`appendLine` gain an optional spread-normal parameter
  (default empty → current `unitOrthogonal` fallback). `snapshottessellator.cpp` builds an adjacency
  pass over the snapshot bonds once per build and passes each bond's normal.
- *Legacy*: `Painter::drawMultiCylinder`/`drawMultiLine` gain an optional plane-normal parameter
  (default: current behavior of fetching the widget's molecule normal, so existing callers — qtaim,
  python bindings — are untouched). `Cylinder::drawMulti` already receives the normal; it simply gets
  the caller's value instead of the widget's global one. `bsdyengine` and `wireengine` compute the
  per-bond normal from live molecule positions via the shared helper and pass it down.

**D4 — Determinism.** The helper is pure floating-point math on positions; same input → same output.
No iteration-order dependence: the sum over substituents is order-insensitive up to floating-point
association; substitutes are gathered in bond order, which is stable for a given molecule state. The
existing "bitwise determinism" convention for rendering applies.

**D5 — Verification.**
- Unit tests for the helper: benzene ring built in the world xz plane (axes with significant z) →
  normal in-plane; terminal C=O (one substituted atom) → normal in the substituent plane; isolated
  diatomic → fallback without crash; determinism (same input twice → identical output).
- Scene-level test: tessellate a snapshot of an xz-plane benzene and assert each double bond's second
  cylinder instances are displaced within the ring plane (offset direction ⊥ axis and ⊥ ring normal,
  within tolerance).
- Tier-parity: legacy path test via `GLPainter`/`Cylinder::drawMulti` with a supplied normal matches
  the modern path for the same bond.
- Existing suites green (including the pick-id and overlay-parity tests that touch
  `SceneBuilder::appendBond`).

## Risks / Trade-offs

- **[Spread direction changes for every existing multi-bond render]** users see differently-oriented
  second cylinders — that is the point, but it is a visible change on every molecule, not just
  benzene. → The new direction is the chemically conventional one; the fallback keeps exotic cases
  renderable.
- **[Acyclic molecules have no canonical plane]** for a chain C=C, the substituent plane is well
  defined (the sp2 geometry is planar), but for distorted/unrelaxed geometry the cross-product sum
  can wobble. → Deterministic and bounded by the actual substituent positions; acceptable for a
  drawing convention.
- **[Virtual signature additions on Painter]** touches an interface with a Python binding. →
  Defaulted parameter preserves source compatibility; MSVC-safe (no aggregate init), verified by the
  Windows installer offscreen leg.

## Migration Plan

Single PR against master; no settings or data migration. Rollback = revert the commit. Windows
verification rides the installer workflow's offscreen ctest leg.

## Open Questions

None blocking. (If users later want the textbook one-sided inner line for ring double bonds, that is
a separate arrangement change layered on the same normal computation plus ring perception.)
