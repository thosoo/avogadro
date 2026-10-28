## Context

Frame flow on the modern tier (worker path), as implemented today:

```
molecule change (setPos/addAtom/... → d->revision++)
  → renderModern(): sceneChanged → submitWorkerBuild(revision)
  → worker tessellates (≥1 frame) → builderResultReady (queued)
       └─ DISCARDED if generation < current revision   ← under continuous
                                                          editing, nearly
                                                          every result
  → frame during in-flight build: drawLastReady()
       └─ beginFrame() ZEROES m_sphereCount/m_bondCount/m_lineCount
       └─ endFrame() → drawInstances() draws "if (count > 0)" → NOTHING
```

So every in-flight frame is presented cleared (`paintGL` line ~752 clears
color+depth first; the background is dark, perceived as black). Under
AutoOpt's 50 ms tick this alternates blank/scene at the tick rate; under a
drawing stroke, `Atom::setPos` → `Molecule::setAtomPos` bumps the revision on
every mouse move, results are perpetually discarded, and the display vanishes
for the whole stroke. The pipeline spec's own "last ready state" clause
(gpu-rendering-pipeline, GUI-thread requirement) is violated by the
implementation.

Key facts that shape the fix:

- The VBO contents survive in GPU memory across frames; only the *counts*
  ("draw N instances") are lost, because `beginFrame()` (modernbackend.cpp
  ~line 152-180) zeroes them and `drawLastReady()` (glwidget.cpp:1236) calls
  begin+end with no pushes in between.
- The stale-instance watermark ("a smaller scene after a larger one must not
  draw stale instances") is *already* enforced where it belongs: at push time
  (`pushSpheres`/`pushBonds` set `m_*Count = store.size()`; pushLines sets 0
  on empty). `beginFrame`'s zeroing adds nothing except breaking retention.
- `drawInstances()` early-returns without `m_haveMvp`
  (`modernbackend.h:207`); `drawLastReady()` never sets the MVP, so retention
  also needs the current camera transform per frame.
- Worker results arrive from a 2-thread pool and can arrive out of order;
  `builderResultReady`'s `generation < revision` discard (glwidget.cpp
  ~1222) is doing real ordering work and must not be simply removed.
- `renderModern()` bails on `tryLockForRead()` failure with a bare return
  (~line 968) — a second cleared-frame path with the same cure.

Landmines observed while investigating — recorded so nobody "cleans them up"
opportunistically, all out of scope here:

- `ensureCapacity()`'s comment claims "re-upload prior contents"; no code
  does. Masked today because worker results are always fully dirty and the
  budget accepts everything. Do not implement incremental dirty tracking in
  this change.
- `ChunkedUploader`'s cost estimate (`kDefaultBytesPerMicros = 4.0e6`) makes
  the 2 ms budget accept ~GB per frame — the design-D2/D3 budget is
  unenforced fiction. Leave as is.
- `fenceAfterUpload()` uses `glGetSynciv` + `glWaitSync`; `glWaitSync` does
  not block the CPU (`glClientWaitSync` would). Within a single GL context,
  in-order execution makes the whole fence dance unnecessary anyway. Leave
  as is.
- Pick readback does one `glReadPixels` per hit pixel for depth (up to 64
  round-trips per mouse move) — a latency smell, but behaviorally correct
  and NOT part of the flicker. Leave as is.

## Goals / Non-Goals

**Goals:**
- A frame with a rebuild in flight re-presents the retained geometry (counts
  persist), tracking the current camera (MVP + modelview set per frame).
- The lock-fail path re-presents instead of clearing.
- No spec-visible behavior change when nothing is in flight.
- Optional: stop the builder queue from accumulating jobs that will be
  discarded on arrival.

**Non-Goals:**
- Not removing the revision discard from `builderResultReady` (it orders
  out-of-order worker results; retention makes it harmless).
- Not fixing upload budgeting, fences, pick latency, mesh streaming, or the
  sync/worker `tool->paint` asymmetry (documented above).
- Not touching tools, OpenBabel, or `fix-autoopt-frozen-atoms` territory
  (disjoint files).
- Not making the AutoOpt HUD persist on retained frames (see Risks).

## Decisions

1. **Counts persist across frames; watermark stays at push time.**
   Delete the three zeroing lines from `beginFrame()` and replace the
   "Per-frame state reset" comment with the retention contract: counts are
   owned by the last push; `beginFrame` only resets the upload budget
   bookkeeping and `m_drawCallsThisFrame`. Zero the counts explicitly in
   `initialize()` (fresh/context-lost backend: new VBOs are empty, nothing
   may draw until the first adopt pushes). Also note in the comment why
   draw-last-ready is then safe (VBO contents are GPU-resident and unchanged
   by begin/end without pushes).
   *Alternative considered:* an explicit `drawRetained()` backend method —
   rejected: more API for exactly what begin+end already do once counts
   persist.
2. **`drawLastReady()` sets the current camera before presenting.**
   Between `beginFrame()` and `endFrame()`, call
   `d->backend->setModelViewProjection(currentModelViewProjection())` and,
   once `fix-modern-tier-lighting-space` lands (or in the same commit if it
   landed first), `d->backend->setModelView(...)`. Reading the GL matrix
   stacks is safe there — `paintGL` established them before `render()` ran,
   and no molecule lock is needed for GL state. Without this, a retained
   frame during camera rotation freezes the picture at the old pose.
3. **Lock-fail path presents retained.**
   In `renderModern()`, on `tryLockForRead()` failure, do not bare-return:
   `beginFrame()` → `setModelViewProjection(currentModelViewProjection())`
   (plus `setModelView` as above) → `endFrame()` → return. Do NOT touch
   `emittedRevision`/`storesValid`/`enginesDirty` — nothing was adopted.
4. **Keep `builderResultReady`'s generation guard; optionally coalesce the
   queue.** The guard orders a 2-worker pool. The optional coalescing lives
   in `BuilderPool::submit()`: under the mutex, before appending, drop any
   queued job whose `generation` is older than the incoming job's
   (`m_queue.erase` over jobs with `job.generation < generation`), so
   sustained churn does not build doomed snapshots. `m_pending` bookkeeping
   must be decremented for each dropped job. Keep the worker-side code
   untouched.
5. **HUD absence on retained frames is accepted.** `tool->paint()` is not
   called on retained frames (in the adopt branch it is; in
   `drawLastReady()` it is not), so during sustained churn the AutoOpt HUD
   text may flicker. Do NOT call `tool->paint()` from `drawLastReady()`:
   AutoOpt's paint calls into the force field (`Energy()`) and mutates the
   molecule (`setEnergy`) — that must not happen on an arbitrary retained
   frame, and `fix-autoopt-frozen-atoms` section 5 owns that path anyway.

## Risks / Trade-offs

- [Count persistence draws stale instances after a scene shrink] → already
  handled at push time (`m_*Count = store.size()`); the spec scenario
  "delete atoms mid-run" verifies no ghost geometry.
- [First frame after context loss draws garbage from a zeroed-count
  VBO] → `initialize()` zeroes counts explicitly; nothing draws until the
  first push. Verify by forcing a context-loss rebuild (resize stress) if
  practical.
- [Retained-frame MVP reads GL stacks outside a molecule lock] → GL state,
  not molecule data; the same reads already happen unconditionally in the
  adopt/sync paths.
- [Shared files with the sibling change] → see
  `fix-modern-tier-lighting-space` design.md merge-order note; regions are
  disjoint (`beginFrame`/`initialize`/`drawLastReady`/lock-fail vs.
  shaders/`drawInstances` uniform sets).
- [Blank frames from other sources may still exist (e.g. compat tier, or
  `emitSceneToBuilder` early-outs)] → out of scope; this change covers the
  modern worker path, which is where the reported flicker lives. If flicker
  persists after this change on `AVOGADRO_RENDERER=compat`, file separately.

## Verification (maps to spec scenarios)

Manual, built app, `AVOGADRO_RENDERER` unset:

1. AutoOpt no-blink: start AutoOpt on a small molecule, watch ≥ 30 s across
   600+ ticks — molecule continuously visible (spec scenario 1; this was the
   reported "flickering during autoopt").
2. Stroke visibility: with AutoOpt running, drag a continuous drawing stroke
   for several minutes — molecule never vanishes; lag is acceptable (spec
   scenario 2; the reported "flickering when drawing").
3. Rotation during in-flight rebuild: rotate while AutoOpt ticks — the
   picture rotates smoothly with the camera, never freezing at an old pose
   (spec scenario 3).
4. Delete-while-run: right-click delete atoms mid-run — no ghost/stale
   geometry appears beyond the expected one-rebuild lag.
5. Lock-fail path: code-review the path (hard to trigger deterministically);
   optionally stress with the Optimize Geometry extension (its thread takes
   write locks) while rotating — no blank flashes.
6. 20× start/stop and pendingUploads: `pendingUploads()` returns 0 after
   churn settles (it should always have been 0 given the permissive budget;
   this guards the count-persistence change against masking real deferrals).
7. Automated: `ctest --test-dir build --output-on-failure` full pass.

## Migration Plan

Single commit; no pin, config, or workflow changes. Rollback = revert. Safe
to run in parallel with `fix-autoopt-frozen-atoms` (disjoint files:
glwidget.cpp/modernbackend.cpp vs. tools + fork) — coordinate only the
manual verification matrix (the active change's design D steps are much
easier to judge once this lands).

## Open Questions

- None blocking.
