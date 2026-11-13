## Why

With xTB sessions live-editable, dragging an atom works — but the session declares convergence
on the *masked* gradient (the dragged atom's tension is excluded), so holding a pull longer than
the surrounding structure takes to relax latches the converged idle state mid-drag. The engine
then stops evaluating while the pointer keeps moving: relaxation freezes, the user can pull a
bond apart with no internal resistance, and on release the dissociated geometry either fails SCF
(latching the failure phase, which release does not clear) or exposes the divergence between the
GUI's static bond records and xtb's distance-only reality. The canonical spec already requires
the opposite ("while an atom is being dragged, the backend SHALL evaluate … so the pulled
structure relaxes responsively"); no requirement currently pins which behavior wins while a
drag is in flight.

## What Changes

- **Drag-active sessions do not idle**: the converged and evaluation-cap latches are deferred
  while `m_draggedIndex >= 0`; the engine keeps evaluating at interactive accuracy for the whole
  drag so the free atoms keep relaxing responsively. Release restores the existing latch
  semantics (a genuinely converged system converges immediately after release).
- **Release-at-dissociation recovery**: the release re-baseline gets one bounded retry at
  reduced accuracy / more SCF iterations before latching a failed evaluation, and the failure
  message names the recovery path (Stop/Start, or an edit that re-establishes the session).
- **Drag-time bond-distance visibility**: while dragging, the HUD reports when the dragged
  atom's nearest bond distance exceeds bonding range, so the GUI/internals divergence (static
  bond record vs. distance-based reality) is visible instead of silent.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities

- `auto-optimize-interaction`: modify the interactive-performance requirement — pin the
  precedence that the converged/cap idle state SHALL NOT engage while a drag is in flight
  (hold-and-pull scenario); modify the failed-evaluation requirement — one bounded release
  re-baseline retry and a recovery-path diagnostic; modify the write-back/drag requirement —
  HUD reports dragged-bond distance beyond bonding range during the drag.

## Impact

- `libavogadro/src/xtb/xtbengine.{h,cpp}` — latch conditions gated on drag state; release
  re-baseline retry; drag-distance diagnostic signal or state for the HUD.
- `libavogadro/src/tools/autoopttool.{h,cpp}` — HUD bond-distance readout during drags; failure
  message wording with recovery path.
- Tests: `libavogadro/tests/xtbenginetest.cpp` (mid-drag no-idle, cap-deferral, release retry),
  `libavogadro/tests/autooptreprotest.cpp` (hold-and-pull keeps relaxing, HUD diagnostic).
- No changes to `lbfgs.{h,cpp}`, the xtb C API shim, the force-field backend, or the installer.
