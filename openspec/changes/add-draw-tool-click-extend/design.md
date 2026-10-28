# Design: DrawTool click-to-extend

## Context

Current gesture flow (`libavogadro/src/tools/drawtool.cpp`):

```
press   atom hit  → if (element != selected) alchemy-mutate (line ~221);
                    else NOTHING (no-op) ← slot for click-extend
        bond hit  → cycle bond order
        no hit    → new atom at cursor (SetIgnoreAtom marker set)
move    drag from atom → bond to cursor / bond onto existing atom
release left btn  → assembles UndoSequence from m_beginAtomAdded /
                    m_endAtomAdded / m_bond (lines ~460–570)
        right/ctrl → delete hit primitive
```

Constraints and facts that shape the design:

- `DrawTool::m_movedSinceButtonPressed` exists but is only ever set `false`
  (dead). `BondCentrictool.cpp:439` has the working pattern:
  `delta.manhattanLength() > 2 → m_movedSinceButtonPressed = true`.
- Hydrogens in this app are **explicit atoms** managed by the
  adjust-hydrogens bookkeeping (`AdjustHydrogens` in drawcommand.cpp), not
  implicit valence.
- `Atom::OBAtom()` returns a **copy** with no parent and no bonds
  (`atom.h:221–229`); `Molecule::OBMol()` likewise returns a copy. There is no
  live OB graph to query cheaply.
- A click-extend must satisfy `auto-optimize-interaction`: no constraint
  markers may survive the gesture; new atoms must be free for the optimizer.

## Goals / Non-Goals

**Goals:**
- Chemically plausible, deterministic placement for the click-extend atom.
- Zero behavior change for every existing gesture (alchemy click, drag, bond
  click, right-click delete, empty-space click).
- Reuse the existing release-path command assembly, including the full
  adjust-hydrogens choreography, so undo/redo parity is free.
- Headless unit-testable geometry.

**Non-Goals:**
- Camera-aware placement (choosing among symmetry-equivalent directions for
  on-screen visibility). A running AutoOpt session relaxes placement within a
  tick; the static case only needs to be sane.
- Conformation search beyond the single anti/stagger reference (D4).
- Fixing `OBBuilder::GetNewBondVector` upstream or building an OBMol
  round-trip per click.
- Any rendering, picking, or camera change.

## Decisions

### D1: Own the placement math — do not call `OBBuilder::GetNewBondVector`

Evidence from the bundled submodule (`extern/openbabel/src/builder.cpp`):

1. It dispatches on `GetExplicitDegree()`, written for **implicit-H**
   molecules. With Avogadro's explicit H's the common targets misroute:
   - ethane terminal C (deg 4, hyb 3) → falls through every case to
     *"Undefined case -- return a random vector"* (~line 695)
   - ethylene C (deg 3, hyb 2) → same random fallback
   - acetylene C (deg 2, hyb 1) → the degree-2 block never assigns `newbond`
     and returns at *"if newbond was not set, it will become non-finite
     here"* (~line 558) — **NaN coordinates**
2. Even routed correctly, it needs a live OB graph; on Avogadro's copied
   `OBAtom` the first statement `((OBMol*)atom->GetParent())->GetDimension()`
   dereferences a null parent.

Alternative considered: per-click `Molecule::OBMol()` copy + atom lookup —
rejected (allocation + perception per click, and cases (1) still misroute).

### D2: Pure placement helper, camera-free

New files `libavogadro/src/tools/placement.{h,cpp}`, compiled into the
drawtool plugin module (add to `drawtool_SRCS` in
`libavogadro/src/tools/CMakeLists.txt:30`). Sketch of the contract (header
comment should carry the angle table):

```cpp
enum class SketchHyb { Sp, Sp2, Sp3 };

// Returns the world position for a new substituent bonded at `origin`.
// heavyNeighbors: positions of the clicked atom's NON-HYDROGEN neighbors.
// preferredDir: optional direction used only by degenerate fallbacks
//               (zero vector = use +X). Deterministic in all inputs.
Eigen::Vector3d placeSubstituent(
    const Eigen::Vector3d &origin,
    const std::vector<Eigen::Vector3d> &heavyNeighbors,
    SketchHyb hyb, double bondLength,
    const Eigen::Vector3d &preferredDir = Eigen::Vector3d::Zero());
```

Angles: Sp → 180°, Sp2 → 120°, Sp3 → 109.471°. With dᵢ = normalize(pᵢ −
origin) (clicked atom → neighbor i), bᵢ = normalize(dᵢ):

| heavy nbrs | placement |
|---|---|
| 0 | n̂ = preferredDir, else (1,0,0) |
| 1 | u = normalize(origin − p₁); pick a reference substituent of the neighbor (heavy first, else any neighbor incl. H — first in iteration order); v = cross(u, ref − p₁), degenerate → cross(u, first non-parallel cardinal axis); v̂₂ = normalize(cross(u, normalize(v))); **n̂ = normalize(u − v̂₂·tan(π − θ))** (the formula from builder.cpp's degree-1 branch; θ=180° reduces to n̂ = u) |
| 2 | Sp2/Sp: n̂ = normalize(b₁ + b₂) (bisector; degenerate → cross(b₁,b₂)); Sp3: bis = normalize(b₁+b₂), v̂ = normalize(cross(b₁,b₂)), **n̂ = normalize(v̂ + bis·(√2/2))** — exactly 109.47° to both when the existing angle is tetrahedral |
| 3+ | n̂ = −normalize(Σ bᵢ) (the remaining tetrahedral vertex); norm < 1e−6 → preferredDir, else +X |

Then `pos = origin + n̂ · bondLength`. Every normalize is guarded (norm <
1e−6 → the documented fallback): finite and deterministic by construction.

### D3: Hybridization heuristic over Avogadro's own bond list

```
any bond order ≥ 3              → Sp
two or more order-2 bonds       → Sp
exactly one order-2 bond        → Sp2
otherwise                       → Sp3
```

Rationale: reproduces organic geometry (ethane sp3 / ethylene sp2 / acetylene
sp / allene center sp); aromatic rings in this app carry kekulé orders (1 or
2), so sp2 falls out. Bond orders ≥ 3 (triple bonds and the
literature-established metal–metal quadruple bonds, settable via the `$`
key, e.g. Cr–Cr, Re–Re, Mo–Mo) map to Sp — a linear extension, which is the
correct default for metal–metal multiple bonds. Alternatives rejected: OB's `AssignHyb` via the copied
OBAtom (undefined parent semantics, hidden perception cost); force-field
lookup (overkill — AutoOpt relaxes geometry anyway).

### D4: Bond length from covalent radii; stagger reference from the neighbor

- Length = `OBElements::GetCovalentRad(A) + OBElements::GetCovalentRad(B)`
  (stateless; precedent `bsdyengine.cpp:95`). If either radius is 0
  (unknown element) → 1.5 Å.
- The degree-1 perpendicular reference (D2 table) makes chains come out
  **anti/staggered** — the neighbor's substituents (its H's, explicit here,
  are legitimate references, exactly as OB uses them) provide the reference
  direction. Which equivalent H is chosen only affects the conformation, and
  AutoOpt relaxes it; take the first in `Atom::neighbors()` order for
  determinism.

### D5: DrawTool integration — synthesize the drag end-state at release

Smallest possible diff; the existing release machinery stays the single
command assembler:

1. **Press** (`mousePressEvent`, atom-hit branch): keep the alchemy branch
   untouched; add an `else` recording `m_clickExtendCandidate = true` (new
   member). No mutation, no force-field marker. Reset
   `m_clickExtendCandidate = false` unconditionally at press, next to
   `m_movedSinceButtonPressed = false` (line ~200) and AFTER the
   `if (m_beginAtom || m_endAtom)` in-flight-stroke guard: `mouseReleaseEvent`
   has early returns that skip its cleanup block (right-click on a managed H,
   X–H bond-order restore), and `toolActivated()`'s reset block is guarded by
   `if (m_beginAtom || m_endAtom || m_bond)` — so release/switch-time resets
   alone can leak a stale candidate into a later alchemy click and synthesize
   an unwanted extension. Press-time reset makes the flag self-healing.
2. **Move** (`mouseMoveEvent`): add the BondCentricTool threshold at the top:
   `if ((event->position() - m_initialDraggingPosition).manhattanLength() > 2)
   m_movedSinceButtonPressed = true;` — activates the dead member, changes no
   drag behavior.
3. **Release** (`mouseReleaseEvent`, left-button branch, BEFORE the
   `if(m_beginAtomAdded || m_bond)` assembly): if `m_clickExtendCandidate &&
   !m_movedSinceButtonPressed && m_beginAtom` valid, and the hydrogen guard
   passes (not extending *from* H when adjust hydrogens is on):
   - compute `heavyNeighbors` of `m_beginAtom` (skip H's),
     `hyb = D3(m_beginAtom)`, `len = D4(begin->atomicNumber(), m_element)`;
   - `m_endAtom = addAtom()`-equivalent: `molecule->addAtom()`, `setPos(placeSubstituent(...))`,
     `setAtomicNumber(m_element)`; `m_endAtomAdded = true`;
   - `m_bond = addBond(molecule, m_beginAtom, m_endAtom)` (honors the
     selected `m_bondOrder`).
   The existing assembly then runs unchanged and produces the
   `UndoSequence { endAtomDrawCommand, bondCommand }` with the full
   adjust-hydrogens options computed exactly as for drag-created bonds.
4. **Cleanup**: reset `m_clickExtendCandidate` alongside the other members in
   `mouseReleaseEvent`, `toolActivated()`, and the destructor path.
5. **Tooltip** (constructor, `action->setToolTip`): document all three
   gestures, e.g. "Left Mouse: Click an atom to extend the molecule (or
   change its element); Click and Drag to draw bonds. Right Mouse: Delete
   Atom."

### D6: No force-field markers for the click-extend path

Pressing on an existing atom sets no `SetIgnoreAtom`/`SetFixAtom`; the
release path unconditionally unsets both. So a click-extend introduces no new
concurrency surface with AutoOpt — the new atom is created free, and the
per-instance constraint-state rules (`bundled-openbabel`) are untouched.
Verify counts stay 0/0 in the AutoOpt-coexistence check.

### D7: Guard every force-field access (pre-existing bug)

`m_forceField` is null-checked in the destructor and `toolActivated()` but
dereferenced unguarded in `mousePressEvent` (SetIgnoreAtom, ~lines 266, 424),
`mouseMoveEvent` (~337, 365–366), and `mouseReleaseEvent` (~647–648). The
`FindForceField` chain legitimately returns null (and does in test binaries —
`autooptreprotest` never sets `BABEL_DATADIR`). Fix: guard every call site
with `if (m_forceField)`; marker bookkeeping is skipped when there is no
force field. No behavior change when a force field is loaded.

### D8: One cleanup path for the release, lock-failure included (pre-existing bug)

Two structural defects in `mouseReleaseEvent`:

1. Early returns skip the cleanup block: the right-click delete branch
   returns early for managed hydrogens and X–H bonds (~652, ~664), and the
   `m_prevBond` X–H restore returns early (~596). Stroke state
   (`m_beginAtom`/`m_bond`/flags) survives → next press swallowed by the
   in-flight guard → following release assembles commands from stale state.
   Fix: restructure so every exit reaches the common cleanup (compute the
   undo command, then always run the reset + marker unsets before returning).
   The semantics of each early return (no deletion, order restored to 1) are
   unchanged; only the cleanup is added.

2. The `tryLockForWrite()` failure path returns before cleanup. Under AutoOpt
   the worker holds the lock briefly per tick; losing the race at release
   leaves the gesture half-finished with dangling state. Fix: on lock
   failure, still release the force-field markers and null the stroke
   pointers (marker/pointer cleanup touches no molecule state, so no lock is
   needed) and return 0 — the gesture aborts. Accepted trade-off: molecule
   changes already applied at press/move remain without an undo command —
   strictly better than today's dangling-state wedge, and unchanged from
   today's behavior in that respect.

This work lands BEFORE the click-extend synthesis (task 3.4) so the new code
is added to an already-hygienic release path.

## Risks / Trade-offs

- [Regression in alchemy/drag] → The same-element branch only adds a flag;
  the `element != m_element` mutation path and all move logic are untouched.
  Manual matrix covers click-N-with-C, drag-from-C, drag-onto-N.
- [Synthesis ordering] → The synthesized state must be built before the
  assembly `if`, otherwise release produces a "change element" no-op command.
  Guarded by the undo unit of the manual matrix (single undo step).
- [Atom invalidated between press and release (AutoOpt deletes it)] →
  Validate `m_beginAtom` against the molecule at release
  (`molecule->atomById(m_beginAtom->id())` non-null) before synthesizing;
  same exposure pattern the existing paths accept.
- [Hypervalent clicks (saturated heavy atoms, e.g. methane with
  adjust-hydrogens OFF)] → Placement still returns a direction (0-heavy-nbr →
  +X; 3-heavy-nbr → 4th vertex); chemistry stays the user's problem, exactly
  as dragging today.
- [Degenerate geometry (perfectly symmetric sums → zero vector)] → Every
  normalize guarded with the cardinal-axis / preferredDir / +X fallback
  ladder; covered by the determinism/finiteness unit test.
- [Which H becomes the stagger reference is arbitrary] → Accepted:
  conformation-level difference only, AutoOpt relaxes it; determinism comes
  from iteration order.
- [Release-path restructure changes delete/restore exits] → Every path now
  ends in the common cleanup; the observable command semantics of each path
  are unchanged. Manual matrix covers right-click delete and bond-order
  clicks; `drawcommand`/`addremovehydrogens` tests guard the commands.
- [Lock-failure abort drops the gesture without undo] → Accepted (see D8):
  strictly better than today's dangling-state wedge; the orphan atom without
  undo is pre-existing behavior on that path.

## Migration Plan

Single self-contained feature; no data or settings migration. Rollback =
revert the commit.

## Open Questions

None material. (Camera-aware direction choice among symmetry-equivalent
placements is a deliberate non-goal, revisitable later without spec change.)
