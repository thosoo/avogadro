# Design

## Context

The user's cdb capture (commit `ac487391e`, reporting machine) faults on the GUI
thread in `drawtool.dll`: `cmp dword ptr [rax+38h], 1` with `rax = 0` — an
`isHydrogen()` / `atomicNumber` read at offset 0x38 of a null `Atom`. Static
trace:

- `mousePressEvent` line 236: `molecule->atomById(bond->beginAtomId())->isHydrogen()`
  — `Molecule::atomById` returns **nullptr** for an id without a live atom.
- `mouseReleaseEvent` line 567: the same chain via `m_prevBond`, a raw `Bond*`
  held across events — use-after-free when the bond was deleted (undo,
  hydrogen-adjustment churn) before the deref.

The tool holds `m_beginAtom`, `m_endAtom`, `m_prevBond`, `m_bond` as raw
pointers across events, while drawing churns the molecule (H adjustment at
release deletes/re-adds hydrogens; undo removes strokes). Tier-independent:
reproduces on every rendering tier; orthogonal to the shipped
`fix-shared-gl-contexts` fix and to `fix-modern-tier-revision-churn`.

## Goals / Non-Goals

**Goals:**
- No dereference of atoms/bonds absent from the live molecule.
- Interrupted strokes degrade to "abandoned state" instead of crashing.
- Contained to `drawtool.cpp`/`drawtool.h`.

**Non-Goals:**
- Reworking hydrogen adjustment or the undo stack.
- The revision-churn display work (separate active change).
- The black-ball render artifact (separate investigation; no code change yet).

## Decisions

1. **IDs, not pointers, for cross-event state.** Replace the `Atom*`/`Bond*`
   members (`m_beginAtom`, `m_endAtom`, `m_prevBond`, `m_bond`) with id members
   (`m_beginAtomId`, …). Ids are assigned while the referenced object certainly
   exists (at assignment time the deref is safe); every later use resolves via
   `molecule->atomById(id)` / `bondById(id)` with a null check. A null
   resolution resets that state member and abandons the interrupted stroke.

2. **Null-check every `atomById`/`bondById` chain.** The two crash sites plus an
   audit of all remaining `atomById(...)->` / `bondById(...)->` chains in
   `drawtool.cpp` gain null guards; missing objects take the same
   "abandon state" path (no partial stroke bookkeeping).

3. **No signal-based invalidation.** Connecting the tool to
   `primitiveRemoved`-style signals was rejected: it adds lifetime coupling to
   every mutation path for state that one resolution-time check already makes
   safe. Resolution at use is the smaller, complete mechanism.

## Risks / Trade-offs

- [Behavior change: strokes silently abandoned after undo] → the alternative
  (today) is a crash; abandoning matches the tool's existing reset-on-release
  semantics and is invisible in normal drawing.
- [Member type change ripples through drawtool.cpp] → contained to one file;
  the members are used only inside the tool.

## Migration Plan

Single commit; no data migration. Local build + full ctest green, both CI legs
green, then the user-machine repro (draw/undo loop with adjust-hydrogens on)
run without a fault. Rollback = revert.

## Open Questions

- None. The faulting instruction, offset, and two concrete defect sites are
  established; the fix is mechanical.
