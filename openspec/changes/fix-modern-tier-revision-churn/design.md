## Context

The worker path's result lifecycle: `renderModern()` submits a build for revision R
(`submittedRevision = R`, deduplicated); `builderResultReady` receives the result tagged
with its generation and **discards it if `generation < molecule->revision()`**; adoption in
`renderModern()` requires `readyRevision == revision` exactly. `Molecule::revision` bumps
unconditionally on every `setAtomPos` (AutoOpt ticks: N atoms × 20 ticks/s; drags: every
mouse move). Whenever the inter-bump gap is shorter than one build, every result is stale
on arrival — discarded, resubmitted, discarded — and presentation freezes at the last
adopted revision until the churn stops. This is starvation by design: the gate encodes
"adopt only exact current state", which under sustained churn is an unreachable target.

A previous attempt at relief (builder-pool submission coalescing) was reverted
(5ec623fff) because it broke `builderpooltest` — whose expectations codify the
exact-revision contract. This change therefore *changes the contract* (and the test)
rather than re-attempting coalescing.

The second lag source: `AutoOptTool::finished()` writes the force field's coordinates over
every atom, including the dragged one. The dragged atom is fix-markered (the optimizer
never moves it), so its force-field coordinate equals the tick's snapshot position — up to
one tick stale, pre-drag. Writing it back teleports the atom backward; the subsequent
`translate(...)` re-applies only the cursor delta from the stale anchor, so the atom
chases the pointer at tick rate (user-reported: "the mouse will be on the new location and
the atom will be there much later").

## Goals / Non-Goals

**Goals:**
- Adoption that makes forward progress under any churn rate; latency bounded by one build.
- No visual regressions: never adopt content older than what is presented.
- No dragged-atom regression from tick write-back.
- `builderpooltest` re-contracted and green; the no-blank-frame scenarios keep passing.

**Non-Goals:**
- Submission coalescing (reverted once; unnecessary once adoption makes forward progress).
- Debouncing or slowing AutoOpt ticks; changing tick period.
- The pick hitbox and mutation-notification fixes (sibling
  `fix-modern-tier-bond-interaction`); the push-lock crash fix (sibling
  `fix-autoopt-push-lock-race`).

## Decisions

1. **Forward-progress adoption.** Track `presentedRevision` = generation of the last
   *adopted* result. `builderResultReady` adopts when `generation > presentedRevision`
   (instead of requiring `generation == molecule->revision()`), discarding only results
   older than presented. After adoption (or discard), if `molecule->revision() !=
   submittedRevision`, resubmit the newest. Consequences: under churn the display trails
   by exactly one build (the spec's sanctioned latency) instead of unboundedly; when churn
   stops, the in-flight build for the newest revision adopts and the display catches up.
   Monotonicity prevents oscillation (a stale result can never overwrite newer presented
   content).
2. **Contract update in `builderpooltest`.** Replace discard-on-newer expectations with:
   (a) results newer than presented are adopted; (b) results older than presented are
   discarded; (c) after adoption with a newer live revision, a resubmission is requested.
   This supersedes 5ec623fff by fixing the contract rather than the mechanism.
3. **Dragged-atom write-back skip.** `finished()` skips `setPos` for `m_clickedAtom` while
   `m_leftButtonPressed` is active (the drag path owns its position; the force field
   cannot move it anyway — it is fix-markered). Non-dragged atoms unchanged. On release,
   coverage resumes from the drag's final position.
4. **Extras (task-level):** unify tool-overlay draw ordering across the emission and
   worker-adopt paths (halos/blend order differs subtly between the two today) — low
   priority, same render path.
5. **Bond pick name encoding (task 3.4, discovered during live 3.1 verification):** bond
   hit names were the raw half-segment instance index (2 instances per bond at order 1,
   2×order for multi-bonds), which `bond(index)` mis-resolved — bond clicks silently
   no-opped or mutated the wrong bond. `BondInstance` now carries `pickId` (the bond's
   primitive index, emitted by the SceneBuilder alongside its packed pick-target
   arrays); the cylinder pick shader returns it as the hit name. Consumers (order
   cycling, bond deletion) are unchanged — names simply resolve to the clicked bond now.

## Risks / Trade-offs

- [One-build-latency geometry during churn] → Sanctioned by the spec's existing "latency
  is acceptable" clause and now *bounded*, which the exact gate did not guarantee.
- [Adopted-but-stale content during heavy churn] → Strictly better than the status quo
  (permanently stale); bounded by one build.
- [Interaction with `fix-modern-tier-bond-interaction`] → Complementary: that change makes
  order clicks bump the revision; this change ensures the resulting build is actually
  adopted during AutoOpt. Either order lands; verify the 4-row matrix after both.

## Verification (maps to spec scenarios)

Manual (defaults; AutoOpt via the AutoOpt tool):

1. Row 4 of the experiment matrix: AutoOpt running, click a bond — the double bond appears
   within ~one build while ticks continue (was: never until Stop).
2. Drag an atom while AutoOpt runs — the atom tracks the pointer with bounded trail, no
   backward jumps (write-war gone); other atoms keep optimizing smoothly.
3. Continuous stroke drawing during AutoOpt — molecule continuously visible, no blinking
   (existing no-blank scenarios still hold).
4. Rotation during rebuild — retained scene still tracks the camera.
5. `ctest --test-dir build --output-on-failure` green, including the re-contracted
   `builderpooltest` and `autooptreprotest`.
