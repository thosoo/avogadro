# Design: Fix modern-tier bond pick names and missing axes cylinders

## Context

Two independently rooted defects (see proposal.md):

**A. Bond picks resolve to bond 0.** The pick pipeline is split-brained:

```
emission path                          pick pass (modernbackend.cpp::pick)
─────────────────                      ──────────────────────────────────
bsdyengine.cpp renderOpaque/           sphere shader (shaders.h kSpherePickVertexShader):
renderTransparent draw atoms/bonds       vPickId = uPickBase + uint(gl_InstanceID)
WITHOUT setName()                 ──▶    ← atoms work by luck: instance i ≈ atom i
SceneBuilder::begin() resets
m_currentId = 0 (scenebuilder.cpp      cylinder shader (kCylinderPickVertexShader):
:402-403); appendBond stamps             vPickId = uPickBase + aInstPickId
inst.pickId = m_currentId                      ← aInstPickId is ALWAYS 0
(scenebuilder.cpp:477)
```

`Painter::setName()` exists for exactly this and `BSDYEngine::renderPick`
(bsdyengine.cpp:413, 421) honors it — but only in the legacy GL_SELECT scene.
The modern emission reuses `renderOpaque`, which historically never needed
names. `SceneBuilder::m_bondPickIds` / `m_spherePickIds` are filled from the
same stale `m_currentId`.

**B. Axes cylinder shafts never draw.** The three axes primitives use
different legacy draw mechanisms (glwidget.cpp:2424 `renderAxesOverlay` →
GLPainter, the real painter on all tiers):

| primitive | mechanism | survives the leak? |
|---|---|---|
| sphere | display list of `glBegin(GL_TRIANGLE_FAN)` (sphere_p.cpp:118) | yes — immediate mode ignores `GL_ARRAY_BUFFER` |
| cone | immediate-mode triangle loop (glpainter_p.cpp:637) | yes |
| cylinder | display list of `glVertexPointer`(**client memory**) + `glDrawArrays(GL_QUAD_STRIP)` (cylinder_p.cpp:144–149) | **no** |

`ModernRendererBackend` leaves a non-zero `GL_ARRAY_BUFFER` binding in the
default VAO: `pushDirtyRanges` binds instance VBOs for upload with VAO 0
active (modernbackend.cpp:90/126), and `drawInstances`/`pick` bind geometry +
instance VBOs inside their own VAOs but never unbind afterwards. When the
axes overlay first compiles the Cylinder display list (first paint — GLPainter
is constructed without a context, so `glGenLists` fails and the lazy
`d->isValid()` re-init happens mid-frame), the client pointer is interpreted
as a byte offset into the still-bound instance VBO → `GL_INVALID_OPERATION`,
and the garbage snapshot is baked into the display list permanently
(`Cylinder::setup` short-circuits on `isValid`). The pick pass has the same
leak.

This is the same bug family as the already-fixed "allocate() writes to
whatever is currently bound" incident (modernbackend.cpp sphere VBO comment)
and the `fix-modern-tier-overlay-state-leak` work — which restored programs,
attributes, and divisors but not buffer bindings.

## Goals / Non-Goals

**Goals:**
- Clicking bond K on the modern tier yields hit name K, on both the sync and
  worker paths.
- Axes cylinder shafts render on the modern tier.
- Mechanical regression tests for both (the current tests are structurally
  blind — see Risks).
- No behavior change on the legacy tier; compat tier verified unchanged.

**Non-Goals:**
- Reworking the sphere pick mechanism (`gl_InstanceID`): atoms pick correctly
  today for plain scenes. Limitations (atom images resolve to atom indices
  ≥ N; non-index-ordered emission) are documented, not fixed.
- Making wireframe (line) primitives pickable — lines are not in the pick
  pass at all today; unchanged.
- Per-pixel pick refinement, depth-peeling, or changing the pick rectangle
  geometry/slop.
- Migrating GLPainter's display lists off fixed function (frozen legacy
  contract).

## Decisions

### D1: Engines set names — the Painter contract, not a backend lookup table

`bsdyengine.cpp renderOpaque()` and the bond-drawing half of
`renderTransparent()` call, per primitive, before its draw calls:

```cpp
foreach(const Bond *b, bonds()) {
  ...
  pd->painter()->setName(b);              // ← added: SceneBuilder::setName(const Primitive*)
  pd->painter()->setColor(...);           // (unchanged)
  pd->painter()->drawMultiCylinder(v1, v3, m_bondRadius, order, shift);
  pd->painter()->drawMultiCylinder(v3, v2, m_bondRadius, order, shift);
}
foreach(const Atom *a, allAtoms) {
  pd->painter()->setName(a);              // ← added: honest, no behavior change
  pd->painter()->setColor(...);
  pd->painter()->drawSphere(a->pos(), radius(a));
}
```

`SceneBuilder::setName(const Primitive*)` (scenebuilder.cpp:73) already maps
`Atom*→index()`, `Bond*→index()` via `qobject_cast`, and `appendBond`
(scenebuilder.cpp:477) already stamps it into every half-segment instance —
so the emission side is a pure call-site fix. Note GLPainter::setName only
stores an id consumed by drawSphere/drawCylinder when the painter is in the
GL_SELECT (picking) scene (d->id != -1); in the normal fixed-function
GL_RENDER path it issues balanced pushName()/popName() pairs, so the legacy
tier's rendering output is unchanged (the now-set GL_INVALID_OPERATION bits
match the pre-existing stick/sphere-engine pattern and are read by no one).
See the residual-risk note under Risks.

Alternatives rejected:
- *Backend resolves instance→bond via `m_bondPickIds`*: drags molecule
  knowledge across the backend boundary; `m_bondPickIds` already contains the
  answer the shader should have read. The `pickId` attribute is the designed
  channel (modernbackend.cpp:557 comment); feed it, don't bypass it.
- *Cylinder pick shader switches to `gl_InstanceID` + CPU-side segment→bond
  map*: bonds are 1→N instances (multi-bond splitting), so the map would be a
  per-instance array the backend must keep in sync — a worse version of D3.

Audit and apply the same pattern to `stickengine.cpp` (bonds as cylinders)
and `sphereengine.cpp` (atoms) emission paths. `wireengine` draws lines only
(not pickable) — leave it, note in code comment.

### D2: Snapshot schema carries pick ids — the worker path must be as honest as the sync path

`workerPathCapable()` (glwidget.cpp) makes worker snapshot tessellation the
default path for plain BSDY scenes, so fixing only `renderOpaque` fixes the
rare path. `SceneSnapshot` (scenesnapshot.h) gains explicit indices:

```cpp
struct SnapshotAtom {
  float pos[3];
  unsigned char atomicNumber;
  unsigned int index;        // ← added: atom primitive index
};
struct SnapshotBond {
  unsigned int begin, end;
  unsigned int order;
  unsigned int index;        // ← added: bond primitive index
};
```

The snapshot taker fills them from the live molecule under the read lock
(currently implicit: position in `QVector` ≈ index — made explicit so future
emission-order changes can't silently break picking). `snapshottessellator.cpp`
sets names before each draw, mirroring D1:

```cpp
builder.setName(Primitive::BondType, sb.index);
builder.drawMultiCylinder(...);
...
builder.setName(Primitive::AtomType, ca.index);
builder.drawSphere(...);
```

(Uses the `(Primitive::Type, int)` overload — the tessellator has indices,
not `Primitive*`.) `SceneBuilder::begin()` resetting `m_currentId` stays as
the belt-and-suspenders "unnamed draws are obvious" signal: with D1/D2 in
place an unsetting path would fail the new tests loudly instead of silently
resolving to 0.

### D3: Unbind GL_ARRAY_BUFFER where the modern tier last touches it

Three sites in `modernbackend.cpp`, each restoring the binding it consumed:

1. `pushDirtyRanges` (per `InstanceVBO` upload loop): after the final
   `glBufferSubData`, `m_gl.glBindBuffer(GL_ARRAY_BUFFER, 0)` (uploads run
   with VAO 0 — this is where the default-VAO poisoning starts).
2. End of `drawInstances()` (after both sphere and cylinder blocks, and the
   early-return-safe position): `glBindBuffer(GL_ARRAY_BUFFER, 0)`.
3. End of `pick()`, after `readPickHitsRect` / framebuffer restore:
   same unbind (the pick pass binds geometry + instance VBOs inside the
   shared VAOs and leaves the last bind current).

Also unbind `GL_ELEMENT_ARRAY_BUFFER` is NOT needed at these sites: index
VBO binds happen inside the geometry VAOs (per-VAO state), and VAO 0's
element binding is never touched. Assert this in the new test anyway
(cheap).

Mechanical guard (test, not runtime): after a submitted frame and after a
`pick()`, `glGetIntegerv(GL_ARRAY_BUFFER_BINDING) == 0`.

### D4: Cylinder::initialize() compiles display lists defensively

In `cylinder_p.cpp initialize()`, immediately before `glVertexPointer`:

```cpp
glBindBuffer(GL_ARRAY_BUFFER, 0);   // client pointers, not VBO offsets
glEnableClientState(GL_VERTEX_ARRAY);
...
```

Rationale: the display-list snapshot (client arrays are dereferenced at
compile time — which is why `freeBuffers()` after `glEndList()` is legal)
must never depend on the caller's `ARRAY_BUFFER` binding. This protects every
GLPainter consumer on every tier from any current or future offender — the
modern-tier leak is just the one we know about. Sphere/cone don't need it
(immediate mode); if a future primitive uses client arrays, same discipline
applies.

Note: an already-poisoned process (axes broken at runtime) stays broken until
restart — `Cylinder::setup` short-circuits on `isValid`. Not worth runtime
re-validation; the fix makes fresh sessions correct.

### D5: Regression tests target the blindness, not just the fix

The current tests pass on broken code because their scenes have exactly one
bond (modernpicktest.cpp:155 `buildTwoAtomBond`), making name 0 correct by
coincidence. New cases:

1. **Multi-bond pick names** (extend `modernpicktest.cpp`, or a new
   `modernpicknames` entry following the same offscreen-GL harness pattern):
   build a 4-atom chain (3 bonds) spanning the frame; `pick()` the middle of
   each bond in turn; assert `GLHit::type() == Primitive::BondType` and
   `name() == bond index` for first/middle/last, plus both half-segments of a
   double bond resolving to one index. Fails on current code (all return 0).
2. **Worker-path names** (headless, no GL): tessellate a `SceneSnapshot` of a
   3-bond molecule into a `SceneBuilder`, then read the emitted
   `ChunkStore<BondInstance>` records and assert each segment's
   `pickId == its bond index`. Also fails today (all 0).
3. **Buffer-binding guard** (modern-backend GL harness, e.g. in
   `compatibilitytest.cpp` next to the existing modern-frame tests): submit a
   frame and run a `pick()`, then assert `GL_ARRAY_BUFFER_BINDING == 0`
   (and `GL_ELEMENT_ARRAY_BUFFER_BINDING == 0` on the default VAO).
4. **Axes shafts (manual matrix item):** the axes overlay draws through
   immediate mode/display lists the headless harness cannot easily
   pixel-verify; cover with a manual check on the modern tier (shafts visible)
   + the D3 mechanical test as the automated half.

### D6: Compat tier gets the same audit, fix only if guilty

`compatbackend.cpp` pushes into VBOs the same way; if it also leaves
`GL_ARRAY_BUFFER` bound, the axes break identically on compat and D4 masks
the shafts there instead of fixing the leak. Audit and apply the D3 unbind
discipline symmetrically if affected — spec scenarios are tier-generic for
the overlay and pick requirements.

## Risks / Trade-offs

- [setName on the legacy tier changes GL_SELECT names?] → GLPainter::setName
  stores an id consumed by pushName/popName only in the GL_SELECT picking
  scene; renderPick already sets names before its draws, so the legacy pick
  scene is unchanged. In GL_RENDER mode the added push/pop pairs are balanced
  and leave rendering output identical (they set, but never clear, balanced
  GL error bits — see the note below). `drawcommand` / `autooptrepro` suites
  guard this.
- [Modern tier leaves GL error bits after each BSDY frame?] → In GL_RENDER
  mode the added pushName()/popName() pairs set (but never clear) balanced
  GL_INVALID_OPERATION bits, matching the pre-existing stick/sphere-engine
  pattern (glpainter_p.cpp drawSphere/drawCylinder). No legacy consumer reads
  errors, the push/pop stack stays balanced (no growth), and rendering output
  is byte-identical, so this is a documentation-precision note, not a
  regression. The pick-name requirement is about the hit NAME, not GL error
  state, so it is unaffected.
- [Spheres-only modern-tier frame leaks GL_ARRAY_BUFFER?] → the drawInstances
  unbind runs unconditionally at the end of the function (after both the
  sphere and cylinder blocks), so a bondless scene (atoms, zero bonds/lines,
  which still renders the axes glyph) also restores ARRAY_BUFFER_BINDING to 0.
  The in-block program-bind-failure `return`s precede any instance-VBO bind,
  so they leak nothing; the only residual case (sphere block binds, then
  cylinder program bind fails and returns early) is a pre-existing rare GL
  error path, not introduced here.
- [Instance-order assumptions elsewhere rely on pickId == 0?] → pickId is
  consumed only by the cylinder pick shader; nothing else reads it. Tests
  asserting current (wrong) behavior would be asserting the bug — none found.
- [Snapshot schema change breaks binary compat of persisted state?] →
  `SceneSnapshot` is in-memory only (never serialized); rebuild is per-frame.
- [Unbinding ARRAY_BUFFER regresses modern-tier draws?] → each modern-tier
  draw site binds what it needs explicitly (proven-safe pattern already in
  drawInstances); the unbinds happen after the last use in each site (after
  pushDirtyRanges' upload loop, after pick()'s framebuffer restore, and
  unconditionally at the end of drawInstances after both instance blocks).
  The buffer-binding guard test pins it.
- [Multi-bond pick test is flaky across GL stacks?] → reuses the existing
  offscreen-GL harness (software GL on CI) that modernpick already requires;
  QSKIP when no GL 3.3 context, as today.
- [Windows/MSVC divergence] → changes are plain C++/Qt; per repo policy, if
  MSVC is unavailable locally, run the Windows installer workflow
  (`gh workflow run 168745168 --ref <branch>`) before merge.

## Migration Plan

Self-contained fix; no data or settings migration. Rollback = revert the
commit. The snapshot schema change and the emission changes land together
(partial landing leaves one path fixed and the other visibly broken — the
tests in D5 forbid it).

## Open Questions

None material. (If the compat-tier audit in D6 finds the same leak, it is
fixed under the same spec scenarios — no new decision needed.)
