# Proposal: Fix surface/orbital visualization performance and responsiveness

## Why

Visualizing orbitals from quantum-chemistry output (ORCA, Gaussian, ...) with
resolution "Very High" (0.05 Å grid) is unusable: the GUI locks up for minutes
at every phase boundary and again when the surface finally appears, and
rotating the molecule with a displayed surface lags badly — even though
rendering runs on the modern instanced engine with a dedicated GPU. Root cause
analysis (session 2026-10-30) traced both symptoms to concrete defects: O(N)
work on the GUI thread, grid-proportional transient allocations reaching
10–20 GB, single-threaded marching cubes with per-vertex trilinear normals,
unscreened per-point basis evaluation, per-frame full-mesh redraw through the
fixed-function path, and a background "precalculate orbitals" queue that
repeats everything ×10 at the user's persisted quality setting.

## What Changes

- **GUI thread never blocks on surface work.** The gaussian-shell vector
  allocation/init, the OpenQube→Avogadro cube data copy, and ESP vertex
  coloring move off the GUI thread; progress updates are rate-limited.
- **Bounded memory.** Marching cubes reserves sane initial buffers and grows
  geometrically (no `dim³×3` reserve); MO+ / MO− generators run sequentially
  instead of concurrently; cube data becomes single precision; meshes move
  into `Mesh` instead of deep-copying.
- **Interaction stays smooth with surfaces displayed.** Meshes draw without
  full-mesh per-frame client-array uploads; backface culling applies to
  closed surfaces; while the camera is moving, surfaces render simplified
  (deferred full redraw on rest); mesh VBO uploads happen once per assembled
  mesh (single revision bump).
- **Faster math.** Distance screening skips negligible primitives per grid
  point; the electron-density path stops heap-allocating per point; marching
  cubes parallelizes over disjoint slabs.
- **Sane precalculation policy.** Background orbital precalculation uses at
  most Medium resolution regardless of the persisted display quality; a
  specific orbital is still recalculated at the requested quality on demand
  (existing cube reuse by `(orbital, resolution)` is preserved).
- All existing gestures of the surface dialog, stale-callback document-switch
  guards (`document-switch-stability`), and rendered surface appearance are
  unchanged apart from the interaction-time simplification above.

## Capabilities

### New Capabilities

- `surface-visualization`: interaction contract of surface calculation and
  display — GUI responsiveness during calculation, bounded memory, rotation
  performance with displayed surfaces, deterministic and screened results,
  and the orbital precalculation policy.

### Modified Capabilities

- (none — `document-switch-stability`'s existing teardown/stale-callback
  requirements already govern this area and are preserved as-is; the change
  must not regress them, verified as scenarios inside the new capability)

## Impact

- **Modified:** `libavogadro/src/extensions/surfaces/openqube/gaussianset.cpp`
  (threading of setup, screening, per-point allocation removal),
  `libavogadro/src/extensions/surfaces/orbitalextension.cpp` (cube handoff,
  precalc policy, progress throttling), `.../surfaceextension.cpp` (same
  handoff + ESP threading), `libavogadro/src/meshgenerator.{h,cpp}` (reserve,
  parallel slabs), `libavogadro/src/mesh.{h,cpp}` (move setters, single
  revision bump), `libavogadro/src/cube.{h,cpp}` (float data),
  `libavogadro/src/glpainter_p.cpp` (culling, single-upload), `libavogadro/src/glwidget.cpp`
  (interaction flag around `flushCapturedMeshes`).
- **New:** headless unit tests in `libavogadro/tests/` (screening tolerance,
  marching-cubes determinism/finiteness, precalc-policy clamp).
- **No changes:** `extern/openbabel` (bundled submodule, untouched), rendering
  tiers/backends beyond the mesh flush, picking, camera paths, draw tool.
