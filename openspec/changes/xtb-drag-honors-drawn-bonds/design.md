## Context

The xTB session worker (`XtbEngine`) owns a client-side L-BFGS optimizer (`Avogadro::Xtb::LBfgs`)
that calls `xtb_singlepoint()` per evaluation inside `evaluateOnce()`; after each singlepoint the
dragged atom's gradient is zeroed so the optimizer treats the pointer-pinned atom as fixed. The
worker re-reads the live molecule every tick under a bounded-retry read lock
(`readLiveMolecule()`), copies the dragged atom's pointer-authored position into the working
coordinates, and masks the atom from write-back. Drag lifecycle flags flow GUI→worker through
`setDraggedAtom()`; a press wakes the worker immediately (`resumeOptimizationLocked()` sets the
tick-pending flag).

Today nothing opposes a pull: the partners only feel whatever the SCF's Hamiltonian produces, and at
dissociation distance that force vanishes — release then starts L-BFGS from the dissociated basin.
The force-field backend's drag path never had this problem: its harmonic bond terms pull the
neighborhood along and always retract at release. See proposal.md for motivation and
specs/auto-optimize-interaction/spec.md for the behavior contract.

## Goals / Non-Goals

**Goals:**
- One mechanism, worker-side only, that makes every xTB-backend drag bond-preserving: partners are
  continuously pulled toward the drawn bonds while the drag lasts.
- Zero GUI-thread and zero tool changes; drag authority, write-back masking, strain report,
  elevated release, and failure containment keep their current roles.
- Deterministic tether targets captured from the live molecule at press time.

**Non-Goals:**
- No healing of bonds broken by means other than a drag (file input, external edits, pre-leash
  sessions) — except the repair cap that engages when a dragged bond is *already* broken at press.
- No rigid clamp/leash on the pointer target (explicitly rejected: drag authority must stay exact).
- No changes to the OpenBabel force-field path, bond perception, or the molecule's bond records.
- No user-visible settings for tether stiffness (a named constant first; a hidden setting only if
  feel demands it).

## Decisions

**D1 — External tether added to the session's own energy/gradient (not the xTB constraint API).**
`evaluateOnce()` already assembles E and ∇ per trial coordinate set for the L-BFGS line search, and
already post-processes the gradient (dragged-atom zeroing). The tether is pure arithmetic there:
for each incident bond with partner *p* and target *d0*,
`E += ½k(|r_di − r_p| − d0)²`, `∇_p += k(|r_di − r_p| − d0)·(r_p − r_di)/|r_di − r_p|`.
E and ∇ stay mutually consistent (valid L-BFGS curvature pairs); the dragged atom's gradient is
zeroed anyway, so the tether acts purely on partners. *Alternative rejected:* the xtb C API's
constraint functions (narrow coverage, per-call mutation of shared calculator state, harder to scope
to a drag window, and an API-availability risk across library builds).

**D2 — Worker-side capture at drag start, via the existing live-molecule read.** The worker detects
the drag-start transition (drag index changed from −1 to ≥0 between ticks) and, in the same tick's
`readLiveMolecule()` pass, also copies the incident bond list under the same bounded-retry read
lock (the thread-confinement requirement already sanctions worker-side topology scans). Targets
`d0` are the current bond lengths at press converted to bohr. *Alternatives rejected:* GUI-side
capture at press (would add a new GUI→worker state channel; the tool currently ships only the index)
and per-tick re-capture (targets would drift as the partner chases).

**D3 — Target policy: drawn length, with a repair cap.** `d0 = press length` normally (the bond as
it stands when grabbed is the user's intent). When the press length exceeds the strain threshold
(1.3× the covalent-radii sum — the same convention `bondStrainText` uses), `d0` is capped at the
covalent-radii sum, so grabbing a previously broken bond pulls it back together rather than holding
the broken distance. Degenerate input (zero-length bond at press, invalid element) skips that bond's
tether.

**D4 — Force constant as a named constant, default 1000 kJ/mol/Å² (≈1.36 Ha/bohr²).** Competitive
with typical xTB restoring forces near moderate stretch without dominating the reduced-accuracy
interactive SCF (drag ticks run at `kXtbInteractiveAccuracy`/bounded iterations already). Lives in
the `kXtb*` constants block; units converted once with the existing `kAngstromToBohr`/Hartree
factors. Tuning is expected; the constant is the single knob.

**D5 — Tether lifecycle mirrors the drag and the bond graph.** Tether state (partner indices +
bohr targets) is worker-owned: captured on drag start, cleared on drag end (`setDraggedAtom(-1)`),
cleared and re-captured on topology re-establishment (`reestablishForTopology` — the bond graph may
have changed), cleared on quiesce and on `setup()`. The re-establishment path already rebuilds
exactly once per topology change, which the "no stale tether" scenario requires.

**D6 — Convergence and idle semantics unchanged.** The tether contributes to partner gradients, so
the masked gradient norm cannot reach the convergence threshold "for free" during a held pull; the
existing rules stay authoritative: converged idle is deferred while dragging, the evaluation cap is
refreshed by pointer movement, and failed-evaluation containment latches regardless of drag.

## Risks / Trade-offs

- **Flick-release transients can still latch a failure.** A violent flick released instantly can
  leave the partner mid-chase at a distance whose SCF fails even at the elevated release budget →
  the existing D4 containment latches with the named recovery path. Accepted: containment semantics
  are unchanged and the tether makes this rare, not impossible.
- **Chase latency on slow backends** — the neighborhood follows at evaluation pace, so a fast pull
  looks like viscous taffy on large molecules. The interaction-latency language of the interactive
  performance requirement already bounds this ("bounded by the evaluation cost").
- **Stiffness feel** — too weak lets flicks stretch bonds dramatically; too strong fights the
  interactive SCF/L-BFGS line search. One named constant + manual check on a small molecule; adjust
  before merge if feel is wrong.
- **Ring/cage deformation** — a tether inside a strained ring pulls two partners; the molecule
  resists and the bond partially stretches instead of the ring imploding. Physics-like, but the
  manual check must eyeball a ring system.
- **Extra xTB dependency surface** — covalent radii for the repair cap come from the OpenBabel
  element table already linked into libavogadro; no new link dependency, but the include must stay
  MSVC-clean (no aggregate initialization in the new state; plain vectors).

## Migration Plan

Single PR against master; no settings or data migration; rollback = revert the commit. The change
touches only the xTB worker, which is exercised by the existing offscreen CI legs (Linux ctest +
Windows installer ctest); the Windows installer workflow verifies MSVC compatibility before merge.

## Open Questions

None blocking. (Tether stiffness is expected to be tuned once during implementation against the
manual feel check; the constant is the designed knob and does not change the spec or the task
breakdown.)
