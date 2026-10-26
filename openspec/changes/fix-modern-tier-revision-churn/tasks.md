## 1. Forward-progress adoption

- [x] 1.1 Add `presentedRevision` tracking to `GLWidgetPrivate`; change
      `builderResultReady` to adopt when `generation > presentedRevision` (discard only
      older-than-presented) and request a resubmission when
      `molecule->revision() != submittedRevision` after adoption or discard. Verify: unit
      — pump result generations interleaved with revision bumps; adoption is monotonic and
      no result newer than presented is discarded.
- [x] 1.2 Keep `renderModern()`'s present path consistent with the new tracking (adopt
      branch uses `readyRevision >= presentedRevision` semantics; `drawLastReady` still
      re-presents between builds). Verify: no blank frames across a churn-storm test.
- [x] 1.3 Re-contract `builderpooltest`: replace exact-revision/discard-on-newer
      expectations with forward-progress + bounded-latency assertions (design D2).
      Verify: `ctest -R builderpooltest` green with the new contract.

## 2. Dragged-atom write-back

- [x] 2.1 `AutoOptTool::finished()`: skip `setPos` for `m_clickedAtom` while
      `m_leftButtonPressed`; resume coverage on release. Verify: headless — tick write-back
      with an active drag marker does not move the grabbed atom; other atoms update.
- [x] 2.2 Manual: drag an atom during AutoOpt — atom tracks the pointer with a bounded
      trail, no backward tick-sized jumps; other atoms keep optimizing. Verify: spec
      scenarios "Pull an atom while the optimization runs" and "Other atoms keep updating".

## 3. Scenario verification

- [x] 3.1 Bond order during AutoOpt: click a bond mid-session — the double bond appears
      within ~one build while ticks continue (needs `fix-modern-tier-bond-interaction`'s
      revision bump to have produced the rebuild; verify the pair together if both are
      applied). Verify: experiment matrix row 4.
- [x] 3.2 Sustained-stroke and rotation regressions: continuous drawing during AutoOpt and
      camera rotation during rebuilds — no blank frames, rotation tracks. Verify: existing
      no-blank-frames scenarios.
- [x] 3.3 Full suite: `cmake --build build --parallel && ctest --test-dir build
      --output-on-failure` green.

## 4. Extras

- [x] 4.1 Tool-overlay draw-order consistency: `renderModern()`'s worker-adopt branch
      paints `tool->paint()` after `endFrame()` while `emitSceneToBuilder()` paints it
      between opaque and transparent passes; unify the ordering (or document why the
      difference is intentional). Verify: halo/blend ordering identical on both paths for
      the AutoOpt HUD halos over a transparent engine.

      RESOLVED by `fix-modern-tier-overlay-parity` (task 4.2): `emitSceneToBuilder()`
      now paints `tool->paint()` AFTER the transparent pass (i.e. after the full 3D
      pass), matching the worker-adopt branch which paints it after `endFrame()`. Both
      paths therefore composite the AutoOpt HUD halos on top of the molecule selection
      halos. See `libavogadro/src/glwidget.cpp` `emitSceneToBuilder()`.
