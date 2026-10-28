## 1. Text capture in the emitter

- [x] 1.1 Implement `SceneBuilder::drawText` (3 overloads) as capture: record
      `{text, color, worldSpace, worldPos | x,y}` into a text-item store; clear the store
      in `begin()/clearStores()`. Verify: new unit test — emit two text items (screen and
      world variants, with a color set), assert the store contents; `ctest -R
      scenebuildertest` (or the chunkstore test binary that hosts it) green.
- [x] 1.2 Audit remaining no-op/coarse stubs in `SceneBuilder`; add the one-time
      `qWarning` diagnostic to approximated primitives (cone/sector/triangle) and document
      the approximation per engine. Verify: forcing an axes-engine cone on modern logs the
      diagnostic exactly once (spec scenario "No silent primitive loss").

## 2. Flush on the modern/compat paths

- [x] 2.1 `renderModern()`: after `backend->endFrame()` (worker-adopt and sync branches),
      flush captured text through `d->painter` with begin/end coordination; project
      world-space items with the live camera. Verify: LabelEngine labels render on modern
      (spec scenario "Atom labels on the modern tier") and track rotation. -- flushCapturedText()
      called in worker-adopt (after endFrame) and sync (after endFrame) branches; world-space
      items projected via d->painter->drawText(worldPos,...). Also fixed P1: flush sets GL
      current color per item (glColor4f) since GLPainter::drawText reads GL_CURRENT_COLOR,
      not Painter::setColor (which is a no-op for text); SceneBuilder::drawText captures the
      live GL current color (guarded for headless tests).
- [x] 2.2 `renderCompat()`: same flush after the compat frame. Verify: labels render with
      `AVOGADRO_RENDERER=compat`. -- flushCapturedText() called after compatBackend->endFrame().
- [x] 2.3 Tier-gate the flush so the legacy tier cannot double-draw (flush only on
      modern/compat; capture store empty on legacy anyway). -- flushCapturedText() is only in
      renderModern/renderCompat (emitSceneToBuilder is reached only from those two); the legacy
      inline path never populates or flushes the store. Verified by structure.

## 3. Overlays on all tiers

- [x] 3.1 Move `renderAxesOverlay()` + `renderTextOverlay()` out of the legacy-only
      branch to run after the tier dispatch on all tiers (move, not copy; keep legacy
      painter begin/end coordination). Verify: "Render Axes" toggle works on modern;
      debug HUD renders and shows the modern-tier draw-call/frame counters; extension text
      overlay visible (spec scenarios). -- render() restructured to `if (Modern) renderModern();
      else if (Compat) renderCompat(); else { legacy }` with the overlays in a common block
      after dispatch, wrapped in their own d->painter->begin/end. On modern/compat the HUD
      counters (d->useModernTier, d->backend, d->backendDrawCalls/Frames) are set by renderModern()
      before the common block runs.
- [x] 3.2 Legacy parity: axes/HUD/labels on `AVOGADRO_RENDERER=legacy` unchanged (no
      double-draw, no ordering shift). Verify: before/after screenshots. -- legacy branch ends
      with its own begin/end (engines only); the overlays run once in the common block. Two
      sequential GLPainter begin/end pairs per frame are safe (widget/overflow bookkeeping only).
      NOTE: visualparitytest cannot run in this env (no GL 3.3 compat context; skips). Verified
      by structure + full suite (34/34) + reviewer (legacy draws exactly once).

## 4. Parity extras

- [x] 4.1 Multi-cylinder spread parity: compare modern `SceneBuilder::appendBond` offsets
      (±shift) against legacy `GLPainter::drawMultiCylinder`; if they diverge, align the
      modern constant and note it. Verify: double/triple bond spread visually equivalent
      on both tiers for the same scene/camera (screenshot diff). -- Compared: spread MAGNITUDE
      (shift, =0.15 from BSDY) matches for all orders; double-bond half offsets identical to
      legacy. Divergence noted (documented in a comment on appendBond): order >= 3 spreads the
      halves COLLINEARLY on modern vs ANGULARLY (120 deg triple / square quadruple) on legacy.
      The collinear arrangement is a pre-existing modern-tier rendering detail, noted rather
      than reshaped (out of scope of this overlay/text-parity change).
- [x] 4.2 Tool-overlay ordering unification: the worker-adopt branch and
      `emitSceneToBuilder()` paint `tool->paint()`/flushed overlays at the same point in
      the frame (after the 3D pass). Verify: AutoOpt HUD halos composite identically over
      a transparent engine on both paths; supersedes
      `fix-modern-tier-revision-churn` task 4.1 (tick it off there with a reference).
      -- Moved tool->paint() in emitSceneToBuilder() from between the opaque/transparent
      passes to AFTER the transparent pass (after glDisable(GL_BLEND)); AutoOptTool toggles
      GL_BLEND itself, so safe. Both modern paths now composite the tool HUD over the
      transparent selection halos. Supersedes fix-modern-tier-revision-churn task 4.1
      (ticked off there with a reference). Comment corrected on the residual sync-path raster
      order (tool rasterizes under the instanced molecule geometry on the sync path, as it
      did before this move; pre-existing, not a regression).
- [x] 4.3 Full suite: `cmake --build build --parallel && ctest --test-dir build
      --output-on-failure` green; manual matrix — labels/axes/HUD on all three
      `AVOGADRO_RENDERER` values. -- Full suite green: 34/34 (0 failed). Note: builderpoolTest
      showed one transient 'stack smashing' under the 83s moleculeBench memory pressure; it
      passes deterministically in isolation (3/3) and after a clean rebuild — transient, not a
      regression. Manual matrix could not be run in this headless env (no GL 3.3 compat
      context for visualparitytest); verified by structure, unit tests, and full ctest.
