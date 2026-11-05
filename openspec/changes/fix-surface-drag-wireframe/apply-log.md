# Apply log: fix-surface-drag-wireframe

## Reviewer integration (task 3.3)

The `reviewer` subagent (glm-5.3-flash) reviewed the complete diff
(`libavogadro/src/glwidget.cpp` 18+/14−, `glwidget.h` 5+/4−). Verdict: **OK
with notes** — no blockers, no majors. All six requested checkpoints verified
against live source:

1. Stability skip (`inst.mesh == nullptr || !inst.mesh->stable()`) UNTOUCHED.
2. Try-lock skip (`tryLockForRead()`) UNTOUCHED, no lock-pairing regression;
   `unlock()` still executes on every path (the only inserted code is a local
   `const int` computation).
3. Culling (`glEnable(GL_CULL_FACE)/glCullFace(GL_BACK)/glDisable`) and blending
   blocks UNTOUCHED.
4. `flushMode` reduces to `inst.mode` when `m_cameraInteracting` is false —
   byte-identical at-rest behavior.
5. `drawMesh` and `drawColorMesh` both implement mode 1 (`GL_LINE`, lighting
   off, state restored on exit).
6. No MSVC/const-correctness concerns (no new mutex, no lock from const method,
   no aggregate-init of user-provided-special-member types).

Findings disposition:

- **Finding 1 (minor, report-only)** — `flushMode` forces 1 even when
  `inst.mode == 2` (points); legacy `renderQuick` preserves points during drags
  (`surfaceengine.cpp:179-181`). **Accepted as by-design**: design D1 states
  exactly `m_cameraInteracting ? 1 : inst.mode`, and the spec delta calls for a
  wireframe net during motion. Not changed (would require a design-D1
  amendment).
- **Finding 2 (minor, docs-only)** — design.md D4/Risks claimed the colored net
  "matches how legacy renders quick-mode colored meshes", but legacy
  `renderQuick` draws colored meshes via `drawMesh` with a solid
  `m_posColor`/`m_negColor` (not `drawColorMesh`). **Addressed**: the design.md
  Risks note was corrected to state the divergence is accepted per D4.
- **Finding 3 (minor, practically unreachable)** — `drawColorMesh` immediate
  fallback under mode 1 renders in leftover raster color when GL buffer
  functions are unavailable; only reachable on a fixed-function context without
  VBO functions, which the modern/compat tiers always provide. **Rejected as
  no-action**: pre-existing painter code, theoretical, not exercised by the
  changed path.
- **Finding 4 (nit)** — flag arms on any held button, not only camera drags;
  pre-existing trigger (the old code hid surfaces during those drags, so
  hidden→wireframe is an improvement). **Accepted as no-action**; noted for the
  3.2 manual matrix.
- **Finding 5 (info)** — `m_cameraInteracting` is an unsynchronized bool; race
  only under `ENABLE_THREADED_GL` (OFF by default, `CMakeLists.txt:491-493`).
  **Rejected as no-action**: pre-existing pattern, not introduced by this diff.

## Manual matrix (task 3.2) environment note

Design D6 records that offscreen/CI cannot drive real GLWidget mouse drags.
Attempted under Xvfb: the app launches on the modern tier, loads
`testfiles/koffein_orca.out`, precalculates orbital cubes/meshes, and a
synthesized camera drag does not crash. However, the Xvfb/llvmpipe context
resolves to **Core profile** (GL 4.5 Core), and `GLPainterPrivate::
coreFixedFunctionAvailable()` returns false on a Core context — so
`drawMesh`/`drawColorMesh` (and therefore the surface-mesh flush) bail out and
surfaces are not rendered at all in this environment. This matches design D6:
the manual matrix requires a real display with a **Compatibility profile**
context where surfaces render and mouse drags are user-driven. The matrix
cannot be completed headlessly; it remains a human verification step.
