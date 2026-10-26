# Why

Drawing molecules with the Draw tool crashes the application after a variable
number of strokes. The user's cdb capture (commit `ac487391e`, Quadro P3200)
faults on the GUI thread inside `drawtool.dll`:

```
(4c08.6d14): Access violation - code c0000005 (first chance)
drawtool!qt_plugin_query_metadata_v2+0x17ff:
83783801  cmp dword ptr [rax+38h], 1   ds:00000000`00000038=????????
```

`cmp dword ptr [rax+38h], 1` with `rax = 0` is a null-atom hydrogen check:
`isHydrogen()` reads `atomicNumber` at offset 0x38 of a null `Atom`. The static
trail lands on two unguarded dereference chains in `drawtool.cpp`:

- `mousePressEvent` (line 236):
  `molecule->atomById(bond->beginAtomId())->isHydrogen()` — `atomById` returns
  **nullptr** for an id whose atom no longer exists (deleted by undo, or removed
  by hydrogen adjustment at a previous stroke's release).
- `mouseReleaseEvent` (line 567):
  `molecule->atomById(m_prevBond->beginAtomId())->isHydrogen()` — dereferences
  `m_prevBond`, a raw `Bond*` kept across events; if the bond was deleted (undo
  or hydrogen churn) this is a use-after-free before the null deref.

The tool holds `m_beginAtom`, `m_endAtom`, `m_prevBond` and `m_bond` as raw
pointers across events and never validates them against the live molecule, while
drawing deletes and re-adds atoms constantly (hydrogen adjustment at stroke
release, undo). Any deletion between two events leaves a dangling reference; the
next event dereferences it. Tier-independent — the crash exists on every
rendering tier; the modern tier is not implicated.

# What Changes

- **Null-safe atom/bond resolution in the Draw tool.** Every `atomById(...)`
  / `bondById(...)` result used by the Draw tool is null-checked; a missing
  atom/bond is treated as "state abandoned" (the stroke resets) rather than
  dereferenced.
- **ID-based tool state.** The cross-event tool state (`m_beginAtom`,
  `m_endAtom`, `m_prevBond`, `m_bond`) switches from raw pointers to ids
  (`unsigned int`), resolved through the live molecule at use time and reset
  when resolution fails — so structural churn (undo, hydrogen adjustment)
  between events can no longer leave a dangling reference.

# Capabilities

### New Capabilities

- `draw-tool-interaction`: the Draw tool's cross-event state SHALL survive
  (or gracefully abandon) structural mutations that happen between mouse
  events — undo, hydrogen adjustment, external deletions — without crashing.

# Impact

- `libavogadro/src/tools/drawtool.cpp` (and `.h` if state members change type) —
  the only file in scope.
- No rendering-tier changes; the bug reproduces on every tier.
- Separate from the still-active `fix-modern-tier-revision-churn` change: that
  change covers revision-churn display stability (lag, adoption), not this
  crash.

# Verification

- Local: build + full ctest green on Linux (`-j8`, default platform).
- Targeted: the existing autoopt/selection tool tests stay green; a manual
  draw-undo-redraw loop (with "adjust hydrogens" on) must not crash.
- Windows: both CI legs green on the fix commit; user-machine check — repeat
  the original draw/undo sequence that crashed without hitting the fault.
