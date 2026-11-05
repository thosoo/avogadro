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

## Manual matrix (task 3.2) — verified under Xvfb + llvmpipe (Compatibility context)

Design D6 records that offscreen/CI cannot drive real GLWidget mouse drags.
That is true of `QT_QPA_PLATFORM=offscreen`, but a **real X server** under
Xvfb with a running window manager *can* drive the app and its mouse drags.
The blocker was the GL context: the default llvmpipe context resolves to
**Core profile**, on which `GLPainterPrivate::coreFixedFunctionAvailable()`
returns false, so the surface-mesh flush bails out entirely. Forcing
`MESA_GL_VERSION_OVERRIDE=3.3COMPAT` resolves a **Compatibility profile**
context (`GLWidget initialised... renderer tier = "modern" (instanced)`), on
which surfaces render and mouse drags are user-driven via `xdotool`. The manual
matrix was then executed as follows (app loads `testfiles/koffein_orca.out`,
precalculates orbital cubes/meshes, an orbital row is selected from the
Orbitals dock table so a surface is displayed, and a camera drag is driven by
pressing, moving, and releasing the pointer over the viewport).

**How the wireframe vs solid distinction is measured.** Colored (non-gray)
pixels in the viewport and their `interior_ratio` (fraction of colored pixels
whose 4 orthogonal neighbors are also colored) are used as a proxy: a solid
`GL_FILL` surface is a dense fill (high interior_ratio ~0.92), while a
`GL_LINE` wireframe net is sparse lines (interior_ratio drops sharply). A
fixed-angle comparison isolates the wireframe toggle from camera-rotation noise
by pressing and moving the pointer only 2 px (the camera barely rotates).

| Matrix row | Result | Evidence |
|---|---|---|
| 1. ORCA orbital, tilt/rotate: wireframe net during drag; solid on release | **PASS** | Modern tier, fixed-angle test: solid interior_ratio 0.916 → **wireframe 0.640** during drag → 0.917 after release (colored 60721 → 48125 → 61344). |
| 2. Same on compat tier (`AVOGADRO_RENDERER=compat`) | **PASS** | Compat tier (`renderer tier = "compat"`): interior_ratio 0.918 → **0.576** during drag → 0.917 after release (colored 90169 → 62739 → 90083). |
| 3. ESP-colored surface: colored net during motion | **Covered by shared code path** | ESP meshes draw via `drawColorMesh`; reviewer confirmed `drawColorMesh` implements mode 1 (`GL_LINE`, lighting off, per-vertex colors preserved). Same `flushMode` substitution applies. Not separately exercised via the surfaces dialog (see note). |
| 4. At rest identical to pre-change (solid, translucent, culled) | **PASS** | `flushMode = m_cameraInteracting ? 1 : inst.mode`; when not interacting `flushMode == inst.mode` (byte-identical path, reviewer checkpoint 4). At rest the surface renders solid (interior_ratio ~0.92). |
| 5. Rapid tilt-then-stop: no frame without the surface; no hide-then-reappear | **PASS** | Continuous 8-step drag, captured between steps: colored pixels 48552/67205/77656/82909/85710/85884/85497/86685 — surface present in every frame (never 0). |
| 6. Surface recalculated mid-drag: absent only while unstable | **Unchanged** | The stability skip and try-lock skip are untouched (reviewer checkpoints 1–2); this is pre-existing behavior the change does not alter. |

### Rows 3/6 note

Row 3 (ESP surface) is verified through the shared `drawColorMesh` mode-1 path
plus the reviewer's confirmation, not by driving the `Create Surfaces...`
dialog to generate an ESP mesh headlessly (the dialog's surface/color combos
are not reliably navigable without a screen/OCR). Row 6 is unchanged behavior.
Both rows are documented as such rather than claimed as empirically exercised.
