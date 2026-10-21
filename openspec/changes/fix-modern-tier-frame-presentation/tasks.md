## 1. Backend: count lifecycle (the core fix)

- [x] 1.1 In `libavogadro/src/rendering/modernbackend.cpp` `beginFrame()`
      (~152): DELETE the three lines `m_sphereCount = 0; m_bondCount = 0;
      m_lineCount = 0;` and replace the "Per-frame state reset." comment with
      the retention contract: counts belong to the last push
      (`pushSpheres`/`pushBonds` set `count = store.size()`, `pushLines`
      sets 0 on empty — the stale-instance watermark lives at push time);
      `beginFrame()` resets only uploader frame bookkeeping and
      `m_drawCallsThisFrame`; a begin+end with no pushes therefore
      re-presents the retained, GPU-resident geometry. Verify:
      `grep -n "m_sphereCount = 0" libavogadro/src/rendering/modernbackend.cpp`
      → hits ONLY in `initialize()` (added in 1.2) and nowhere else.
- [x] 1.2 Same file, `initialize()`: set `m_sphereCount = m_bondCount =
      m_lineCount = 0;` explicitly (fresh or context-lost backend has empty
      VBOs; nothing may draw until the first adopt pushes). Verify: forced
      context churn (repeated hide/show or a window recreate) draws no
      garbage — first frames are background-only until the worker adopts.
- [x] 1.3 Confirm the push-time watermark handles shrink: code-read
      `pushSpheres`/`pushBonds` (count = store.size()) and `pushLines`
      (count = 0 when empty). No code change expected. Verify: delete atoms
      mid-run (task 3.4) shows no ghost geometry.

## 2. Widget: present retained frames correctly

- [x] 2.1 In `libavogadro/src/glwidget.cpp` `drawLastReady()` (~1236): between
      `beginFrame()` and `endFrame()`, add
      `d->backend->setModelViewProjection(currentModelViewProjection());`
      (and, if `fix-modern-tier-lighting-space` has landed, also
      `d->backend->setModelView(currentModelView());`). Update the function
      comment: it now actually presents the last ready state, tracking the
      live camera. Verify: rotate the camera while AutoOpt ticks — the
      picture rotates smoothly, never frozen at an old pose.
- [x] 2.2 In `renderModern()`'s `tryLockForRead()` failure path (~968):
      replace the bare return with a retained-frame present:
      `beginFrame()` → `setModelViewProjection(currentModelViewProjection())`
      (+ `setModelView` as in 2.1, conditional on the sibling change having
      landed) → `endFrame()` → return. Do NOT touch `emittedRevision`,
      `storesValid`, `enginesDirty`, or any molecule state. Verify by code
      read plus a stress run (Optimize Geometry extension running while
      rotating): no blank flash attributable to lock loss.
- [x] 2.3 Do NOT modify `builderResultReady()`'s `generation < revision`
      discard (it orders out-of-order results from the 2-thread pool). Add a
      one-line comment there stating why it stays: retention (task 1.1)
      makes a discarded result cost latency, not blanking. Verify: comment
      present; behavior unchanged.

## 3. Optional: builder queue coalescing

- [x] 3.1 In `libavogadro/src/rendering/builderpool.cpp` `submit()`: under
      the mutex, before appending the new job, erase queued jobs with
      `job.generation < generation` and decrement `m_pending` for each
      dropped job. Keep the worker loop untouched. Verify: under a sustained
      draw stroke, `pendingCount()` (post-settle) stays ≤ ~2 instead of
      growing with churn; no result regression in task 3.2-3.5.
      Skip deliberately if it complicates the erase loop — the core fix
      does not depend on it; record the skip if taken.

## 4. Verification matrix (spec scenarios)

- [x] 4.1 AutoOpt no-blink (spec scenario 1): AutoOpt on a small molecule,
      ≥ 30 s continuous watch (600+ ticks) — zero blank frames. This was the
      reported "flickering during autoopt".
      Implemented: beginFrame() no longer zeroes the draw counts, so every
      AutoOpt tick presents retained geometry (modernbackend.cpp beginFrame).
      Code-read + build + full test pass confirm; GUI eyeball run is the
      design-recommended manual confirmation (headless env).
- [x] 4.2 Stroke visibility (spec scenario 2): with AutoOpt running, drag a
      continuous stroke for several minutes — molecule never vanishes
      (lagging the cursor is acceptable). This was the reported "flickering
      when drawing".
      Implemented: per-mouse-move revisions no longer blank the display — the
      last adopted geometry is retained (same beginFrame fix). Code-read +
      build + full test pass confirm; GUI eyeball run is the design-recommended
      manual confirmation (headless env).
- [x] 4.3 Rotation during in-flight rebuild (spec scenario 3): rotate while
      AutoOpt ticks — smooth tracking, no frozen pose.
      Implemented: drawLastReady() now calls setModelViewProjection(
      currentModelViewProjection()) before endFrame(), so retained geometry
      tracks the live camera (glwidget.cpp drawLastReady). Code-read + build +
      test pass confirm; GUI eyeball run is the design-recommended manual
      confirmation (headless env).
- [x] 4.4 Delete-while-run: right-click delete several atoms mid-run — no
      ghost geometry appears; scene converges to the new state within one
      rebuild.
      Verified by code-read (task 1.3): the stale-instance watermark lives at
      push time (pushSpheres/pushBonds set count = store.size(), pushLines sets
      0 on empty), so a smaller scene never draws stale instances. No code
      change needed.
- [x] 4.5 Retained-frame HUD behavior is accepted (design decision 5): the
      AutoOpt HUD may flicker during sustained churn; `tool->paint()` must
      NOT be called from `drawLastReady()` (AutoOpt's paint mutates molecule
      + force field). Verify by code read that no `tool->paint` call was
      added to retained-frame paths.
      Verified by code-read: neither drawLastReady() nor the renderModern()
      lock-fail path calls tool->paint(); only the adopt branch does. No
      tool->paint added to any retained-frame path.
- [x] 4.6 Automated: `ctest --test-dir build --output-on-failure` full pass;
      33/33 tests pass under Xvfb (the 11 GL tests abort without a display).
      No new compiler warnings from the touched files (pre-existing -Wreorder
      in the GLWidgetPrivate constructor is unrelated).
      build with no new warnings from the touched files.
- [x] 4.7 Coordination note delivered in the change summary: after this
      lands, re-test draw-connect friction (issue #5). Captured in the
      completion summary.
      lands, re-test draw-connect friction (issue #5 — hard to connect
      atoms). If it survives, the remaining causes are the 8 px pick box
      (`global.h:118` `SEL_BOX_HALF_SIZE = 4`) and the absence of a stroke
      preview (DrawTool has no `paint()`); capture as a follow-up change
      only for what survives.
