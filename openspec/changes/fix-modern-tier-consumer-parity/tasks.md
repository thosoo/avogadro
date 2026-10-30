# Tasks: fix-modern-tier-consumer-parity

Design decisions D1–D6 with implementation anchors from design.md's
"Implementation anchors" table (line numbers as of the 2026-10-21 audit —
trust symbol names over line numbers). Land sections in order: the
overlay-pass restructure (1.x) is the foundation the mesh flush (2.x), unit
cells (3.x), and settings (4.x) plug into. No CUDA/Qt-template code is
touched, but the change modifies paint-flow code shared with AutoOpt — per
repo policy, run the `reviewer` subagent on the full diff after tasks 1–4
land and before declaring the change complete.

## 1. Shared overlay pass (tool paint bracket + ordering)

- [x] 1.1 In `glwidget.cpp`, add a private helper
      `void paintOverlaysAfterFrame()` (declared in `glwidget.h`'s private
      section) structured as: `d->painter->begin(this)` → unit-cell pass
      (3.2, initially a no-op placeholder call) → mesh flush (2.1,
      placeholder) → `if (d->tool) d->tool->paint(this)` → the existing
      `flushCapturedText()` loop (nesting `begin/end` is safe —
      `GLPainter::begin/end` are overflow-counted, glpainter_p.cpp:1516–1530;
      alternatively inline the text loop into the helper's single bracket,
      either is fine, pick one). Call it from (a) the worker-adopt branch of
      `renderModern` replacing the `d->tool->paint(this)` +
      `flushCapturedText()` pair (~1131–1141, keep the `return;` and the
      forward-progress comment intact), (b) the sync path of `renderModern`
      replacing the bare `flushCapturedText()` after
      `backend->endFrame()` (~1173), and (c) `renderCompat` after
      `compatBackend->endFrame()` (~1490, task 1.2). Then delete the
      in-emission tool-paint block from `emitSceneToBuilder()`
      (~1608–1617, including its ordering comment) so emission is
      engines-only. Verify: build;
      `ctest -R "visualparity|meshstream|modernrenderer" --output-on-failure`
      green.
- [x] 1.2 (If not already done in 1.1c) wire the helper into `renderCompat`.
      Verify: build; with `AVOGADRO_RENDERER=compat`, AutoOpt shows the
      energy line and bond-centric shows length/angle labels.
- [x] 1.3 Confirm no per-frame "GLPainter not active." warnings remain on
      either tier during tool paint — the warning is emitted from
      `GLPainterPrivate::isValid()` (glpainter_p.cpp:~222) whenever
      `d->widget == 0`, which was the entire bug. Verify: console clean
      over 60 s of AutoOpt ticking on modern (previously ~2 warnings/frame).

## 2. Mesh flush through the real painter

- [x] 2.1 Fill in the helper's mesh-flush placeholder (before the tool
      paint, per D2): iterate
      `const ChunkStore<MeshInstance> &meshes = d->builder.meshes()`
      (`size()` / `at(i)`, chunkstore.h:68/81). For each instance
      (records.h:71–76): resolve `inst.mesh`; skip null or
      `!mesh->stable()`; acquire `mesh->lock()->tryLockForRead()` and skip
      on failure (SurfaceEngine's pattern, surfaceengine.cpp:82–100);
      `glEnable(GL_BLEND)` around the whole flush; set the painter color
      from `inst.color[0..3]` (unsigned char → float /255f) via
      `d->painter->setColor(r, g, b, a)`; call
      `inst.colored ? d->painter->drawColorMesh(*mesh, inst.mode)
      : d->painter->drawMesh(*mesh, inst.mode)`; `unlock()` in all paths;
      restore GL state (`glDisable(GL_BLEND)`, and re-enable
      `GL_LIGHTING` — `GLPainter::drawMesh` already ends by restoring
      `glPolygonMode(GL_FRONT, GL_FILL)` + `glEnable(GL_LIGHTING)`).
      Mesh pointers stay valid because the caller holds the molecule read
      lock (render()'s RAII ReadLockGuard). `GLPainter`'s VBO cache
      handles upload-once/stable gating (ensureMeshVBO). Verify: build;
      headless test (5.1) plus manual ORCA orbital visible on modern.
- [x] 2.2 Remove `pushMeshes` from the backend interface:
      `rendererbackend.h:69` (pure virtual), `modernbackend.h:162`
      (override), `modernbackend.cpp:310–313` (stub). Fix any callers
      (grep says none call it today). Verify: build green on Linux;
      `git grep pushMeshes` returns nothing.
- [x] 2.3 Emitter diagnostics: add the one-shot `static bool warned`
      `qWarning` pattern of `drawCone` (scenebuilder.cpp:183–193) to
      `drawTorus` (311–314), `drawEllipsoid` (316–319), and
      `drawShadedQuadrilateral` (247–256) — the latter currently coarsens
      silently, the former two drop silently, both contrary to the
      existing "no silent primitive loss" spec clause. No geometry
      changes. Verify: build; headless test 5.3 asserts each warning
      fires exactly once per process.

## 3. Unit cells on the modern tier

- [x] 3.1 Expose the cell drawing for the helper: `renderCrystalAxes()`
      (glwidget.cpp:1702) and its TU-local `renderClippedBox` companion
      (anonymous namespace, ~1737–1840, drawing via
      `d->painter->drawBoxEdges` at 1829) are already painter-based —
      extract nothing more than reachability: move/keep them above the
      helper and call `renderCrystalAxes()` from the helper when
      `d->molecule->OBUnitCell()` is non-null. Leave the legacy branch's
      `renderCrystal(displayList)` path exactly as is (glwidget.cpp:1673;
      legacy stays byte-identical by construction). The `glCallList`
      repetition loop never repeats (`aCells/bCells/cCells` fixed 1,
      glwidget.cpp:197, no setter) so skipping it on modern loses
      nothing. Verify: build; legacy-tier crystal render unchanged in the
      parity harness.
- [x] 3.2 Invoke the cell pass from the overlay helper (replacing 1.1's
      placeholder), positioned before the mesh flush so blended surfaces
      composite over the opaque cell edges (legacy layering, D3). Gate on
      `d->molecule->OBUnitCell() != nullptr`. Verify: headless test (5.2)
      + manual crystal file on modern shows cell edges/axes as legacy
      does; molecular files show no difference.

## 4. Settings reach the modern tier

- [x] 4.1 Quality (per corrected D4 — NOT the tessellator; instances carry
      no tessellation): map the slider value onto the existing
      `LodLevel` presets exactly as settingsdialog.cpp `qualityChanged`
      labels it — 0–1 → Low, 2 → Medium (default), 3–4 → High. In
      `GLWidget::setQuality` (glwidget.cpp:974) compute the mapped level
      and, only when it changed, call the new
      `ModernRendererBackend::setDetailLevel(LodLevel)` which rebuilds the
      unit sphere/cylinder index buffers at `lodParams(level)` — extract
      the current one-shot builds (modernbackend.cpp:627 `SphereGeometry`,
      702 `CylinderGeometry`) into a private rebuild helper that deletes +
      recreates the VAO/VBO/EBO sets; instance stores are untouched
      (worker results stay valid; no re-keying). Remove the now-dead
      `SceneBuilder::m_quality` (+ its getter/setter; `beginFrame`'s
      quality parameter stays as-is or drops the unused arg while we're
      there). Leave `GLPainter::setQuality` (legacy tier) and the compat
      backend untouched (frozen). Verify: headless test — after
      `setDetailLevel(High)`, the rebuilt sphere geometry reports
      `lodParams(LodLevel::High)` ring/segment counts (expose via a small
      `A_EXPORT`-ed test getter on the backend, following the
      `AVOGADRO_TEST_*` hook precedent); manual: slider visibly changes
      sphere/bond coarseness on modern.
- [x] 4.2 Fog: in `shaders.h` add `uniform vec3 uFogColor; uniform float
      uFogStart; uniform float uFogEnd;` (+ an enable, or treat
      `uFogStart >= uFogEnd` as disabled) to the shared lighting preamble
      (~46–68) with a `fogFactor(viewPos)` helper, and add
      `vViewPos = (uModelView * vec4(worldPos, 1.0)).xyz;` in each vertex
      shader (~119–131); mix toward `uFogColor` by
      `clamp((uFogEnd - length(vViewPos)) / (uFogEnd - uFogStart), 0, 1)`
      in each fragment main after lighting (sphere, bond, line). Backend
      gains `setFog(bool enabled, const QVector3D &color, float start,
      float end)` storing values applied with the other per-frame uniforms
      (modernbackend.cpp:773–776 is the pattern). `renderModern` computes
      and passes them with the legacy branch's exact math
      (glwidget.cpp:1544–1556): `distToCenter = |modelview * d->center|`;
      `start = distToCenter - (d->fogLevel / 8.0) * d->radius`;
      `end = distToCenter + ((10 - d->fogLevel) / 8.0 * 2.0) *
      d->radius`; color = `d->background`; `d->fogLevel == 0` →
      disabled. Keep the legacy branch's `glFog*` calls untouched.
      Verify: build; visual parity check against legacy fog on the same
      scene — if it diverges unacceptably, record the finding and switch
      to the D4/D6 fallback (disable the fog control on modern with a
      visible reason; touchpoint settingsdialog.cpp + a per-tier query on
      GLWidget) instead; either outcome satisfies the spec.
- [x] 4.3 Confirm the settings dialog still enables both sliders on all
      tiers (no gating scaffolding unless 4.2's fallback triggered —
      then wire the visible per-tier reason into settingsdialog.cpp /
      mainwindow.cpp). Verify: manual — sliders active, effects
      observable per 4.1/4.2.

## 5. Tests

- [x] 5.1 New headless test `consumerparitytest.cpp` (register in the
      `tests` list of `libavogadro/tests/CMakeLists.txt` AND add the name
      to the `MATCHES "^(parity|compatibility|…)"` env regex so it runs
      under `LIBGL_ALWAYS_SOFTWARE=1` — it drives a real GLWidget, and
      QOpenGLWidget needs a drawable on the offscreen platform). Harness:
      `makeHiDpiWidget`-style helper (hidpiviewporttest.cpp:~123).
      Mesh-flush coverage: add an `AVOGADRO_TEST_*`-guarded upload counter
      to `GLPainterPrivate` (increment in `ensureMeshVBO`'s upload branch,
      glpainter_p.cpp:~260; precedent for test-only hooks:
      `AVOGADRO_TEST_NOPLUGIN`) — a stable mesh produces exactly one
      upload and a cached draw on subsequent frames; an unstable mesh
      (call `mesh->setStable(false)`) is skipped; a `mesh->setVertices()`
      revision bump re-uploads once. Compile set as for `meshstream`
      (glpainter_p.cpp + sphere_p + cylinder_p + textrenderer_p +
      OpenGL::GLU) if asserting at the painter level, plus bsdyengine if
      driving a full widget scene. Verify:
      `ctest -R consumerparity --output-on-failure` green.
- [x] 5.2 Extend the same test: overlay-pass bracketing — a minimal
      `Tool` subclass whose `paint()` calls
      `widget->painter()->drawText(...)` (compile autoopttool.cpp-style
      standalone tool sources with `AVOGADRO_TEST_NOPLUGIN` if a real
      tool is preferred); assert (a) via a `qInstallMessageHandler`
      counter (pattern: glselectoverflowtest.cpp:312) that no
      "GLPainter not active" message fires during a rendered frame, and
      (b) text reaches the framebuffer or the captured store
      (`textItems()` assertion pattern: scenebuildertest.cpp:525–531).
      Unit-cell: first test in the suite to construct an OpenBabel unit
      cell (`OBUnitCell::SetData` + attach via `Molecule`'s OBMol) —
      assert the cell pass runs for an OBUnitCell molecule (framebuffer
      differs from the same molecule without the cell; no new GL errors)
      and not for a molecular one. Verify: green.
- [x] 5.3 Emitter-diagnostic test: install the message handler, call
      `SceneBuilder::drawTorus` / `drawEllipsoid` /
      `drawShadedQuadrilateral` twice each, assert each warned exactly
      once per process (second call adds no message). Verify: green.

## 6. Verification

- [x] 6.1 Manual matrix on the modern tier: AutoOpt energy + Fixed/Ignored
      counts + constraint halos visible (formerly all invisible); bond-centric
      length/angle labels; ORCA file → select HOMO/LUMO → both lobes render,
      isovalue change updates; ESP-colored mesh keeps gradient; crystal file
      shows cell; quality slider changes tessellation; fog slider changes
      depth cueing; legacy tier unchanged (forced via
      `AVOGADRO_RENDERER=legacy`). Verify: all rows match the delta spec
      scenarios.
- [x] 6.2 AutoOpt coexistence re-check: with the new tool-paint bracket,
      no constraint markers leak after gestures; fixed/ignored counts stay
      0/0 with no intentional constraints. Verify: matches
      auto-optimize-interaction requirements.
- [x] 6.3 Full suite: `cmake --build build --parallel && ctest
      --test-dir build --output-on-failure` green. Verify: exit code 0.
- [ ] 6.4 Windows/MSVC compatibility: shader/uniform changes and new test
      compile under the Windows installer workflow. Verify:
      `gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`
      green, including offscreen ctest.
- [ ] 6.5 Run the `reviewer` subagent on the complete diff (correctness +
      regression risk per repo policy); integrate accepted findings.
      Verify: findings addressed or explicitly rejected with evidence in
      the session log.
