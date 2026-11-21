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
  external harmonic tether to each bond incident to the dragged atom, evaluated inside the session's
  own energy/gradient assembly (client-side L-BFGS over library singlepoints — no xTB constraint API
  dependency). The tether pulls the *partner* atoms toward the pinned dragged atom, so a hard pull
  drags the bonded neighborhood along (taffy) instead of snapping the bond. The dragged atom keeps
  tracking the pointer exactly; drag authority is unchanged.
- **Drawn-bond targets with repair cap**: tether target `d0` is captured per incident bond at drag
  start from the live molecule. A bond grabbed already beyond the strain threshold (1.3x covalent
  radii sum, the existing convention) gets a capped target at the covalent-radius sum, so grabbing a
  previously broken bond begins pulling it back together.
- **Force-field parity**: the tether gives the xTB drag path the same contract the OpenBabel force
  field path already has — FF bond terms are harmonic and unbreakable, so FF drags can never
  dissociate a bond. After this change, no backend lets a drag break a drawn bond.
- **Unreachable dissociation via drags**: because the release geometry always has partners at
  tethered distances, the dissociated-basin failure mode (`dragReleaseDissociatedLatchesWithRecovery`
  via the drag path) becomes unreachable through dragging; the strain report and the elevated release
  evaluation keep their roles as stretch feedback and recovery for stretched-but-bonded geometry.
- **No UI or tool changes**: the tether lives entirely in the worker; `autoopttool.cpp`, the HUD, the
  strain report, and the write-back masking are unchanged.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities

- `auto-optimize-interaction`: add a requirement that a drag on any optimization backend SHALL NOT
  break an existing bond — the pull SHALL deform the molecule to keep drawn bonds connected, with
  deterministic tether targets, release re-equilibration, and unchanged drag authority; drag-time
  failure containment, strain visibility, and write-back requirements are unaffected.

## Impact

- `libavogadro/src/xtb/xtbengine.{h,cpp}` — tether state (partner indices + bohr targets), capture at
  drag start (extends the per-tick live-molecule read with the bond list under the same bounded-retry
  read lock), external tether energy/gradient contribution in `evaluateOnce`, cleanup on drag end /
  topology re-establishment / quiesce / setup; new named constants (force constant, repair-cap
  threshold) beside the existing `kXtb*` block.
- `libavogadro/src/xtb/lbfgs.{h,cpp}` — no changes (the tether rides the existing per-evaluation
  E/grad callback).
- `libavogadro/src/tools/autoopttool.cpp` — no changes (worker-side capture; press/move/release path
  untouched).
- Tests: extend `libavogadro/tests/xtbenginetest.cpp` (hard-pull keeps the molecule connected; release
  contracts the bond; pre-broken bond heals on grab; no tether without a live molecule source;
  headless vector setup unaffected). Physics tests keep the skip-without-library convention.
- Renderers, Python bindings, and other engines are untouched.
