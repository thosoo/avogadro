## Why

Atoms sometimes become permanently frozen during AutoOpt: the optimizer will not move them and the user cannot drag them. The frozen state survives stopping/starting AutoOpt and is only cleared by restarting the application. Root cause: OpenBabel stores force-field constraint state (`OBFFConstraints _constraints`, `_fixAtom`, `_ignoreAtom`) as **process-global statics** shared by every `OBForceField` instance, and nothing in Avogadro ever cleans them. Drawing with the draw tool while AutoOpt runs mutates this shared state underneath the running optimizer (missing unsets, stale indices after atom deletion, and an `AddIgnore` race that permanently flags atoms with transiently-unset atomic numbers).

## What Changes

- **OpenBabel fork (root fix, delivered via pin bump):** make force-field constraint state per-instance instead of `static` — `OBFFConstraints`, `_fixAtom`, and `_ignoreAtom` become instance members so concurrent force-field users (draw tool on the GUI thread, AutoOpt worker thread, extensions) can no longer contaminate each other.
- **Avogadro AutoOpt hygiene (defense regardless of OB build):** AutoOpt starts each session from a clean constraint state; the per-tick `AddIgnore` accumulation for dummy atoms is replaced by a rebuild on topology change; `abort()`/`disable()` reset the fix/ignore atom; the temporary drag fix is still unset on every exit path.
- **Avogadro draw tool hygiene:** unset the fix/ignore atom statics when the tool deactivates mid-stroke, not only on mouse release.
- **Discoverability:** AutoOpt paints an overlay on atoms the optimizer will not move (fixed/ignored), and the HUD reports fixed vs. ignored counts separately (the current "Num Constraints" counts ignore entries and grows unboundedly).
- Drawing while AutoOpt runs remains a supported workflow — the fix makes it safe rather than disabling it.

## Capabilities

### New Capabilities
- `auto-optimize-interaction`: AutoOpt tool interaction semantics — which atoms the optimizer moves, which atoms the user can drag, how the session lifecycle and concurrent editing affect frozen/constrained state, and visibility of constrained atoms.

### Modified Capabilities
- `bundled-openbabel`: new requirement that force-field constraint state is per-instance (no process-global statics), acquired through the existing submodule pin workflow (gitlink-only pin bump, fork-native changes, clean-tree rules unchanged).

## Impact

- `extern/openbabel` (pinned fork `thosoo/openbabel`): `include/openbabel/forcefield.h`, `src/forcefield.cpp` — de-static three members; audit confirms Avogadro callers (autoopttool, drawtool, forcefieldextension, constraintsdialog) each consistently use their own instance and none relies on cross-instance sharing.
- `libavogadro/src/tools/autoopttool.cpp/.h`: session lifecycle constraint reset, ignore rebuild, drag-fix cleanup, paint overlay/HUD.
- `libavogadro/src/tools/drawtool.cpp`: unset statics on tool deactivation.
- `libavogadro/src/extensions/forcefieldextension.cpp`, `constraintsdialog.cpp`: unaffected behaviorally (per-instance state now local to their own sessions).
- CI: no workflow changes; pin bump flows through the existing submodule cache key.
