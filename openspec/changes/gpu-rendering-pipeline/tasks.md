## 1. Scene core (tier-agnostic)

- [x] 1.1 Scene snapshot model + chunked stores: new `libavogadro/src/rendering/` — snapshot struct copied from `Molecule` (`molecule.h:845-861`; `Atom::pos()` is a pointer into the LIVE `m_atomPos` vector, copy values — `molecule.h:993-994`); add `Molecule::m_revision` bumped in the mutators listed in design A1; chunked stores for spheres/bond-halves/lines/meshes with dirty ranges; unit-test dirty ranges under append/edit/clear (design.md D1, D2, A1)
- [x] 1.2 `RendererBackend` interface (beginFrame/pushChunk/endFrame + picking) and `SceneBuilder` emitter mirroring the exact `Painter` signatures in design A2 (`painter.h:131-349`); `PainterDevice` unchanged, only `pd->painter()` redirected (A2); verify by compiling a null backend and rendering a test molecule headlessly through the emitter (design.md D1, D4)

## 2. Modern tier backend (D2)

- [x] 2.1 Unit-sphere and unit-cylinder geometry VAOs, per-instance vertex formats (sphere: pos 3f + radius 1f + color 4×u8; bond half: 2×3f + radius + color — design D2), instanced shaders with lighting parity against `initializeGL`'s two-light setup (`glwidget.cpp:538-556`); verify with a single-chunk render test producing visually correct atoms/bonds
- [x] 2.2 Chunked instance-buffer upload with the ≤2 ms/frame time-sliced budget and fences; verify by log/assert that no upload exceeds the budget on a 50k-atom rebuild
- [x] 2.3 LOD zoom bands (2-3 index-buffer detail levels on the same instance data, hysteresis) + per-chunk AABB frustum culling; verify no re-tessellation during rotation at fixed zoom (churn counter stays 0) and culling skips off-screen chunks
- [x] 2.4 Wire `GLWidget::render()` (`glwidget.cpp:810`) to the backend keeping `engineLessThan` pass order (`glwidget.cpp:41`); request GL 3.3 compatibility in `main.cpp:357-365` (non-Apple only); GPU buffers recreated in `initializeGL` (`glwidget.cpp:601`) on context loss; add backend draw-call/frame counters to the debug overlay (`renderTextOverlay`, `computeFramesPerSecond` at `glwidget.cpp:1890`); verify a static 1M-atom synthetic scene stays at a bounded count (design A3; apitrace optional cross-check)

  Done: added `GLWidget::renderModern()` (paints engines into the `SceneBuilder` via a `GLPainterDevice` painter override, keeping opaque→tool→transparent `engineLessThan` order, then hands the finished chunk stores to the `ModernRendererBackend` for a bounded one-draw-per-primitive-type draw); `render()` dispatches to it when `d->useModernTier` (default true on a GL 3.3+ context, with a legacy fallback if the backend fails to build). `initializeGL` creates/rebuilds the backend on the current context (recreated on context loss). `main.cpp` requests a GL 3.3 CompatibilityProfile context (non-Apple). The debug overlay shows draw calls + frames submitted. `modernrenderertest::testDrawCallsBounded` proves a 1M-atom + 1M-bond scene draws exactly 2. Also hardened `ModernRendererBackend`'s context handle to `QPointer<QOpenGLContext>` so a context deleted before the backend (test teardown / widget teardown) nulls out instead of dangling - this fixed an intermittent SIGSEGV in `~ModernRendererBackend()` → `QOpenGLContext::isValid()` that fired ~2/3 of full-suite runs (use-after-free on freed context memory); the backend now skips VBO cleanup when the context is gone (its VBOs are tied to that context and are gone too).

## 3. Engine migration via emitter (D4)

- [x] 3.1 Port BSDY ball-and-stick (`bsdyengine.cpp:132-395` — opaque/transparent/quick/pick variants) to the emitter API (atoms incl. `atomImages()` union, half-bond coloring, multi-bond split via `drawMultiCylinder` order/shift, selection halos); verify side-by-side parity screenshots vs legacy tier for a mixed-element molecule
- [x] 3.2 Port sphere (`sphereengine.cpp`), stick (`stickengine.cpp:72-153`), wire engines; verify parity screenshots per engine and that settings widgets still affect rendering (opacity → transparent pass with per-instance alpha, design A9)
- [x] 3.3 Verify unported engines (labels, hbond, qtaim, …) still render through the fixed-function compatibility bridge alongside modern-tier passes in the same GL 3.3 compatibility context, without corrupting them (design D2, D4)

## 4. Picking and selection (D5, D6)

- [ ] 4.1 Color-ID picking (ID shader over chunk buffers, `(type<<28)|id` encoding, scissored ~16×16 at cursor, delayed PBO read; rubber band = full-rect pass) — MUST synthesize `GLHit(type,name,minZ,maxZ)` (`glhit.h:74`) so all tool consumers stay untouched (`drawtool.cpp:138,221,537`; `selectrotatetool.cpp:126,337,427`; `clickmeasuretool.cpp:84`; `aligntool.cpp:91,138`; `glwidget.cpp:2407-2445`); delete GL_SELECT from the modern tier; verify correct topmost picks in a >100k-atom scene and rubber-band selection beyond the legacy 32k-hit capacity (design A4)
- [ ] 4.2 Overflow detection in the GL_SELECT path (`glRenderMode(GL_RENDER)` returning −1 → report/refine instead of silent truncation at the `global.h:116` cap) for compat/legacy tiers; verify a forced-tier rubber-band selection exceeding capacity no longer mis-selects silently (design A4)
- [ ] 4.3 `GLWidgetPrivate::selectedPrimitives` (`glwidget.cpp:249`) → `QSet<Primitive*>`, keep the public `PrimitiveList` API; empty-set early-out at the per-primitive check sites (`bsdyengine.cpp:216`, sphere/cartoon selection loops); verify select-all on a large scene changes frame rate by ~0% (design A5)

## 5. Mesh streaming (D2)

- [ ] 5.1 Stream `Mesh` to VBOs once when `stable()` flips (hook `Mesh::setStable` `mesh.cpp:60`; add mesh revision bumped in `setVertices/addVertices/setNormals/addColors` `mesh.cpp:87-130`), replacing the per-frame deep copy in `GLPainter::drawMesh/drawColorMesh` (`glpainter_p.cpp:997,1041`); covers callers `cartoonengine.cpp:154,181`, `surfaceengine.cpp:84-123`; verify with an orbital-surface scene that per-frame CPU time is independent of vertex count (design A6)

## 6. Worker threading (D3)

- [ ] 6.1 Builder pool (2-3 threads) consuming (snapshot, dirty-chunks) jobs; snapshots taken under `Molecule::lock()->lockForRead()`; completions via queued signals; GUI uploads ready chunks in `paintGL` under the ≤2 ms budget; patterns to copy: `readfilethread_p.cpp`, `cartoonmeshgenerator`; verify input latency stays under the frame budget while a 1M-atom scene builds (design A7)
- [ ] 6.2 Race/teardown tests: edits during builds (revision check discards stale results), view close during builds (`QPointer`/shared refs), context loss mid-upload (buffers rebuilt from CPU chunks — A3); verify no crashes, no leaked builders, TSAN/ASAN clean

## 7. Tier framework (D7, D9)

- [ ] 7.1 CMake options near `ENABLE_GLSL` (`CMakeLists.txt:441`): `AVO_RENDERER_TIER2/TIER1/LEGACY` (all ON, APPLE forces TIER2 OFF) as definitions on the `avogadro` library target (sources appended in `libavogadro/src/CMakeLists.txt:73+`); startup capability probe (3.3+/2.0+), `AVOGADRO_RENDERER` override before first GLWidget creation, tier+renderer logging via `force-dedicated-gpu` diagnostics; verify on this Linux machine: default = modern, override forces compat/legacy, unsupported+explicit fails with a clear diagnostic (design A8)
- [ ] 7.2 Compat tier = batched fixed-function flush (client arrays/VBO) of the same chunk stores, legacy GL_SELECT picking per 4.2; verify forced-run basics: molecule renders, tools work, picking correct (spec "Compat tier basics"); Tier 0 code frozen — no changes beyond build guards (design D9)

## 8. Validation

- [ ] 8.1 FPS harness (scripted orbit over synthetic 4k/50k/1M scenes, driven through the existing camera/navigate API) reporting avg fps per tier from the debug overlay counters; verify modern-tier targets: 4k ≥60 iGPU-class, 50k ≥60 discrete / ≥30 integrated, 1M ≥30 discrete (spec "Modern-tier frame performance")
- [ ] 8.2 Visual-parity review: side-by-side modern vs legacy (`AVOGADRO_RENDERER` forced runs) for the ported engines on reference molecules — default scene, protein, orbital surface; sign off parity including transparency class (spec "Visual parity for ported engines", design D2)
- [ ] 8.3 Full build + test suite passes locally: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`; legacy tier verified unchanged via forced-tier smoke run (spec "Legacy tier unchanged")
