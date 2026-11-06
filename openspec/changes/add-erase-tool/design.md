## Context

Deletion today exists only as the Draw tool's right-click (one atom or bond per
click, `DeleteAtomDrawCommand` / `DeleteBondDrawCommand` returned from
`mouseReleaseEvent`, GLWidget pushes them onto the undo stack). Tools are
self-contained plugin classes in `libavogadro/src/tools/` registered via the
`AVOGADRO_TOOL` macro; the Draw tool already demonstrates live-mutation +
command-wrapping, gesture-state cleanup on tool switch, and `tryLockForWrite`
abort-on-contention at release. Tool overlays render through `Tool::paint()` on
all tiers (established by the gpu-rendering-pipeline spec). See proposal.md for
motivation; behavior contract is in specs/erase-tool-interaction/spec.md.

## Goals / Non-Goals

**Goals:**
- Lasso deletion as a single self-contained tool, zero changes to existing tools
  or undo-command classes.
- One undo step per lasso, with nothing mutated before release.
- Interaction that stays correct when a running AutoOpt session is ticking.
- Touch-readiness: the tool consumes ordinary mouse events only, so the future
  touch-synthesis layer can drive it without tool-side changes.

**Non-Goals:**
- No sweep-erase (erase-as-you-cross). The lasso's release-time batch delete is
  the whole interaction.
- No lasso *selection* (that is the select tool's concern), no bond-cutting by
  loop crossing, no keyboard shortcut in this change (F7 reserved).
- No changes to Draw tool right-click behavior or to molecule-sketching specs.

## Decisions

**D1 — Lasso over sweep or click-delete.** Alternatives: (a) click/tap delete —
rejected as the primary gesture because accidental single clicks deleting atoms
is the worst failure mode on touch and a regression risk on desktop; (b)
sweep-erase (delete atoms the pointer crosses, live) — rejected because live
mid-gesture mutation forces an "already applied" mode in the delete commands
(~15 lines, new idiom for deletion), path interpolation so fast sweeps don't
skip atoms, and heavier interrupted-gesture cleanup. Lasso defers *all* work to
release: nothing mutates before release, undo grouping is free (children
execute on push), tap-safety is enforced by a minimum-path guard rather than
slop heuristics, and containment is insensitive to pointer speed.

**D2 — Containment in screen space at release.** Record the pointer path
in device pixels (the same coordinate space the scaled mouse events and the
camera's projection helpers use, per the hidpi-render-coordinates invariant).
At release: project every atom position through the camera, bbox-cull against
the loop's bounding box, then even-odd point-in-polygon on survivors. No
`GL_SELECT` picking on the erase path at all — this avoids the selection-buffer
overflow concern on large molecules entirely and keeps the tool independent of
pick-slop tuning. Alternative (picking-based sweep hit tests) rejected with D1.

**D3 — Undo via an `UndoSequence` of existing delete commands returned at
release.** During the drag the tool only records the polyline. At release it
constructs one `DeleteAtomDrawCommand` per victim (which snapshots id, element,
position, bond context before any mutation) inside an `UndoSequence`, returns
the sequence; GLWidget pushes it, and the children's `redo()` performs the
deletions — the exact flow the Draw tool's right-click uses today. No new
command classes, no "already applied" mode needed. Undo text on the sequence
("Lasso Delete") with children carrying their existing texts.

**D4 — Minimum-path guard makes taps inert.** A gesture whose polyline travel
is below a small threshold (comparable to the click-vs-drag thresholds the other
tools use) produces no command, no undo entry, no mutation. This is the
enforcement of "a tap never deletes" — a guard at release, not an input-slop
heuristic.

**D5 — Managed-hydrogen parity by reuse.** Victims are determined in atom-id
space; when adjust-hydrogens is enabled, managed hydrogens are excluded from the
victim set exactly as the Draw tool's delete excludes them, and heavy-atom
victims pass the same `adjustHydrogens` flag to `DeleteAtomDrawCommand` so
hydrogen adjustment behaves identically. One policy, two consumers.

**D6 — Lock discipline mirrors the Draw tool release.** At release the tool
acquires the molecule write lock with `tryLockForWrite`; on failure it aborts
the gesture cleanly (discard polyline, no commands, no state) — the same shape
as the Draw tool's lock-contention abort. Deletions happen inside the lock
(via the pushed command's redo during undo-stack push on the GUI thread).
During a running AutoOpt session this serializes with worker reads; successful
deletions leave no fix/ignore markers behind on survivors.

**D7 — Overlay through `Tool::paint()`.** The in-progress loop renders as a
screen-space polyline from the recorded points, following the precedent of the
bond-centric tool's overlay; the existing tool-paint contract guarantees it
renders on modern/compat/legacy tiers without painter diagnostics. The overlay
draws read-only state (the polyline, optionally highlighted enclosed atoms)
never mutated molecule data, consistent with "nothing mutates during the drag".

**D8 — Registration and gesture-state scoping.** New tool class in
`libavogadro/src/tools/` with `AVOGADRO_TOOL` metadata, eraser icon, no
shortcut; palette placement near the Draw tool. The tool connects to
`toolActivated` (same lazy-`UniqueConnection` pattern as the Draw tool) to
abandon an in-flight loop when another tool activates mid-gesture, and treats a
null molecule / changed document as an abandon. Every exit path resets the
polyline.

**D9 — MSVC/portability.** Plain Qt6 widget-less tool code following the
existing tools' patterns; no threading primitives, no aggregate-init of
user-special-member types, no new dependencies — low Windows risk, but the
change still rides the normal two-workflow CI gate.

## Risks / Trade-offs

- [Loop with very many vertices × many atoms point-in-polygon cost at release]
  → bbox cull first; polygon test is integer ray-casting; cost is microseconds
  for editor-scale molecules, and it runs once per gesture, not per frame.
- [Atoms move under AutoOpt while the loop is drawn; user intent may be
  ambiguous] → spec pins containment to release-time positions (what the user
  sees at release is what deletes). Undo makes any surprise cheap.
- [Projection space mismatch on HiDPI] → record path in device pixels and use
  the camera's own projection helpers; the hidpi spec already pins camera
  helpers and rendered-frame agreement.
- [Screen-space loop vs 3D depth] → containment is intentionally 2D: it deletes
  everything visually enclosed, including atoms behind each other. This matches
  graphics-editor expectations and the rubber-band select's existing semantics.
- [Icon artwork] → ship a serviceable placeholder eraser PNG; polish is not
  load-bearing for behavior.

## Migration Plan

Purely additive: new tool files + one CMakeLists entry + icon resource. Rollback
is reverting the commit. No settings, file-format, or API surface changes; tools
not activated by default beyond appearing in the palette.

## Open Questions

- Whether a loop that *crosses* a bond should cut the bond (delete bond, keep
  both atoms) — deferred; current contract: bonds follow atoms only.
- Exact palette ordering value (`usefulness()`) relative to Draw — cosmetic,
  settle during implementation.
