# Tasks: Surface wireframe during camera drags

Follow `design.md` decisions D1–D6. The change is one behavior flip in
`flushCapturedMeshes` (D1) plus release-time flag clearing (D2); everything
else is comment hygiene and verification. Per the repo's delegation policy,
run the `reviewer` subagent on the diff after task 2 lands and before
declaring the change complete (the touch point sits inside the mesh
read-lock / stability-guard region that fix-orbital-visualization hardened;
the review must confirm those guards survived intact).

## 1. Wireframe flush during camera interaction (design D1, D3, D4)

- [x] 1.1 In `libavogadro/src/glwidget.cpp` `flushCapturedMeshes()` (~1641):
      delete the `if (m_cameraInteracting) return;` early return and its
      "D10 / task 4.3" comment; compute
      `const int flushMode = m_cameraInteracting ? 1 : inst.mode;` once per
      mesh and pass `flushMode` to BOTH the `drawColorMesh` (colored) and
      `drawMesh` branches. Leave the stability skip, the
      `tryLockForRead()` skip, the culling block, and the blending setup
      exactly as they are. Verify: builds (`cmake --build build --parallel`).
- [x] 1.2 Reword the now-stale D10/task-4.3 comments: the constructor timer
      comment (~glwidget.cpp:576), the `mouseMoveEvent` re-arm comment
      (~2946), and the member declarations in `glwidget.h` (~927) — from
      "surfaces are skipped while dragging" to "surfaces render wireframe
      while dragging (legacy renderQuick parity); flag selects the flush
      mode, it no longer skips". Verify: `git grep -n "skip.*drag\|drag.*skip"
      libavogadro/src/glwidget.*` shows no leftover skip semantics; build clean.

## 2. Solid returns on release (design D2)

- [x] 2.1 In `GLWidget::mouseReleaseEvent` (~2913), set
      `m_cameraInteracting = false;` next to the existing
      `d->quickRender = false;` block (with a one-line comment: solid surface
      returns on the first post-release frame; the 250 ms timer remains the
      backstop for missed releases). Do NOT stop the timer — an armed timer
      firing after a clean release is a harmless no-op repaint. Verify:
      build clean.

## 3. Verification

- [x] 3.1 Full suite: `cmake --build build --parallel && ctest --test-dir
      build --output-on-failure` green — in particular `consumerparity`
      (mesh-flush upload counter must stay flat: the VBO cache invariant is
      untouched by this change) and `meshstream`. Verify: exit code 0.
- [ ] 3.2 Manual matrix (real app, default modern tier; offscreen cannot
      drive mouse drags — design D6), each row against the spec delta:
      - ORCA orbital displayed, tilt/rotate: surface visible in EVERY frame
        as a wireframe net; release: solid returns on the first frame, no
        250 ms blink.
      - Same on compat tier (`AVOGADRO_RENDERER=compat`): net during motion.
      - ESP-colored surface: colored net during motion, solid at rest.
      - At rest: rendering identical to pre-change (solid, translucent,
        culled).
      - Rapid tilt-then-stop: no frame presents the molecule without the
        surface; no hide-then-reappear interval.
      - Surface being recalculated mid-drag: absent only while unstable,
        returns on stabilize (unchanged behavior).
      Verify: all rows match `specs/surface-visualization/spec.md`
      (MODIFIED requirement scenarios).
- [x] 3.3 Run the `reviewer` subagent on the complete diff (correctness +
      regression risk per repo policy; explicitly confirm the stability and
      try-lock skips are untouched and no lock-pairing regression exists).
      Integrate accepted findings. Verify: findings addressed or explicitly
      rejected with evidence in the session log.
