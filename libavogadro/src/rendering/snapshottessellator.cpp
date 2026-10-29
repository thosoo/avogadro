/**********************************************************************
  SnapshotTessellator implementation (see snapshottessellator.h).

  Reproduces BSDYEngine::renderOpaque / renderTransparent from an immutable
  snapshot + GUI-captured JobParams. It drives the SceneBuilder emitter (which
  appends byte-identical ChunkStore records to what the GUI-thread engine path
  produces), so parity is structural: same draw calls => same records.
 *****************************************************************/

#include "snapshottessellator.h"

#include <Eigen/Geometry>

#include <avogadro/global.h>

#include "scenebuilder.h"

namespace Avogadro {
namespace rendering {

namespace {

inline Eigen::Vector3d posOf(const SceneSnapshot &snapshot, unsigned int id)
{
  const auto &a = snapshot.atoms.value(static_cast<int>(id));
  return Eigen::Vector3d(a.pos[0], a.pos[1], a.pos[2]);
}

//! Append the opaque bonds: each bond is two halves meeting at the radius
//! bisector point v3, exactly as BSDYEngine::renderOpaque emits them.
void emitOpaqueBonds(SceneBuilder &builder, const SceneSnapshot &snapshot,
                     const JobParams &p)
{
  const TessellatorSettings &s = p.settings;
  for (const auto &b : snapshot.bonds) {
    const Eigen::Vector3d v1 = posOf(snapshot, b.begin);
    const Eigen::Vector3d v2 = posOf(snapshot, b.end);
    Eigen::Vector3d dir = v2 - v1;
    const double len = dir.norm();
    if (len < 1e-9)
      continue;
    dir /= len;
    const Eigen::Vector3d v3 = (v1 + v2 + dir * (p.atoms.value(b.begin).radius
                                                 - p.atoms.value(b.end).radius)) / 2.0;

    const int order = (s.showMulti != 0) ? b.order : 1;

    // Pick name: name this bond before its two halves so BOTH half-segments
    // carry this bond's molecule index (design D2). Without it every half-
    // segment inherits pickId 0 and every worker-path bond click hits bond 0.
    builder.setName(Primitive::BondType, static_cast<int>(b.index));

    const CapturedAtom &c1 = p.atoms.value(b.begin);
    builder.setColor(c1.rgba[0], c1.rgba[1], c1.rgba[2], c1.rgba[3]);
    builder.drawMultiCylinder(v1, v3, s.bondRadius, order, s.shift);

    const CapturedAtom &c2 = p.atoms.value(b.end);
    builder.setColor(c2.rgba[0], c2.rgba[1], c2.rgba[2], c2.rgba[3]);
    builder.drawMultiCylinder(v3, v2, s.bondRadius, order, s.shift);
  }
}

//! Append the opaque atoms (position from the snapshot, colour/radius captured).
void emitOpaqueAtoms(SceneBuilder &builder, const SceneSnapshot &snapshot,
                     const JobParams &p)
{
  for (int i = 0; i < snapshot.atoms.size(); ++i) {
    const auto &sa = snapshot.atoms[i];
    const auto &ca = p.atoms[i];
    const Eigen::Vector3d pos(sa.pos[0], sa.pos[1], sa.pos[2]);
    // Pick name: name each atom with its molecule index before its sphere
    // (design D2), so a click resolves to this atom.
    builder.setName(Primitive::AtomType, static_cast<int>(sa.index));
    builder.setColor(ca.rgba[0], ca.rgba[1], ca.rgba[2], ca.rgba[3]);
    builder.drawSphere(pos, ca.radius);
  }
}

//! Append the transparent atoms: atoms with a translucent colour (0<alpha<1)
//! plus selection halos for selected atoms (always, matching the engine).
void emitTransparentAtoms(SceneBuilder &builder, const SceneSnapshot &snapshot,
                          const JobParams &p)
{
  const TessellatorSettings &s = p.settings;
  const QColor selection = QColor::fromRgbF(0.1f, 0.8f, 0.9f, 1.0f);
  for (int i = 0; i < snapshot.atoms.size(); ++i) {
    const auto &sa = snapshot.atoms[i];
    const auto &ca = p.atoms[i];
    const Eigen::Vector3d pos(sa.pos[0], sa.pos[1], sa.pos[2]);
    // Pick name: name this atom before its translucent half and selection halo
    // so both resolve to this atom's molecule index (design D2).
    builder.setName(Primitive::AtomType, static_cast<int>(sa.index));

    if (s.alpha > 0.001 && s.alpha < 0.999) {
      builder.setColor(ca.rgba[0], ca.rgba[1], ca.rgba[2], s.alpha);
      builder.drawSphere(pos, ca.radius);
    }

    if (p.selectedAtoms.contains(static_cast<unsigned int>(i))) {
      builder.setColor(selection.redF(), selection.greenF(),
                       selection.blueF(), selection.alphaF());
      builder.drawSphere(pos, SEL_ATOM_EXTRA_RADIUS + ca.radius);
    }
  }
}

//! Append the transparent bonds: translucent halves when 0<alpha<1, plus the
//! selected-bond halo for selected bonds.
void emitTransparentBonds(SceneBuilder &builder, const SceneSnapshot &snapshot,
                          const JobParams &p)
{
  const TessellatorSettings &s = p.settings;
  const QColor selection = QColor::fromRgbF(0.1f, 0.8f, 0.9f, 1.0f);
  int bondIndex = 0;
  for (const auto &b : snapshot.bonds) {
    const bool selected = p.selectedBonds.contains(bondIndex);
    if (!selected && s.alpha > 0.999)
      continue;

    const Eigen::Vector3d v1 = posOf(snapshot, b.begin);
    const Eigen::Vector3d v2 = posOf(snapshot, b.end);
    Eigen::Vector3d dir = v2 - v1;
    const double len = dir.norm();
    if (len < 1e-9)
      continue;
    dir /= len;
    const Eigen::Vector3d v3 = (v1 + v2 + dir * (p.atoms.value(b.begin).radius
                                                 - p.atoms.value(b.end).radius)) / 2.0;
    const int order = (s.showMulti != 0) ? b.order : 1;

    // Pick name: name this bond before its translucent halves and selection
    // halo so every half-segment resolves to this bond's molecule index
    // (design D2).
    builder.setName(Primitive::BondType, static_cast<int>(b.index));

    if (s.alpha > 0.001 && s.alpha < 0.999) {
      const CapturedAtom &c1 = p.atoms.value(b.begin);
      builder.setColor(c1.rgba[0], c1.rgba[1], c1.rgba[2], s.alpha);
      builder.drawMultiCylinder(v1, v3, s.bondRadius, order, s.shift);

      const CapturedAtom &c2 = p.atoms.value(b.end);
      builder.setColor(c2.rgba[0], c2.rgba[1], c2.rgba[2], s.alpha);
      builder.drawMultiCylinder(v3, v2, s.bondRadius, order, s.shift);
    }

    if (selected) {
      builder.setColor(selection.redF(), selection.greenF(),
                       selection.blueF(), selection.alphaF());
      builder.drawMultiCylinder(v1, v2, SEL_BOND_EXTRA_RADIUS + s.bondRadius,
                                order, s.shift);
    }
    ++bondIndex;
  }
}

} // namespace

TessellationResult snapshotTessellateBSDY(const SceneSnapshot &snapshot,
                                          const JobParams &params)
{
  TessellationResult result;

  SceneBuilder builder;
  builder.begin();
  builder.clearStores();

  // Opaque pass (engine returns early when the molecule is translucent).
  if (params.settings.alpha > 0.999) {
    emitOpaqueBonds(builder, snapshot, params);
    emitOpaqueAtoms(builder, snapshot, params);
  }

  // Transparent pass (translucent atoms, selection halos, selected bonds).
  emitTransparentAtoms(builder, snapshot, params);
  emitTransparentBonds(builder, snapshot, params);

  builder.end();

  result.spheres = builder.spheres();
  result.bonds = builder.bonds();
  result.lines = builder.lines();
  return result;
}

} // namespace rendering
} // namespace Avogadro
