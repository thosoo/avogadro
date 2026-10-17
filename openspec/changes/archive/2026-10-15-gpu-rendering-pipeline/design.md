## Context

Findings from exploration and static analysis (all in `libavogadro/src` unless noted):
per-primitive immediate-mode rendering with per-primitive display lists and ~8-12 GL calls
per atom/bond per frame (`engines/bsdyengine.cpp:132-196`); whole-scene display list
recompiled every dragged frame (`glwidget.cpp:294-314`); per-frame deep copy of mesh
vertices (`glpainter_p.cpp:1016`); O(scene × selection) selection checks
(`glwidget.cpp:2573`); GL_SELECT picking with silent truncation at a 262144-GLuint buffer
cap (`glwidget.cpp:2305-2365`, `global.h:116`); per-frame `new NeighborList` leak in the
hbond engine (`engines/hbondengine.cpp:94`). Painting runs on the GUI thread
(`ENABLE_THREADED_GL` off; `CMakeLists.txt:453`). The app uses Qt6 `QOpenGLWidget`, whose
context is owned by the GUI thread — a true render thread is not practical (see D3).
CI is MSVC-only; `ENABLE_GLSL`/GLEW shader infrastructure exists but is off and
per-engine (`engine.h:186`). Target scenarios: gp23-monomer (~4k atoms), assemblies
(~50k), capsid scale (~1M), orbital-surface meshes.

## Goals / Non-Goals

**Goals:**
- 60 fps target on monomer/assembly scenes, 30 fps at capsid scale (modern tier; numbers
  in the spec).
- GUI thread never blocked by viewport work beyond a small bounded submit/upload budget.
- Long-term seam: a tiered backend architecture the future GPU-driven tier can grow into.
- Old hardware keeps working (compat tier); macOS and panic scenarios keep the frozen
  legacy tier.

**Non-Goals:**
- Implementing Tier 3 now (defined only; D8).
- Vulkan / D3D12 / Metal / Qt RHI migration.
- Fixing Tier 0 (legacy) pathologies — they are documented and accepted; that code is
  frozen.
- macOS on the modern tier (no test device; legacy by default there).
- Modifying the Windows installer or CI toolchain.

## Decisions

### D1: Tier boundary at a backend seam
Shared, tier-agnostic layers: scene snapshot (immutable per molecule revision), chunked
primitive stores with dirty ranges, LOD zoom bands, selection set, diagnostics. Swappable
submit stage behind `RendererBackend` (`beginFrame/pushChunk/endFrame`, plus picking).
Rationale: keeps compat/legacy cheap and makes the tier ladder maintainable. Alternative
considered: one monolithic rewrite (no fallback, all-or-nothing risk) — rejected.

### D2: Modern tier = instanced shaders on GL 3.3 compatibility profile
- Unit-sphere and unit-cylinder geometries in VAOs; per-instance records in chunked
  instance buffers: sphere = pos(3f) + radius(1f) + color(4×u8); bond half = endpoints
  (2×3f) + radius + color. ~1M atoms ≈ 32 MB — trivial for VRAM.
- One draw call per primitive type per frame (plus per-chunk ranges when culled).
- Lighting in the shader mirrors the current two-light fixed-function setup for visual
  parity.
- LOD = 2-3 prebuilt index-buffer detail levels on the same instance data, chosen per
  zoom band with hysteresis; band rebuilds (rare) are prepared by workers. This replaces
  per-primitive dynamic scaling and is strictly higher quality than the current
  octahedron floor.
- Culling: per-chunk AABB vs frustum on the CPU (fine at capsid scale; GPU culling is
  Tier 3).
- Transparency: sorted chunk submission approximating today's back-to-front pass; exact
  per-primitive sorting is out of scope (same visual class as today's display behavior).
- Compatibility profile (not core) is deliberate: unported engines keep using
  fixed-function in the same context during and after migration.

### D3: Workers build, GUI submits (not a render thread)
QOpenGLWidget owns its context on the GUI thread; fighting that (as the off-by-default
`ENABLE_THREADED_GL` did) is fragile. Instead: worker threads hold CPU-side scene
snapshots (taken under the molecule read lock) and tessellate/update chunk buffers;
results are handed to the GUI thread and uploaded with a bounded budget (≤2 ms/frame,
fenced). A frame always presents the last ready state — rebuilds never stall input.
Alternative considered: shared-context render thread producing FBOs — rejected as
QOpenGLWidget-hostile. `ENABLE_THREADED_GL` stays off and is not ported forward.

### D4: Emitter API as the engine migration seam
`SceneBuilder` exposing `drawSphere/drawCylinder/drawMesh/...` with the same signatures as
the existing `GLPainter` calls, so porting an engine is mechanical. Port order: BSDY
(default ball&stick), sphere, stick, wire, then surface + cartoon/ribbon mesh paths.
Unported engines keep calling GLPainter (fixed-function) in the same compatibility
context — mixed pipeline is legal there. Engine `changed()`/invalidation signals already
exist and map to chunk dirty ranges.

### D5: Picking by color-ID pass on the modern tier
Render the same chunk buffers with an ID shader, scissored to a small region around the
cursor; read via delayed PBO (1-frame-old answer is acceptable, matching current latency
class). Rubber band = ID pass over the band rect. GL_SELECT is deleted from the modern
tier; compat/legacy tiers keep it and gain overflow detection (`glRenderMode` returns −1
on overflow → report/refine instead of silent truncation).

### D6: Selection = hash set
`selectedPrimitives` becomes a `QSet<Primitive*>` (O(1) contains) with an empty-set
early-out in render paths. Applies to all tiers (it is tier-agnostic state).

### D7: Tier selection: compile-time inclusion, runtime choice
CMake flags `AVO_RENDERER_TIER2/TIER1/LEGACY` (default ON; macOS forces TIER2 off).
Startup probes the context (3.3+, 2.0+), applies the `AVOGADRO_RENDERER` override, logs
tier + `glGetString` renderer string (slot prepared by `force-dedicated-gpu`). A tier
compiled out or unsupported + explicitly requested → clear startup diagnostic, not silent
fallback.

### D8: Tier 3 defined, gated, not built
GPU-driven tier (GL 4.3+): compute culling, `glMultiDrawElementsIndirect`, persistent
mapped buffers. The only constraint imposed now (enforced in review): a chunk must be a
self-contained, GPU-describable unit (own vertex range/count/draw record); no submit-stage
code may assume CPU-side per-primitive iteration. Growing Tier 3 later is then a new
submit stage plus a cull pass, not a redesign.

### D9: Low-tier policy
Compat tier: batched fixed-function flush of the same chunk stores (client arrays/VBO),
legacy GL_SELECT picking with overflow detection, target "monomer ≥30 fps", bugfix-only
thereafter. Legacy tier: frozen verbatim, incl. documented pathologies; smoke-test only.
Neither receives new features; this policy is what keeps three tiers cheaper than two
rewrites.

## Implementation anchors (source map for apply)

Verified against the current tree; line numbers are anchors, not contracts.

### A1. Molecule data model & snapshot (tasks 1.1, 6.1)

- `molecule.h:845-861`: `m_atomPos` is `std::vector<Vector3d>*` indexed by atom id;
  `m_atomConformers` holds all conformers; `m_bonds` is `std::vector<Bond*>`;
  `m_atomList`/`m_bondList` are `QList`s; `m_lock` is a `QReadWriteLock*`
  (`lock()` at `molecule.h:729`).
- `Atom::pos()` returns `&(*m_atomPos)[id]` (`molecule.h:993-994`) — a pointer into the
  LIVE vector. Snapshots must copy values, never store pointers. `atomicNumber()`
  (`atom.h:148`), `customColorName()` (`atom.h:194`). `Bond`: `beginAtomId()`/`endAtomId()`
  (`bond.h:111,121`), `order()` (`bond.h:155`).
- No revision counter exists. Add `m_revision` bumped in the mutators that already set
  `d->invalidGeomInfo`/emit `updated()` (`molecule.cpp:177,230,249,935-952`, `translate`,
  conformer switch) — the snapshot model keys on it.
- Snapshot = {revision, positions (f32×3), atomicNumbers, bond (begin,end,order), chunk
  assignments}; taken by workers under `lock()->lockForRead()`. The paint path already
  uses `tryLockForRead` (`glwidget.cpp:780-784`); readers coexist, writers are the only
  exclusion — snapshot cost is one O(N) copy, amortized per edit batch.

### A2. Emitter API mirrors Painter exactly (task 1.2)

- Mirror these `Painter` pure-virtuals (`painter.h`): `drawSphere(center,radius)` :131,
  `drawSphere(*center,radius)` :140, `drawCylinder(end1,end2,radius)` :149,
  `drawMultiCylinder(end1,end2,radius,order,shift)` :175, `drawLine` :197,
  `drawMultiLine` :213, `drawMesh(mesh,mode)` :342, `drawColorMesh(mesh,mode)` :349,
  `setName(Primitive*)` :88 / `setName(type,id)` :95, plus `begin/end`, `setColor*`,
  `setDynamicScaling`.
- `SceneBuilder` implements the same signatures but appends to per-primitive-type chunk
  streams. `PainterDevice` (`painterdevice.h:44-49`) stays as-is; in emitter mode only
  `pd->painter()` is redirected, so engines need no signature changes and color/selection
  queries keep working.
- `Engine` integration points (`engine.h`): `renderOpaque/renderTransparent/renderQuick/
  renderPick(PainterDevice*)` :232-299, `layers()` :196, `transparencyDepth()` :306,
  `atoms()/bonds()` :323,336, `m_atomImages` :524, `setMolecule` :507. The
  `changed()` → `GLWidget::addEngine` connections (`glwidget.cpp:2041-2047`) already
  exist and become the chunk-dirty triggers.

### A3. GLWidget paint-path integration (task 2.4)

- Today's sequence in `render()` (`glwidget.cpp:775-905`): `tryLockForRead` →
  `painter->begin` → fog → (`quickRender` ? `dlistQuick` : opaque engines → tool →
  transparent engines) → axes → `renderTextOverlay()` :905 → unlock. New: same skeleton,
  engine loop bodies swap to emitter→backend; `beginFrame(camera, viewport, quality)` /
  `endFrame()` bracket the pass list. Keep `engineLessThan` ordering
  (`glwidget.cpp:41`) — non-overlay first, then `transparencyDepth` — for both opaque
  and transparent pass order.
- Supercell/crystal: `renderCrystal` (`glwidget.cpp:911`) re-calls a display list per
  cell translation; atom images already arrive through `atoms()+atomImages()`
  (`bsdyengine.cpp:183`) — modern tier treats images as plain extra instances. Cell axes
  overlay stays fixed-function until separately ported.
- Context request: extend the `QSurfaceFormat::setDefaultFormat` block
  (`avogadro/src/main.cpp:271-273`) with `setVersion(3,3)` +
  `setProfile(QSurfaceFormat::CompatibilityProfile)` when Tier 2 is compiled in and not
  Apple (Apple keeps the current format → legacy tier). Every GLWidget is created after
  this call (`mainwindow.cpp:3897-3901`, `glgraphicsview.cpp:35,49`;
  `DetachedView` re-parents an existing GLWidget, `detachedview.cpp:35-41`), so no
  per-widget `setFormat` is needed.
- Context loss/teardown: GPU buffers are recreated in `initializeGL`
  (`glwidget.cpp:494`) from the surviving CPU chunk data; hook
  `QOpenGLContext::aboutToBeDestroyed` for GPU-side deletion. Engines/backends are
  per-widget (engines are created per view already), so per-widget GPU buffers are
  correct; cross-view sharing is explicitly out of scope (design Open Questions).
- Verification instrumentation (task 2.4): the backend self-instruments draw calls/frame;
  surface the counters in the debug overlay (`renderTextOverlay`, called from
  `render()` :905; `computeFramesPerSecond` :2742) — apitrace is the optional
  cross-check, not the primary gate.

### A4. Picking (tasks 4.1, 4.2)

- Legacy path: `GLWidget::hits(x,y,w,h)` (`glwidget.cpp:2289-2388`) — GL_SELECT pass
  over enabled engines' `renderPick`, buffer `(numAtoms+numBonds)*8` capped at
  `SEL_BUF_MAX_SIZE` = 262144 (`global.h:116-117`); results parsed into
  `GLHit(type,name,minZ,maxZ)` (`glhit.h:74`), sorted by `minZ` via `operator<`.
- Consumers that MUST keep working unchanged (they parse `QList<GLHit>`):
  `drawtool.cpp:138,221,537`; `selectrotatetool.cpp:126,337,427`;
  `clickmeasuretool.cpp:84`; `aligntool.cpp:91,138`; and
  `computeClickedPrimitive/Atom/Bond` (`glwidget.cpp:2407-2445`). Therefore the ID pass
  synthesizes `GLHit`s with `type = Primitive::AtomType/BondType`, `name = primitive id`,
  `minZ/maxZ` from the depth buffer range — no tool signatures change.
- ID encoding: 32-bit `(type<<28) | id` (ids < 2^28 — enforce with an assert); click =
  ID pass scissored to ~16×16 px at cursor; rubber band = scissored ID pass over the band
  rect (one draw), reading back per-pixel (or reduced-resolution) hits with depth range.
  A 1-frame-old PBO read matches today's latency class.
- Overflow handling on GL_SELECT tiers: `glRenderMode(GL_RENDER)` returning −1 means the
  buffer overflowed — report and refine (split the band / shrink the box), never silently
  truncate (fixes the `global.h:116` cap bug for compat/legacy too).

### A5. Selection (task 4.3)

- `GLWidgetPrivate::selectedPrimitives` is a `QList<Primitive*>`
  (`glwidget.cpp:249`); users: `setSelected`, `toggleSelected` (×2), `clearSelected`,
  `unselectPrimitive`, `isSelected` (`glwidget.cpp:2573`), and `selectedPrimitives()`
  which materializes a list. Swap to `QSet<Primitive*>`; keep the public
  `PrimitiveList` API (rare calls materialize). Named selections store ids separately
  (`glwidget.cpp:250`) — untouched. Add empty-set early-out at the top of every
  per-primitive selection check site (`bsdyengine.cpp:216`, `sphereengine.cpp` selection
  loop, `cartoonengine.cpp:162`).

### A6. Mesh streaming (task 5.1)

- `Mesh` (`mesh.cpp`): CPU `m_vertices/m_normals/m_colors` + `m_stable` (`setStable` :60)
  + per-mesh `QReadWriteLock`. The per-frame deep copy is in `GLPainter::drawMesh`
  (`glpainter_p.cpp:997+`) and `drawColorMesh` (:1041+) — `v = mesh.vertices()` copies
  the whole vector each call.
- Callers: `cartoonengine.cpp:154,181`; `surfaceengine.cpp:84,87,95,98,119,123`;
  ribbon/hair engines similar. `Mesh` has no revision counter — add one bumped in
  `setVertices/addVertices/setNormals/addColors` (`mesh.cpp:87-130`), or key the VBO
  cache on `(Mesh*, revision, stable)`.
- Upload trigger: generators already run on QThreads and signal
  `finished() → engine changed()` (`cartoonengine.cpp:251-252`) — the backend uploads a
  mesh VBO once when `stable() && revision changed`. One-time GUI-thread upload is fine
  (meshes change per generation, not per frame); no worker upload needed for meshes in
  this change.

### A7. Worker threading (tasks 6.1, 6.2)

- Patterns to copy: `ReadFileThread` (`readfilethread_p.cpp` — QThread subclass, ready()
  signal) and `CartoonMeshGenerator` (QThread + `finished()` + deleteLater).
- New: a small builder pool (2-3 threads) consuming (snapshot, dirty-chunk-set) jobs;
  completions arrive via queued signals; the GUI thread uploads ready chunks inside
  `paintGL` under a `QElapsedTimer` budget (≤2 ms/frame, fenced — D2/D3).
- Required race tests: edit during build (snapshot revision checked at job end; stale
  results discarded); view close during build (jobs hold `QPointer`/shared scene refs);
  context loss mid-upload (buffers rebuilt from CPU chunks — A3).

### A8. CMake & tier flags (task 7.1)

- Sources: explicit list in `libavogadro/src/CMakeLists.txt:73+` — add
  `rendering/*.cpp` entries there.
- Targets: library `avogadro` (root `CMakeLists.txt` aliases `avogadro-lib`); app
  `avogadro-app` (`avogadro/src/CMakeLists.txt:86`, output name `avogadro`).
- Options go near `ENABLE_GLSL` (`CMakeLists.txt:441`):
  `AVO_RENDERER_TIER2/TIER1/LEGACY` (all ON; APPLE forces TIER2 OFF), surfaced as
  `AVO_RENDERER_TIER2=1`-style definitions on the `avogadro` library target.
- `AVOGADRO_RENDERER` override read at startup (before first GLWidget creation), tier +
  renderer logged via the `force-dedicated-gpu` diagnostics qDebug.

### A9. Colors are baked at build time

- Today colors are resolved per frame (`map->setFromPrimitive(a)` per atom,
  `pd->colorMap()` fallback, per-atom custom names `atom.h:194`, selection color via
  `setToSelectionColor()`). Modern tier bakes resolved RGBA into instance records at
  build time. Rebuild triggers: engine `changed()` (already connected,
  `glwidget.cpp:2041-2047`), `GLWidget::setColorMap` (`glwidget.cpp:690` — add scene
  invalidation; today it only swaps the pointer), and per-atom custom-color edits
  (`atomUpdated()` → dirty chunk).
- Transparency: engines with `m_alpha < 1` (bsdy/sphere opacity) emit into the
  transparent pass with per-instance alpha; chunks sorted back-to-front by camera
  distance; engine-level order still follows `transparencyDepth` (A3).

## Risks / Trade-offs

- [Regression surface across engines] → compatibility bridge keeps unported engines
  working; port order starts with the default engine; visual-parity screenshots per port
  (tasks 3.x); modern tier can be env-disabled in a release if a blockship appears.
- [Compatibility-profile reliance is long-term debt] → accepted consciously; Tier 3 stays
  GL; a future RHI migration is a separate change.
- [Old Intel driver quirks at GL 3.3/4.0] → tier probing + override + forced-tier test
  runs; HD 2500/4000-class (GL 4.0) still runs Tier 2.
- [llvmpipe/software fallback] → works but slow; Change 1's honest diagnostics make it
  visible; installer-side software-GL policy is out of scope here.
- [QOpenGLWidget context loss / resize storms vs cached GPU buffers] → recreate chunk
  buffers on context destruction; chunks are re-uploadable from CPU snapshots by design.
- [Transparency ordering differences vs legacy] → chunk-level sorting approximation;
  parity requirement is "same visual class", validated by side-by-side review.

## Migration Plan

Staged: scene core + modern backend → engine ports (default engine first) → picking swap →
mesh streaming → threading hardening → compat tier → defaults flip to modern. Legacy
remains compiled and selectable until modern tier is feature-complete; flipping the
default back is a one-line/env change (rollback). No data migrations.

## Open Questions

None blocking. Deferred by design: Tier 3 implementation, RHI evaluation, macOS modern
tier, multi-window buffer sharing (per-engine-instance caches are acceptable initially;
sharing is an optimization, not a requirement).
