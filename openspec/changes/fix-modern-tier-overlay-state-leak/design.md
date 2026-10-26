# Design

## Context

The reporting machine's white-background capture (commit `ac487391e`, Quadro
P3200 / NVIDIA 573.22) shows the axes navigation glyph displaced to the scene
center as a black ring of triangle fans, atom-sized and molecule-anchored, and
the debug HUD text vanishing once the scene's first atom exists. Static trace:
`ModernRendererBackend::endFrame` binds the sphere program (line 761) and the
cylinder program (line 803) and never releases either — unlike the file's other
bind sites (348/351, 622/648, 686/715) — and leaves the instancing attributes
(2–5) enabled with divisors set. The frame's subsequent fixed-function draws
(axes overlay via `glBegin`, debug/extension text via QPainter, tool overlay
paint) therefore execute with the leaked program active: vertices transformed
by the stale molecule MVP (displacing the glyph to the molecule's focus,
atom-sized, molecule-anchored) and colored by the unbound per-instance color
attribute (constant black). With an empty scene (`m_sphereCount == 0`) no bind
occurs, which is exactly why both overlays break "after drawing the first
atom".

## Goals / Non-Goals

**Goals:**
- `endFrame` restores the program and vertex-attribute state it binds.
- Overlays render correctly on the modern tier with scene content present.
- Contained to `modernbackend.cpp` `endFrame`.

**Non-Goals:**
- Routing the axes overlay or text through the SceneBuilder capture layer
  (larger redesign; unnecessary once state is restored).
- Changing shaders, instance layout, or the draw order.

## Decisions

1. **Release after each draw block.** After the sphere instanced draw:
   `m_sphereGeom.program.release()`. After the cylinder/line instanced draws:
   `m_cylinderGeom.program.release()`. This matches the release discipline the
   file's other bind sites already follow and costs one `glUseProgram(0)` per
   pass.
2. **Disable instance attributes and reset divisors after the last use.** At
   the end of each draw block: `glDisableVertexAttribArray(2..5)` and
   `glVertexAttribDivisor(2..5, 0)` (sphere block: 2–4; cylinder block: 2–5).
   Enabled generic attributes pointing at the instance VBO would otherwise
   poison the fixed-function overlay draws and QPainter's interleaved GL work
   even with the program released.
3. **No change to the early-return path.** `endFrame`'s early return (backend
   invalid) happens before any bind, so no restoration is needed there; a
   program bind failure returns before drawing, leaving the previously bound
   program (none of ours) active.

## Risks / Trade-offs

- [Per-block release adds two `glUseProgram(0)` calls per frame] → negligible;
  the overlays already switch programs implicitly today (brokenly).
- [Attribute disable/reset may affect a later modern-tier pass within the same
  frame] → each draw block re-enables its own attributes explicitly (the
  proven-safe pattern already in place), so restoring between blocks cannot
  starve a later block.

## Migration Plan

Single commit; no data migration. Local build + full ctest green, both CI legs
green, then the white-background user check: axes glyph in its corner, debug
text persistent after the first atom, black ring gone. Rollback = revert.

## Open Questions

- None. The defect, mechanism, and fix site are pinned by the source and the
  on-machine observation.
