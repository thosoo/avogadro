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
- One mechanism, worker-side only, that makes every xTB-backend drag preserve the drawn bond
  graph — no drawn bond breaks AND no new bond forms (atom bond partners are invariant during a
  drag) — so an xTB drag feels like a force-field drag: the neighborhood follows the pull
  (taffy), bonds hold, and release retracts.
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
`readLiveMolecule()` pass, also copies the full bond list AND the non-bonded floor-pair candidates
(see D8) under the same bounded-retry read lock (the thread-confinement requirement already
sanctions worker-side topology scans). Tether targets `d0` are the current bond lengths at press
converted to bohr. *Alternatives rejected:* GUI-side capture at press (would add a new GUI→worker
state channel; the tool currently ships only the index) and per-tick re-capture (targets would
drift as the partner chases).

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
bohr targets) and floor state (non-bonded pair indices + floor distances) are worker-owned:
captured on drag start, cleared on drag end (`setDraggedAtom(-1)`), cleared and re-captured on
topology re-establishment (`reestablishForTopology` — the bond graph may have changed, and a new
bond changes the non-bonded set), cleared on quiesce and on `setup()`. The re-establishment path
already rebuilds exactly once per topology change, which the "no stale tether" scenario
requires. The bond-graph projection (D8) has no drag lifecycle: it reads the session's current
numbers/bond graph and enforces the graph on every evaluation for the session lifetime.

**D6 — Convergence and idle semantics unchanged.** The tether contributes to partner gradients, so
the masked gradient norm cannot reach the convergence threshold "for free" during a held pull; the
existing rules stay authoritative: converged idle is deferred while dragging, the evaluation cap is
refreshed by pointer movement, and failed-evaluation containment latches regardless of drag.
(The D9(d) deferred-latch below is the one drag-phase exception: a drag-phase evaluation failure
is surfaced but does not latch until the drag is held fixed on a genuinely unconvergeable
geometry.)

**D7 — Bond-graph invariance: the tether scope extends from the dragged atom's incident bonds to
every drawn bond.** The incident-only tether has a loophole: radial (distance) constraints never
see angle changes, so two hydrogens bonded to the same carbon can swing together to near-bonding
distance by bending the H–C–H angle (~109.5° → ~43°) at *zero tether cost* — the xTB SCF then
forms H₂, splitting off a hydrogen molecule exactly as reported in manual use. During a drag,
every drawn bond therefore gets the same harmonic restraint to its press length (same arithmetic,
same `kXtbTetherForce`); the repair cap (D3) stays scoped to the dragged atom's incident bonds so
the drag does not silently re-typing-relax the rest of the molecule. Away from the drag, press
lengths equal relaxed lengths, so the resting forces are near zero and the molecule keeps its
free-bending feel — only *lengths* become invariant, which is the xTB analog of the force-field
backend's harmonic bond terms and produces the same drag feel (taffy pull, hold, retract).
*Alternative rejected:* hybrid FF-drag (relax with the OpenBabel force field during xTB drags) —
structurally guarantees invariance but changes mid-drag physics, adds an FF typing failure mode
mid-session, and breaks the identical-interaction-per-backend symmetry.

**D8 — Bond-graph projection: the drawn graph is enforced geometrically, not energetically.
** Radial restraints cannot express "these two atoms must never bond": finite walls lose to the
SCF's bond wells under sustained pulling, pairwise capture sets miss pairs, and any drag-scoped,
energy-only mechanism leaves the post-release and no-drag relaxation paths uncovered (each
measured in implementation). The optimizer therefore projects every trial and accepted geometry
onto the drawn-bond-graph manifold before the backend evaluates it (SHAKE-style, a projector
hook on the client-side L-BFGS line search): every drawn bond is clamped into
[0.5, 1.3]× the covalent-radii sum (re-formation reach, the D3 convention) and every
non-bonded pair is pushed out of new-bond range (1.5× the covalent-radii sum, 2.0× for
hydrogen–hydrogen pairs — the H covalent-radius sum badly undershoots the H–H repulsion
wall; the SCF's own H···H equilibrium for a collapsed H–C–H geometry is ~1.0 Å).
Corrections move only non-dragged atoms (drag authority is exact: the pinned atom is never
moved, and a constraint touching it is satisfied entirely by moving the other end). Ten
correction sweeps resolve overlapping constraints (measured: the 6-sweep build left an H–H
pair at ~1.05 Å against the 1.24 Å floor under a sustained ethane hard pull — the bond clamp
that pulls a hydrogen back to reach fights the non-bonded push that keeps it off its neighbor,
so more sweeps are needed to converge the overlap; the cost is trivial next to the SCF). The
projection is deterministic for
identical input and runs for the session lifetime — drags, releases, and plain relaxation all
evaluate projected geometry, so no partner change is reachable through any optimization path.
The O(N²) pair sweep per evaluation is negligible next to the SCF at interactive scales.
*Alternatives rejected:* repulsive floor restraints (measured: equilibrium inside the H₂ well
under sustained drag, transient dips through the wall, and the uncaptured-pair and post-release
loopholes); xtb constraint API (D1); hybrid FF-drag (D7).

**D9 — Drag-phase evaluation resilience.** Two bounded, deterministic hardenings added during
implementation (both measured necessary): (a) the L-BFGS line search caps its first trial-step
displacement norm at 0.25 Å — a quasi-Newton direction accumulated during a stiff repair phase
otherwise proposes trial geometries with near-coincident atoms that the backend cannot evaluate
(measured: "Coulomb evaluator / lattice points" failures); (b) when a drag-phase singlepoint
fails at the steady-state budget, the session retries it once at the release-elevation settings
(full accuracy, 2× iterations) on freshly created xTB objects — the C API cannot clear a fatal
error state (calling the library again on it corrupts its heap), so the retry reuses the D2
destroy/recreate discipline; a retry that also fails takes the D4 containment latch unchanged;
(c) a pointer-authored move of the dragged atom during a latched drag — the user retreating the
pull — arms one bounded recovery attempt: the four xTB objects are recreated (the failed
evaluation can leave the calculator's internal state poisoned, so re-evaluation on them fails
regardless of geometry — user-reported as "restarts failing ... not even pulling close will
fix it"), the latch clears, and the retreated geometry re-evaluates at the elevated budget;
intermediate retreat positions may legitimately re-fail (one attempt per pointer move), and a
retreat that reaches a geometry the backend can converge resumes the session without a
Stop/Start cycle; (d) **deferred-latch during a drag** — the projector's constraint-conflict
residuals (a pinned far drag can leave a bond compressed to `minLen` while another stretches to
`maxLen`, a geometry the SCF sometimes cannot converge at any iteration budget) mean a hard pull
can visit SCF-unconvergeable trial geometries, and the spec scenario forbids a latch from the pull
alone. A drag-phase failure (after the (b) retry also fails) is therefore surfaced but does **not**
latch immediately: the worker counts *consecutive failed drag ticks*, resetting on any successful
tick or a pointer-authored move of the dragged atom, and engages the D4 containment latch only
when the count reaches `kXtbDragFailLatchThreshold` (4) — i.e. only when the drag is held fixed on
a geometry the backend genuinely cannot evaluate (bounded CPU, still idles with a named failure).
Release-path and non-drag failures latch immediately as before (D4 unchanged); the release
elevated evaluation and its containment semantics are untouched.
## Risks / Trade-offs

- **Flick-release transients can still latch a failure.** A violent flick released instantly can
  leave the partner mid-chase at a distance whose SCF fails even at the elevated release budget →
  the existing D4 containment latches with the named recovery path. Accepted: containment semantics
  are unchanged and the tether makes this rare, not impossible.
- **Chase latency on slow backends** — the neighborhood follows at evaluation pace, so a fast pull
  looks like viscous taffy on large molecules. The interaction-latency language of the interactive
  performance requirement already bounds this ("bounded by the evaluation cost").
- **Stiffness feel** — too weak lets flicks stretch bonds dramatically; too strong fights the
  interactive SCF/L-BFGS line search. Two named constants (bond tether D4, floor D8) + manual
  check on a small molecule; adjust before merge if feel is wrong.
- **Floor vs legitimate close contacts** — floors engage only below 1.5× the covalent-radii sum;
  the worst legitimate engagement is a strained-ring 1–3 contact penetrating ~0.01–0.05 Å
  (forces of tens of kJ/(mol·Å)) — negligible deformation, but the manual check must eyeball a
  ring system. A deliberate head-on drag (drag authority is absolute) can compress a pair to the
  projection floor exactly (for H–H ~1.24 Å with the shipped constants; the H₂ minimum is
  0.74 Å) — partial close-contact character, but no H₂ forms and the graph never changes;
  the manual check (5.7) tunes the factors if that feels wrong.
- **Violent flicks can still latch a failure** — containment semantics are unchanged; the
  difference is that the frozen geometry is now a *bonded* geometry (the graph invariant held
  until the latch), so recovery never has to repair a partner swap.
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

None blocking. (Tether and floor stiffness are expected to be tuned once during implementation
against the manual feel check; the constants are the designed knobs and do not change the spec or
the task breakdown.)
