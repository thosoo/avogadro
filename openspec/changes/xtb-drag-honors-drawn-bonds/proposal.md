## Why

During a running xTB optimization session (GFN1-xTB, GFN2-xTB, GFN-FF), dragging an atom can pull
an existing bond beyond dissociation. Once the drag writes a beyond-bonding distance and the worker
evaluates it, release starts the L-BFGS phase from the dissociated geometry — a local minimum the
optimizer never leaves, so the drawn bond stays broken (topologically present, geometrically gone).
The existing strain report names the divergence while it develops, and the elevated release
evaluation rescues only *strained-but-recoverable* geometry; neither prevents the break. Bond
breaking during a relaxation drag is counterproductive: the bond graph is the user's stated intent,
and a pull should bend the molecule to honor it, not be allowed to contradict it.

## What Changes

- **Bond-honoring drag (tether)**: while a drag is active on an xTB backend, the worker adds an
  external harmonic tether to every drawn bond (not only those incident to the dragged atom),
  evaluated inside the session's own energy/gradient assembly (client-side L-BFGS over library
  singlepoints — no xTB constraint API dependency). The tether holds every drawn bond at its press
  length, so a hard pull drags the bonded neighborhood along (taffy) instead of snapping a bond.
  The dragged atom keeps tracking the pointer exactly; drag authority is unchanged.
- **Drawn-bond targets with repair cap**: tether target `d0` is captured per incident bond at drag
  start from the live molecule. A bond grabbed already beyond the strain threshold (1.3x covalent
  radii sum, the existing convention) gets a capped target at the covalent-radius sum, so grabbing a
  previously broken bond begins pulling it back together.
- **Force-field parity**: the tether gives the xTB drag path the same contract the OpenBabel force
  field path already has — FF bond terms are harmonic and unbreakable, so FF drags can never
  dissociate a bond. After this change, no backend lets a drag break a drawn bond.
- **Non-bonded floors (partner invariance)**: holding drawn bond *lengths* cannot stop two atoms
  that share a bonded neighbor from swinging together through an angle change — the measured H₂
  split-off pathway (radial constraints never see the H–C–H bend, and the xTB SCF genuinely
  favors forming the new H–H bond). During a drag, the worker additionally applies a repulsive
  harmonic floor to captured non-bonded pairs (graph separation ≥ 2, press distance < 3.0 Å,
  engaging below 1.5× the covalent-radii sum — 2× for hydrogen–hydrogen pairs — at twice the
  tether stiffness, twice that for hydrogen pairs), so no new bond can form: each atom's bond
  partners are exactly its drawn partners while the drag lasts, and an xTB drag feels like a
  force-field drag.
- **Unreachable failure modes via drags**: because the release geometry always has partners at
  tethered distances and non-partners outside new-bond range, neither the dissociated-basin
  failure mode (`dragReleaseDissociatedLatchesWithRecovery` via the drag path) nor a partner swap
  is reachable through dragging; the strain report and the elevated release evaluation keep their
  roles as stretch feedback and recovery for stretched-but-bonded geometry.
- **No UI or tool changes**: the tether lives entirely in the worker; `autoopttool.cpp`, the HUD, the
  strain report, and the write-back masking are unchanged.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities

- `auto-optimize-interaction`: amend the drag requirement to bond-graph invariance: a drag on any
  optimization backend SHALL NOT change the drawn bond graph — no drawn bond breaks AND no new
  bond forms (atom bond partners are invariant during a drag) — with deterministic tether
  targets, release re-equilibration, the force-field drag feel (neighborhood follows, bonds hold,
  release retracts), and unchanged drag authority; drag-time failure containment, strain
  visibility, and write-back requirements are unaffected.

## Impact

- `libavogadro/src/xtb/xtbengine.{h,cpp}` — tether state (bond list + bohr targets), non-bonded
  floor state (pair indices + floor distances), capture at drag start (extends the per-tick
  live-molecule read with the bond list and the floor-pair scan under the same bounded-retry read
  lock), external tether + floor energy/gradient contributions in `evaluateOnce`, cleanup on drag
  end / topology re-establishment / quiesce / setup; new named constants (floor factor, floor
  force constant) beside the existing `kXtb*` block.
- `libavogadro/src/xtb/lbfgs.h` — a 0.25 Å trial-step trust cap (design D9): the tether-stiffened
  landscape can otherwise propose near-coincident trial geometries the backend cannot evaluate.
- `libavogadro/src/tools/autoopttool.cpp` — no changes (worker-side capture; press/move/release path
  untouched).
- Tests: extend `libavogadro/tests/xtbenginetest.cpp` (hard-pull keeps the molecule connected; release
  contracts the bond; pre-broken bond heals on grab; no tether without a live molecule source;
  headless vector setup unaffected; new: ethane hard pull forms no H₂ — partner-set invariance with
  floors load-bearing; isolated atom cannot be driven into another atom). Physics tests keep the
  skip-without-library convention.
- Renderers, Python bindings, and other engines are untouched.
