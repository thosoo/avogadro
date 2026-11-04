# Design: Fix orbital mesh rendering on the modern tier

## Context

Pipeline trace (verified live under xvfb with an ORCA 6.1 methane output that parses
cleanly): queue calculation completes → `MeshGenerator` writes vertices + normals and
**no colors** → engine enabled with mesh pointers → `emitSceneToBuilder()` runs the
transparent pass → `SurfaceEngine::renderTransparent` → `SceneBuilder::drawMesh` →
`if (!mesh.valid()) return;` → `Mesh::valid()` (`mesh.cpp:236`) requires
`colors.size() == 1 || colors.size() == vertices.size()` → **0 colors matches neither →
mesh silently dropped** → `flushCapturedMeshes()` sees an empty store → nothing paints.
No diagnostic is emitted anywhere.

Constraints that shape the fix:

- `GLPainter` (legacy/compat painting, `glpainter_p.cpp`) already defines the correct
  semantics for uncolored meshes: `haveColors = false; // no per-vertex colors to
  stream` — it streams vertices/normals and applies the painter's current color.
- `SceneBuilder` mesh instances already carry an instance color (`MeshInstance.color`,
  filled from the painter's current color at `drawMesh` time), and
  `flushCapturedMeshes()` draws uncolored instances with `inst.color`. Only the
  capture gate is broken.
- Existing mesh tests build fixtures **with** colors (`setColors`), which is why every
  current test passes while every real orbital mesh fails.

## Goals / Non-Goals

**Goals:**

- Uncolored meshes (vertices + normals, zero per-vertex colors) render on modern and
  compat tiers with their instance color — parity with the legacy painter.
- No silent mesh loss: a genuinely invalid mesh is reported once, per the existing
  "no silent primitive loss" requirement.
- Regression coverage with the mesh shape reality produces (uncolored
  `MeshGenerator` product), not just colored fixtures.
- MSVC portability of the change verified on CI.

**Non-Goals:**

- Changes to the orbital queue/click logic (`orbitalextension.cpp`) — verified correct.
- The second startup GLWidget's backend that fails initialization and never retries.
- The precalc queue's duplicate-priority `QHash` collapse (HOMO computed last).
- ORCA parse/availability reporting (separate change, `fix-orca-orbital-availability`).
- ESP-colored mesh rendering — already works and must stay untouched.

## Decisions

### D1: Widen `Mesh::valid()` — uncolored means renderable

`Mesh::valid()` additionally accepts `colors.size() == 0` when vertex and normal counts
agree. Rationale: this matches the semantics the legacy painter has always applied
(empty colors = "stream geometry only, use current color"), keeps a single predicate at
the single choke point both emitter entry points consult, and cannot regress colored
meshes (the `== 1` and `== vertices.size()` branches stay). Alternatives rejected:
per-call-site color checks in `SceneBuilder::drawMesh`/`drawColorMesh` (duplicates the
predicate, and any future capture path repeats the bug), and having `MeshGenerator`
emit a dummy color (mutates data to satisfy a bad predicate; ESP-colored meshes would
then see `colors.size() == 1` ambiguity — a single-color mesh is a legitimate distinct
case).

### D2: Instance color is the fallback path — no engine changes needed

For uncolored meshes the engine already calls `setColor(...)` before `drawMesh`, and
`SceneBuilder::drawMesh` copies the painter's current color into `MeshInstance.color`;
`flushCapturedMeshes()` draws with `inst.color`. `SurfaceEngine::readSettings` already
resets `m_colored = false` when the mesh has no colors, so uncolored meshes take the
`drawMesh` (not `drawColorMesh`) path. No engine, shader, or backend change is
required; the fix is the acceptance predicate plus reporting.

### D3: Report genuinely invalid meshes once

A mesh whose vertex/normal counts disagree still cannot be drawn; per the existing
"no silent primitive loss" requirement, `SceneBuilder::drawMesh`/`drawColorMesh` emit a
one-time diagnostic naming the mesh and the reason (static-flag pattern, matching the
"reported (once) as omitted" convention used elsewhere in the emitter). Spec-scenario
coverage: the uncolored-mesh scenario renders, the corrupt-mesh scenario reports.

### D4: Regression test at the capture boundary with a real generator product

New headless test (own binary, wired in `libavogadro/tests/CMakeLists.txt` following
the existing pattern of compiling non-exported rendering sources directly):
generate an uncolored mesh via `MeshGenerator` from a small cube (the production shape —
no `setColors` call), assert `Mesh::valid()` is true, capture it through
`SceneBuilder::drawMesh`/`drawColorMesh`, and assert the mesh store holds the instance
with the expected color. Two negative assertions: a corrupt mesh (mismatched
vertex/normal counts) is not captured and the diagnostic fired exactly once. This is
the first test in the repo whose fixture matches what orbital surfaces actually look
like.

### D5: Build and MSVC verification contract

- **Build command is fixed at `-j8`**: `cmake --build build --parallel 8` (and
  `ctest --test-dir build --output-on-failure`). No other parallelism level is used for
  this change's builds.
- **The branch is pushed to GitHub and the Windows Installer workflow is taken from
  Actions before merge.** The change modifies C++ (`mesh.cpp`, `scenebuilder.cpp`, test
  wiring) that the Windows installer job compiles, and Linux CI does not prove MSVC
  correctness (repo policy: const-correctness, aggregate-init differences). So: push
  the branch, run the Windows installer workflow (`.github/workflows/windows-installer.yml`,
  workflow id `168745168`, `gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro`),
  and require both `build-installer` jobs green **and** the full test suite (including
  `autooptreproTest`) passing under `QT_QPA_PLATFORM=offscreen`. A red Windows leg
  blocks merge exactly as a red Linux leg does.

## Risks / Trade-offs

- [`valid()` semantics widen] → Colored-mesh branches unchanged; existing colored
  fixtures keep passing (test suite is the guard). Callers of `valid()` were audited:
  only the SceneBuilder capture gates consume it.
- [One-time diagnostic could hide repeated corruption] → Accepted: the convention is
  "report once" repo-wide; spamming per-frame diagnostics is worse. The test pins the
  exactly-once behavior.
- [Test binary needs non-exported sources] → Pattern exists (meshstream/consumerparity
  compile `glpainter_p.cpp` et al. directly); follow it, including the GLU link rule if
  `glpainter_p.cpp` is pulled in.
- [MSVC-only compile error slips Linux CI] → Mitigated by D5's mandatory
  windows-installer run on the pushed branch before merge.

## Migration Plan

Single self-contained fix; no data or settings migration. Rollback = revert the commit.

## Open Questions

None material.
