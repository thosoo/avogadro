# Proposal: Fix orbital mesh rendering on the modern tier

## Why

Computed orbital surfaces never render on the modern tier: `SceneBuilder::drawMesh`
silently drops every mesh that `Mesh::valid()` rejects, and `Mesh::valid()` rejects
uncolored meshes — exactly what `MeshGenerator` produces (vertices + normals, zero
per-vertex colors). Every orbital click therefore passes all logic guards (queue,
pending-orbital, engine lookup, progress reporting) and still paints nothing, with no
diagnostic anywhere. Verified live under xvfb: an ORCA 6 output parses, the table
populates, calculations complete, "Attempting to render orbital N" fires — and the
canvas shows bare geometry.

## What Changes

- **Uncolored meshes render on the modern tier.** `Mesh::valid()` (or its call sites)
  accepts meshes with zero per-vertex colors, treating them as "solid instance color"
  — the semantics `GLPainter` (legacy) has always applied (`haveColors = false; // no
  per-vertex colors to stream`). `MeshGenerator` products, and thus orbital, VdW and
  density surfaces, become renderable on the modern and compat tiers.
- **No silent mesh loss at capture.** `SceneBuilder::drawMesh`/`drawColorMesh` never
  drop a mesh silently: a rejected mesh is reported once (diagnostic), per the existing
  "no silent primitive loss" requirement, and an end-to-end path exists that renders a
  real `MeshGenerator` product through `SceneBuilder` capture + `GLWidget::
  flushCapturedMeshes` so the class of failure is covered by tests rather than by
  colored test fixtures.
- **Test fixtures reflect reality.** New regression coverage uses an uncolored
  `MeshGenerator` product (the shape every orbital mesh has); existing colored-fixture
  tests are unchanged.

Out of scope (separate follow-up changes): the ORCA output print-directive /
silent-parse-failure reporting work (`fix-orca-orbital-availability` candidate), the
second startup GLWidget's backend that fails initialization and never retries
("ModernRendererBackend: no current GL context"), and the precalc queue's
duplicate-priority `QHash` collapse that defers HOMO to last.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `gpu-rendering-pipeline`: the "Captured meshes render on the modern tier" requirement
  is tightened so that uncolored meshes (no per-vertex colors) — the shape
  `MeshGenerator` produces for orbital, VdW, and density surfaces — are accepted and
  rendered with their instance color on every tier, and the emitter never drops a mesh
  silently.

## Impact

- **Code:** `libavogadro/src/mesh.cpp` (`Mesh::valid()` semantics) or the
  `SceneBuilder::drawMesh`/`drawColorMesh` gates in
  `libavogadro/src/rendering/scenebuilder.cpp`; optionally a one-time diagnostic.
  `libavogadro/src/glpainter_p.cpp` and the engine layer are already correct for
  uncolored meshes and stay untouched.
- **Tests:** `libavogadro/tests/` — new end-to-end regression (uncolored
  MeshGenerator mesh → SceneBuilder capture → flush), wired in
  `libavogadro/tests/CMakeLists.txt` following the existing mesh-test pattern.
- **No changes:** queue/click logic in `orbitalextension.cpp` (verified correct in the
  live repro), OpenQube, shaders, render tiers' frame loops.
- **Risk:** low — the fix widens an acceptance predicate; existing colored-mesh
  behavior (ESP surfaces) is unchanged and remains covered by current tests.
