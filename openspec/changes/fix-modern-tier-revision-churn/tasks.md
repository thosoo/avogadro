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

      VERIFIED LIVE (autooptreprotest `liveDragAtomDuringAutoOpt`, real GL on this
      machine's display): QTest mouse press-drag-release through the active AutoOptTool;
      the grab engages through the real GL pick, the atom tracks the pointer monotonically
      (max sampled backward jump < 5 px), non-dragged atoms keep optimizing during the
      drag, and the dragged atom resumes optimizing after release.

## 3. Scenario verification

- [x] 3.1 Bond order during AutoOpt: click a bond mid-session — the double bond appears
      within ~one build while ticks continue (needs `fix-modern-tier-bond-interaction`'s
      revision bump to have produced the rebuild; verify the pair together if both are
      applied). Verify: experiment matrix row 4.

      VERIFIED LIVE (autooptreprotest `liveBondOrderClickDuringAutoOpt`, real GL on this
      machine's display): with AutoOpt running, a real mouse click on a bond cycles its
      order 1→2 through DrawTool + the undo stack, `molecule->update()` bumps the
      revision, the modern tier adopts a rebuild at/after the click revision
      (`presentedRevision >= clickRevision`) within the bound, and the tick stream
      continues afterwards. The live run exposed a blocking defect in the companion pick
      mechanism (bond hit names were raw half-segment instance indices, so most bond
      clicks resolved to the wrong bond or to null); fixed in task 3.4 below.
- [x] 3.4 Bond pick name encoding: bond instances carry their bond's primitive index
      (`BondInstance::pickId`, emitted by the SceneBuilder alongside the existing packed
      pick-target arrays); the cylinder pick shader returns it as the hit name instead of
      the raw half-segment instance index, so a click on any segment of a bond resolves
      to that bond (order cycling, bond deletion). Previously the name was the instance
      index (2 per bond at order 1, 2×order for multi-bonds), which `bond(index)`
      mis-resolved — clicks silently no-opped or mutated the wrong bond. Verify:
      `modernpicktest` green; live bond-order click resolves the clicked bond
      (`liveBondOrderClickDuringAutoOpt`).
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
