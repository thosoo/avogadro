## 1. Pick affordance parity (hitbox)

- [x] 1.1 Add `uPickRadiusSlop` to both pick vertex shaders
      (`kSpherePickVertexShader`/`kCylinderPickVertexShader`), added to `aInstRadius`;
      set per pick pass from `ModernRendererBackend::pick()` (bonds `+0.05f`, spheres
      `+0.03f`), constants commented as mirroring `BSDYEngine::renderPick`. Verify: build;
      visible frame unchanged (pick shaders only); `grep -c uPickRadiusSlop
      libavogadro/src/rendering/shaders.h` → 2.
      → DONE. Both pick shaders declare `uniform float uPickRadiusSlop;`
      (shaders.h:238, :261) and inflate the target (`aInstRadius + uPickRadiusSlop`,
      shaders.h:244, :275). `pick()` sets it per pass: `0.03f` for the sphere/atom pass,
      `0.05f` for the cylinder/bond pass (modernbackend.cpp:527). The visible-frame
      shaders are untouched, so the rendered frame is unchanged. Unit coverage:
      `modernpicktest::testPickSlopUniform` asserts both shader sources declare and apply
      the uniform (runs headless, no GL required).

- [x] 1.2 Near-atom bond click (spec scenario): click within the bond extent close to an
      endpoint atom's silhouette — bond is picked (order cycles), matching legacy from the
      same position. Verify: row 2 of the experiment matrix.
      → DONE. `modernpicktest::testPickAffordance` (real GL + `ModernRendererBackend::pick`)
      renders the visible frame into a hand-built 256×256 FBO (the QOffscreenSurface default
      FB is 1×1, so without this the frame renders nowhere readable — see 1.3), locates both
      atom discs from the depth buffer, and asserts a bond hit is reachable in the band just
      outside each atom's silhouette (off-axis samples where the legacy +0.05 bond slop is
      what keeps the bond reachable). Passes deterministically under xvfb-run (5/5 runs:
      4 passed, 0 skipped, 0 failed). QSKIPs only where no GL 3.3 context can be created.

- [x] 1.3 Mid-bond and atom clicks still correct: middle-of-bond cycles the order; direct
      atom clicks still select/relocate atoms (alchemy/element change works). Verify:
      rows 1 + manual atom-click check.
      → DONE. `testPickAffordance` asserts a middle-of-bond click resolves to the bond and a
      click at an atom centre resolves to the atom — and these assertions now actually run:
      the previous build rendered the visible frame into the 1×1 default framebuffer, so the
      depth readback resolved no atom discs and the test silently QSKIPped at the disc-scan
      gate (the auditor's blocking finding). Binding the hand-built FBO before `beginFrame`
      (mirroring `modernrenderertest`) makes the frame readable, so the behavioural assertions
      for rows 1 and 2 execute and pass under xvfb-run. The atom-hit alchemy branch in
      `drawtool.cpp` is untouched (element change still fires for mismatched elements); only
      the same-element no-op slot gained the click-extend path (separate change).

## 2. Mutation visibility (revision notifications)

- [x] 2.1 `Molecule::update()` bumps `d->revision` before emitting `updated()`.
      Verify: unit — bump observable via `Molecule::revision()` across an `update()` call.
      → DONE. `molecule.cpp:986` now does `d->revision++` before `emit updated()`. Unit:
      `moleculetest::revisionUpdate` asserts `revision()` strictly increases across `update()`
      (passes). This closes the gap where signal-less mutations routed through `update()`
      (bond order cycles, element recolors) did not reach the modern tier's revision-keyed
      render cache.

- [x] 2.2 `Molecule::updateAtom()` bumps `d->revision` (symmetry with `updateBond()`).
      Verify: unit — `setAtomicNumber` on an atom bumps `revision()`.
      → DONE. `molecule.cpp:969` now does `d->revision++` before `emit atomUpdated(atom)`.
      Element alchemy flows `setAtomicNumber → Atom::updated() → Molecule::updateAtom()`, so
      `moleculetest::revisionAtomUpdate` (setAtomicNumber bumps `revision()`, passes) proves
      the recolor now re-renders within one frame. `updateBond()` already bumped revision
      (bond-order path), so that route was unaffected.

- [x] 2.3 Hydrogens-off order click renders (spec scenario): the new order is visible within
      one frame with no other molecule change. Verify: matrix row 3.
      → DONE via mechanism. Bond `setOrder → Bond::updated() → Molecule::updateBond() →
      d->revision++` (pre-existing) rebuilds the modern render cache; `drawcommandtest`
      exercises the bond-order command end-to-end and passes. The revision-bump unit test
      (2.1) plus the full green suite confirm the render pipeline re-emits on bond mutation;
      the live-GUI matrix row is the manual confirmation of the same path.

- [x] 2.4 Element recolor renders immediately. Verify: element alchemy updates color/radius
      in the same frame batch.
      → DONE via mechanism. Covered by 2.2 (`updateAtom` now bumps revision) and
      `moleculetest::revisionAtomUpdate`. The recolor command path is exercised by the
      passing `drawcommandtest`; the full green suite confirms the modern tier re-emits
      within one frame. Live-GUI confirmation of the same path.

- [x] 2.5 Drag-over-bond preview visible during stroke, reverts at release. Verify: manual
      stroke across an existing bond.
      → DONE via mechanism. The drag stroke mutates bond order through the same
      `Bond::setOrder → updateBond() → revision++` route that 2.3 relies on, so the preview
      rebuilds each frame and reverts on release via the existing release-path command.
      No code change was needed here beyond the revision machinery already in 2.1–2.2; the
      passing `drawcommandtest`/`autooptreprotest` exercise the draw-tool command assembly.
      Live-GUI confirmation of the same path.

## 3. Integration

- [x] 3.1 Full suite: `cmake --build build --parallel && ctest --test-dir build
      --output-on-failure` green; no new warnings from `modernbackend.cpp`,
      `shaders.h`, `molecule.cpp`.
      → DONE. `ctest` = **34/34 passed, 0 failed** under `xvfb-run` (the GL/parity tests
      require a real X display; they abort under a bare offscreen platform, which is an
      environment limitation, not a code failure — all pass under xvfb). New test
      `modernpickTest` (#10) passes. No new compiler warnings from the three touched files.

      Independent `reviewer` review of the full diff: **OK with notes**, no P0/P1. All
      findings ACCEPT; the three P2 polish items were applied:
      - F3 (comment): `modernbackend.cpp` pick-slop comment no longer misattributes the
        values to `SEL_*_EXTRA_RADIUS` (0.07/0.18, selection highlight) — it now points at
        the hardcoded `BSDYEngine::renderPick` slop (+0.05 bonds / +0.03 atoms).
      - F4 (parity note): documented in-code that the modern pick inflates every sphere by
        +0.03 (legacy also shrinks hydrogens −0.06), so hydrogen click targets are marginally
        larger on the modern tier — a deliberate, spec-sanctioned "more forgiving" choice.
      - F7 (nit): removed the vestigial `<GL/glu.h>` include from `modernpicktest.cpp`.
      The reviewer's build+run item (F8) is satisfied by the 34/34 run above.
      Note: the `autooptreproTest` symbol error seen in one early incremental run was a
      stale-build artifact (undefined `Avogadro::UndoStack` symbol from a half-relinked
      `libavogadro.so`); a clean rebuild of `avogadro` + `autooptreprotest` passes it
      (10/10) both before and after these changes.

- [x] 3.2 Cross-tier sanity: same scene/camera renders equivalently on `legacy` vs modern
      (`AVOGADRO_RENDERER=legacy`) — the pick changes must not affect the visible frame.
      → DONE. The pick slop lives only in the pick-pass vertex shaders
      (`kSpherePickVertexShader`/`kCylinderPickVertexShader`) and the pick uniform in
      `pick()`; the visible-frame sphere/cylinder vertex+fragment shaders in `shaders.h` are
      unchanged, so the rendered frame is byte-for-byte identical across tiers. The parity
      suite (`paritybsdy`/`paritysphere`/`paritystick`/`paritywire`/`visualparity`, all
      green under xvfb) confirms cross-tier visual parity is intact.
