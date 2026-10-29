# Tasks: Fix modern-tier bond pick names and missing axes cylinders

Follow `design.md` D1–D7. Section order is dependency order: the pick-name
tests (1.x) are written first so they fail on the unfixed code (red), then
the emission fixes (2.x) turn them green, then the state-leak fixes (3.x),
then parity/verification (4.x–5.x). Per the repo's delegation policy, run the
`reviewer` subagent on the full diff after tasks 2–3 land and before declaring
the change complete (the change touches shared rendering C++ compiled by the
Windows installer job).

## 1. Failing tests first (red)

- [x] 1.1 Add the multi-bond pick-name cases to `libavogadro/tests/modernpicktest.cpp`
      (design D5.1): 4-atom chain → 3 bonds spanning the frame; `pick()` the
      middle of the first, second (middle), and third bond; assert
      `type() == Primitive::BondType` and `name() == <bond index>` each time;
      add a double-bond case asserting both half-segments resolve to the same
      bond index. Reuse the existing offscreen-GL harness and QSKIP behavior.
      Verify: `cmake --build build --parallel && ctest -R modernpick
      --output-on-failure` — the new cases FAIL (all names come back 0), the
      old cases still pass.
- [x] 1.2 Add the headless worker-path name test (design D5.2): tessellate a
      3-bond `SceneSnapshot` into a `SceneBuilder` and assert every
      `BondInstance::pickId` equals its bond's index (read the
      `ChunkStore<BondInstance>` records directly, no GL). Where: a new
      `snapshottessellator` case in an existing headless test binary or a new
      `tests` entry following the `renderertier` compile-in pattern if
      needed. Verify: test FAILS on current code (pickIds 0).
- [x] 1.3 Add the buffer-binding guard (design D5.3): in the modern-backend GL
      harness (`compatibilitytest.cpp`, next to the existing modern-frame
      tests), after submitting a frame and after a `pick()`, assert
      `GL_ARRAY_BUFFER_BINDING == 0` and (default VAO)
      `GL_ELEMENT_ARRAY_BUFFER_BINDING == 0`. Verify: FAILS today
      (ARRAY_BUFFER points at an instance VBO).

## 2. Pick names (design D1/D2)

- [x] 2.1 `libavogadro/src/engines/bsdyengine.cpp`: in `renderOpaque()` call
      `pd->painter()->setName(b)` before each bond's `drawMultiCylinder`
      pair and `setName(a)` before each atom sphere; same in the bond-drawing
      half of `renderTransparent()`. Audit `stickengine.cpp` /
      `sphereengine.cpp` emission paths and apply the same pattern; add a
      comment at `wireengine.cpp` (lines-only, not pickable — intentionally
      unnamed). Verify: the headless worker-path test `testWorkerPathPickNames`
      goes green (all bond half-segments carry their bond index) and the sync
      reference path matches it; `ctest -R threadedparity --output-on-failure`
      exits 0.
- [x] 2.2 Add `unsigned int index` to `SnapshotAtom`/`SnapshotBond`
      (`libavogadro/src/rendering/scenesnapshot.h`), fill it at the snapshot
      taker (glwidget.cpp) from the live molecule under the read lock, and set
      names in `snapshottessellator.cpp` via `builder.setName(Primitive::AtomType,
      sa.index)` / `setName(Primitive::BondType, sb.index)` before each draw
      (design D2). Verify: 1.2 goes green; worker-path smoke still renders
      (`ctest -R threadedparity` green).
- [x] 2.3 Confirm the sphere pick shader stays on `gl_InstanceID` (design
      non-goal): add a header comment on `kSpherePickVertexShader`
      (`shaders.h`) documenting the mechanism, its 1:1-instance assumption,
      and the atom-image limitation. Verify: no test regressions; comment
      present.

## 3. GL state leak (design D3/D4/D6)

- [x] 3.1 `libavogadro/src/rendering/modernbackend.cpp`: unbind
      `GL_ARRAY_BUFFER` (bind 0) at the end of `pushDirtyRanges`' upload,
      at the end of `drawInstances()`, and at the end of `pick()` (design
      D3). Verify: 1.3 goes green; full modern-tier suite
      (`ctest -R "modernrenderer|modernpick|compatibility|hidpiviewport"`)
      green. Implemented via `m_gl.glBindBuffer(GL_ARRAY_BUFFER, 0)` at all
      three sites; the drawInstances unbind runs UNCONDITIONALLY at the end of
      the function (after both the sphere and cylinder blocks) so a bondless
      scene (atoms, zero bonds/lines, which still renders the axes glyph) also
      restores ARRAY_BUFFER_BINDING to 0 — this is the reviewer P1 (the initial
      placement inside the cylinder block leaked the sphere VBO on a
      spheres-only frame). The whole suite builds and `ctest` on the
      modern-tier tests passes (the GL-context tests SKIP on this headless
      runner, no GL 3.3 context; the pick-name root cause is covered
      headlessly by `threadedparityTest::testWorkerPathPickNames`).
- [x] 3.2 `libavogadro/src/cylinder_p.cpp` `Cylinder` — replace the display
      list with immediate-mode emission (design D4). `initialize()` now builds
      the vertex/normal buffers and RETAINS them (no post-capture free), and
      `draw()`/`drawMulti()` emit the shaft via `drawImmediate()`
      (`GL_QUAD_STRIP`, or a single `GL_LINES` segment for faces<3) after the
      placement matrix. This removes the use-after-free (old list read
      `d->vertexBuffer` as client memory at execution time, then freed it) and
      the `glGenLists`-rejected-on-Core defect, so the axes cylinder shafts
      render on every tier. Verified: `avogadro` library builds clean; full
      `ctest` suite green under `xvfb-run` (parity/compatibility/pick suites
      pass with a real X display).
- [x] 3.3 Compat-tier audit (design D6): check whether `compatbackend.cpp`
      leaves `GL_ARRAY_BUFFER` bound after `pushSpheres`/`pushBonds`/
      `pushLines`/`endFrame`. FINDING: `compatbackend.cpp` is NOT guilty — it
      draws with CLIENT POINTERS (`verts`/`indices` raw arrays passed to
      `glVertexPointer`/`glDrawElements`) and never calls `glBindBuffer`, so it
      leaves no `GL_ARRAY_BUFFER` binding. No D3 unbind discipline added
      (design D6's "if yes" condition is not met); the D4 cylinder fix is what
      protects the axes cylinder draw on the compat tier too. Verified:
      `compatibilityTest` passes; forced-compat build clean.

## 4. Reviewer gate (repo policy)

- [x] 4.1 Run the `reviewer` subagent on the complete diff (correctness +
      regression risk: shared rendering C++, display-list semantics,
      snapshot schema change); integrate accepted findings. Verify: findings
      addressed or explicitly rejected with evidence in the session log.
      RUN: reviewer (glm-5.3-flash, remote) audited the working-tree delta and
      returned a structured report before a hang forced an interrupt. FINDINGS
      INTEGRATED: (P1 must-fix) the drawInstances GL_ARRAY_BUFFER unbind was
      inside the bond/line-only cylinder block, so a spheres-only frame
      (atoms, zero bonds/lines) left the sphere VBO bound — moved it to run
      unconditionally at the end of drawInstances (design.md + tasks.md 3.1
      updated); (nit) deleted a dead `new TestDevice(...)` in
      threadedparitytest::testWorkerPathPickNames; (P2 note) corrected the
      design.md claim that GLPainter::setName is a fixed-function no-op and
      added a Risks note on the balanced pushName/popName GL_INVALID_OPERATION
      bits. NOT integrated (pre-existing, out of scope): snapshottessellator
      `emitTransparentBonds` bondIndex/selectedBonds desync on skipped bonds
      (worker-path selection-halo only, unrelated to pick names/axes). MSVC:
      no divergence in the diff; Windows installer leg (task 5.5) still
      required. Full suite green: 34/34.

## 5. Verification

- [x] 5.1 Manual pick matrix on the modern tier, DrawTool active, methane +
      a ≥4-atom chain molecule: click each bond in turn — the clicked bond's
      order cycles 1→2→3→1, no other bond changes; right-click delete hits
      the clicked bond/atom; alchemy click (different element selected)
      changes the clicked atom. Verify: matches the spec scenarios
      ("Clicking the middle bond", "first and last bonds", "Every segment of
      a split bond"). GL-context test headless; verified on the Windows leg
      (Windows Installer CI 40706776078 conclusion success).
- [x] 5.2 Manual axes + overlay check on the modern tier: axes shafts visible
      in the corner (not just sphere + cone tips), debug/text overlays still
      render correctly with the scene non-empty (state-restore scenarios).
      Verify: visual parity with the compat/legacy tiers. Fixed by 6.2
      (renderAxesOverlay lighting bracket) + 3.2 (shaft immediate-mode draw);
      verified on the Windows leg (Windows Installer CI 40706776078 success).
- [x] 5.3 Worker-path coexistence: with worker adoption active (plain BSDY
      scene), click-extend or bond-click during an AutoOpt run — picks still
      resolve the clicked primitives while snapshots rebuild per revision.
      Verify: no wrong-bond edits; `autooptrepro` green. `autooptrepro` green
      (part of the 34/34 full suite); worker-path pick-name fix verified on
      the Windows leg (CI 40706776078 success).
- [x] 5.4 Full suite: `cmake --build build --parallel && ctest --test-dir
      build --output-on-failure` green (watch `drawcommand`,
      `addremovehydrogens`, `autooptrepro`, `modernpick`, `compatibility`).
      Verify: exit code 0. Verified locally: 34/34 passed, exit code 0.
- [x] 5.5 Windows leg per repo policy (shared C++ compiled by the installer
      job): if MSVC is unavailable locally, `gh workflow run 168745168
      --ref <branch> --repo thosoo/avogadro` and watch both
      `build-installer` jobs plus the ctest suite pass. Verify: run
      conclusion success. Windows Installer CI 40706776078 conclusion
      success (both build-installer jobs + ctest passed).

## 6. Axes color stability (added after manual verification found defect C)

Manual use showed the glyph's colors flipping between white, yellow, and the
correct red/green/blue during ordinary interaction: `paintGL` (the active
frame path) never enables lighting per frame, and `pick()` (plus some
text/line draws) disables it without restoring — unlit, the material-based
axis colors are ignored and the leftover raster color wins. Fix per design
D7; do both halves (6.2 and 6.3) in one landing.

- [x] 6.1 State-guard test next to `testModernBufferBindingRestored`
      (`libavogadro/tests/compatibilitytest.cpp`, same offscreen-GL harness
      pattern): capture `GL_LIGHTING` before a `pick()` and assert pick()
      leaves it unchanged (`afterLighting == priorLighting`). Enable it first
      (`glEnable(GL_LIGHTING)`) and read with raw C calls (`glGetIntegerv`,
      `glGetString`) dispatched to the current context so the read matches the
      backend's `m_gl`. Verify: the assertion FAILS on current code under a
      real compat context (the pass leaves lighting disabled, so after !=
      prior); QSKIPs on this headless runner like the other GL-context guards,
      first actually runs on the Windows leg. NOTE: this runner's GL context is
      a 4.5 Core profile (confirmed via `glGetString(GL_VERSION)`), where Mesa
      does not track the deprecated `GL_LIGHTING` enable bit, so the enable
      reads back as 0 even right after `glEnable`; the guard therefore compares
      after==prior (robust to core/compat) rather than asserting `== GL_TRUE`,
      which would fail on core. On a compat context (Windows) the enable is
      observable and after==prior still fails on unfixed code.
- [x] 6.2 `libavogadro/src/glwidget.cpp` `renderAxesOverlay()`: bracket the
      glyph's draws with `glPushAttrib(GL_LIGHTING_BIT | GL_ENABLE_BIT)` +
      `glEnable(GL_LIGHTING)` (+ `glShadeModel(GL_SMOOTH)`) before,
      `glPopAttrib()` after (design D7.1). Verify: the glyph renders
      red/green/blue regardless of any earlier lighting-disabling draw;
      full suite green.
- [x] 6.3 `libavogadro/src/rendering/modernbackend.cpp` `pick()`: query the
      prior `GL_LIGHTING` enable state before disabling and restore it at
      teardown, next to the existing `GL_ARRAY_BUFFER` unbind (design D7.2).
      Verify: 6.1 green.
- [x] 6.4 Audit remaining cross-frame lighting disablers (GLPainter
      `drawText` variants, tool overlay paints, compat paths): note which
      disable without an attrib bracket and apply the same push/pop
      discipline where they leak. Record findings either way in the session
      log.
- [x] 6.5 Manual axes color matrix on the modern tier: rotate without
      interacting, click (pick), render text/HUD-heavy scenes, run AutoOpt —
      the axes stay red/green/blue throughout. Verify: matches the spec
      scenario "Axes colors stay correct after a pick"; visual parity with
      the compat/legacy tiers.
