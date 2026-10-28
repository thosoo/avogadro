# Proposal: Fix modern-tier bond pick names and missing axes cylinders

## Why

Two user-visible defects on the modern rendering tier make basic interaction
untrustworthy: clicking **any** bond with the DrawTool cycles the order of the
**first** bond of the molecule instead of the clicked one, and the navigation
axes glyph in the bottom-left corner renders without its cylinder shafts
(only the center sphere and cone tips appear). Both break contracts the
`gpu-rendering-pipeline` spec already claims: pick hits must resolve the
clicked primitive, and the axes overlay must render identically on every tier.

## What Changes

- **Bond pick names are emitted.** The engine emission paths (`BSDYEngine::renderOpaque`
  / `renderTransparent`, and the stick/sphere engines) call `Painter::setName(...)`
  per primitive before drawing, so `SceneBuilder` stamps the real atom/bond
  primitive index into every instance's `pickId`. Today `m_currentId` stays 0
  for the whole frame, so every cylinder pick resolves to bond 0.
- **Scene snapshots carry pick ids.** `SnapshotAtom`/`SnapshotBond` gain an
  explicit primitive-index field and the worker tessellator sets names on the
  builder, so the (default) worker adoption path produces the same correct
  pick names as the synchronous path.
- **Pick-name regression test with a multi-bond molecule.** The existing pick
  tests use single-bond scenes where the wrong name (0) is indistinguishable
  from the right one; a ≥3-bond scene asserting the clicked bond's index fails
  on the current code.
- **Modern tier unbinds GL buffer bindings at frame/pick end.** `drawInstances`,
  the dirty-range uploads, and the pick pass leave `GL_ARRAY_BUFFER` pointing
  at an instance VBO in the default VAO; the frame-end state-restore contract
  is extended to buffer bindings.
- **Legacy display-list compilation is defensive.** `Cylinder::initialize()`
  explicitly unbinds `GL_ARRAY_BUFFER` before compiling its client-pointer
  vertex arrays, so any current or future offender cannot poison the baked
  display list (this is what erases the axes cylinder shafts today).
- **Axes regression assertion.** A headless check that `GL_ARRAY_BUFFER_BINDING`
  is 0 after a modern-tier frame and after `pick()`, guarding the state-restore
  contract mechanically.

Existing picking, rendering, and overlay behavior is otherwise unchanged; the
compat and legacy tiers are audited for the same leak and get the same unbind
discipline if affected.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `gpu-rendering-pipeline`:
  - **ADDED** requirement: pick hits identify the clicked primitive — a hit's
    name is the primitive's own index (clicking bond K yields K), independent
    of how many instances/segments the primitive was drawn as.
  - **MODIFIED** requirement "Modern tier restores GL program and attribute
    state after drawing": the restore contract is extended to GL buffer
    bindings (`GL_ARRAY_BUFFER`), and the axes glyph scenario now explicitly
    covers the cylinder shafts rendering.

## Impact

- **Modified code:**
  - `libavogadro/src/engines/bsdyengine.cpp` — `setName` calls in
    `renderOpaque`/`renderTransparent`; same audit for `stickengine.cpp`,
    `sphereengine.cpp`, `wireengine.cpp`.
  - `libavogadro/src/rendering/scenesnapshot.h` — pick-id fields on
    `SnapshotAtom`/`SnapshotBond`; `libavogadro/src/rendering/snapshottessellator.cpp`
    — set names during emission.
  - `libavogadro/src/rendering/modernbackend.cpp` — unbind `GL_ARRAY_BUFFER`
    at the end of `drawInstances`, `pushDirtyRanges` uploads, and `pick()`.
  - `libavogadro/src/cylinder_p.cpp` — defensive `glBindBuffer(GL_ARRAY_BUFFER, 0)`
    in `Cylinder::initialize()`.
  - `libavogadro/src/rendering/compatbackend.cpp` — leak audit (fix if affected).
  - `libavogadro/tests/modernpicktest.cpp` (or a sibling test) — multi-bond
    pick-name cases; buffer-binding assertion in the modern-backend harness.
- **No changes:** shaders' visual output, sphere pick mechanism
  (`gl_InstanceID`, documented as deliberate), camera/picking rectangle
  geometry, OpenBabel, tools.
- **Risk area per repo policy:** shared C++/Qt compiled by the Windows
  installer job — the Windows leg must be run before merge if MSVC is not
  available locally.
