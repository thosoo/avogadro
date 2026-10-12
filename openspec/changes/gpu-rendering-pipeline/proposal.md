## Why

The renderer is fixed-function immediate mode: every frame walks every atom and bond with
~8-12 legacy GL calls each (`bsdyengine.cpp:132-196`, `sphere_p.cpp:73`), recompiles a
whole-scene display list during every dragged frame (`glwidget.cpp:294-314`), deep-copies
mesh vertices per frame (`glpainter_p.cpp:1016`), and paints on the GUI thread — so a big
protein (bacteriophage T4 gp23 scale and up) makes the whole application slow and sloppy,
not just the viewport. Static analysis also found selection checks that are O(scene ×
selection) per frame (`glwidget.cpp:2573`) and a picking path that silently truncates on
large scenes (`glwidget.cpp:2305-2365`, cap at `global.h:116`). Patching these preserves a
dead-end technology; the long-term goal is a modern, threaded rendering pipeline with a
60fps target.

## What Changes

- Add a tiered rendering architecture behind a backend seam: shared scene model
  (snapshot → chunked instance buffers → dirty ranges → LOD bands) with swappable submit
  stages.
- New default tier (`modern`, GL 3.3+ compatibility profile): shader/instanced rendering of
  atoms and bonds (one draw call per primitive type), worker-thread chunk builders with
  time-sliced GPU uploads, CPU per-chunk frustum culling, zoom-band LOD with hysteresis.
- Replace `GL_SELECT` picking on the modern tier with a shader color-ID pass (scissored at
  the cursor); keep GL_SELECT (with overflow detection) only in lower tiers.
- Stream meshes to VBOs once when stable, eliminating the per-frame vertex copy.
- Make selection checks O(1) (hash set instead of QList::contains).
- Compatibility tier (`compat`, GL 2.0 + VBO): batched fixed-function flush of the same
  chunks for pre-3.3 iGPUs; minimal, bugfix-only.
- Legacy tier (`legacy`): today's renderer verbatim, frozen — the macOS path and the
  support escape hatch; its known pathologies are accepted, not fixed.
- Tier selection via CMake flags (`AVO_RENDERER_TIER2/TIER1/LEGACY`), capability probing at
  startup, and the `AVOGADRO_RENDERER=modern|compat|legacy` override; the chosen tier is
  logged using the diagnostics from `force-dedicated-gpu`.
- Define (but do not build) a GPU-driven Tier 3 (GL 4.3+: compute culling, indirect draws)
  as a growth path that Tier 2's chunk model must not preclude.
- Non-goals: Vulkan/D3D12/Metal (Qt RHI) migration; fixing Tier 0 pathologies; macOS on
  the modern tier; render-path performance work beyond the new pipeline.

## Capabilities

### New Capabilities
- `gpu-rendering-pipeline`: Tiered GPU rendering with a modern instanced default tier,
  60fps targets, non-blocking GUI-thread submission, scalable selection and picking, and
  frozen compatibility tiers for old hardware.

### Modified Capabilities

## Impact

- New rendering core in `libavogadro/src/` (scene snapshot, chunk/instance buffers, backend
  interface, modern/compat backends, builder threads).
- `libavogadro/src/glwidget.cpp` — context/profile request, paint path delegates to the
  active backend, picking path replacement on the modern tier.
- Engines ported incrementally to the emitter API (BSDY ball&stick, sphere, stick, wire
  first; surface and cartoon/ribbon mesh paths); unported engines keep rendering through
  the compatibility bridge (mixed fixed-function in the same compatibility context).
- `libavogadro/src/mesh.cpp` / `glpainter_p.cpp` — stable-mesh streaming replaces the
  per-frame copy.
- `CMakeLists.txt` — tier flags; `global.h` selection cap superseded on the modern tier.
- CI (MSVC, `windows-installer.yml`) unchanged; validation adds forced-tier runs and an
  FPS harness; no dedicated old-hardware requirement (Tier 1 via forced-tier runs).
