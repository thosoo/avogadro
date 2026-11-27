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
#include "bondspread.h"

namespace Avogadro {
namespace rendering {

namespace {

inline Eigen::Vector3d posOf(const SceneSnapshot &snapshot, unsigned int id)
{
  const auto &a = snapshot.atoms.value(static_cast<int>(id));
  return Eigen::Vector3d(a.pos[0], a.pos[1], a.pos[2]);
}

//! Build the per-bond local sp2 spread normal for every bond in the snapshot.
//!
//! The multi-bond spread direction is a chemistry question: the parallel
//! cylinders of a multiple bond should lie in the bond's local sp2 plane,
//! perpendicular to the bond axis, rather than in a world-frame or global
//! best-fit-plane direction (fix-double-bond-offset-plane, design D1/D3). A
//! single adjacency pass over the snapshot bonds collects each bonded atom's
//! neighbors (excluding the bond partner), and the shared bondSpreadNormal
//! helper turns them into the unit spread vector. Degenerate bonds (no
//! substituents / collinear) yield a zero vector, so the emitter falls back to
//! its pre-change behaviour (design D2).
QVector<Eigen::Vector3d> buildBondSpreadNormals(const SceneSnapshot &snapshot)
{
  const int nAtoms = snapshot.atoms.size();
  QVector<QVector<int>> neighbors(nAtoms);
  for (const auto &b : snapshot.bonds) {
    // Guard the adjacency index (operator[] is UB on an out-of-range index)
    // the same way posOf() tolerates a missing atom; app-built snapshots always
    // resolve, but a malformed snapshot must not fault.
    if (b.begin < nAtoms && b.end < nAtoms) {
      neighbors[static_cast<int>(b.begin)].append(static_cast<int>(b.end));
      neighbors[static_cast<int>(b.end)].append(static_cast<int>(b.begin));
    }
  }

  QVector<Eigen::Vector3d> normals;
  normals.reserve(snapshot.bonds.size());
  for (const auto &b : snapshot.bonds) {
    const int begin = static_cast<int>(b.begin);
    const int end = static_cast<int>(b.end);
    QVector<Eigen::Vector3d> beginNbrs;
    QVector<Eigen::Vector3d> endNbrs;
    for (int nb : neighbors[begin])
      if (nb != end)
        beginNbrs.append(posOf(snapshot, static_cast<unsigned int>(nb)));
    for (int nb : neighbors[end])
      if (nb != begin)
        endNbrs.append(posOf(snapshot, static_cast<unsigned int>(nb)));
    normals.append(bondSpreadNormal(posOf(snapshot, b.begin),
                                    posOf(snapshot, b.end), beginNbrs,
                                    endNbrs));
  }
  return normals;
}

//! Append the opaque bonds: each bond is two halves meeting at the radius
//! bisector point v3, exactly as BSDYEngine::renderOpaque emits them.
void emitOpaqueBonds(SceneBuilder &builder, const SceneSnapshot &snapshot,
                     const JobParams &p,
                     const QVector<Eigen::Vector3d> &spreadNormals)
{
  const TessellatorSettings &s = p.settings;
  for (int bi = 0; bi < snapshot.bonds.size(); ++bi) {
    const SnapshotBond &b = snapshot.bonds[bi];
    const Eigen::Vector3d spreadNormal = spreadNormals[bi];
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
    builder.drawMultiCylinder(v1, v3, s.bondRadius, order, s.shift, spreadNormal);

    const CapturedAtom &c2 = p.atoms.value(b.end);
    builder.setColor(c2.rgba[0], c2.rgba[1], c2.rgba[2], c2.rgba[3]);
    builder.drawMultiCylinder(v3, v2, s.bondRadius, order, s.shift, spreadNormal);
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
                          const JobParams &p,
                          const QVector<Eigen::Vector3d> &spreadNormals)
{
  const TessellatorSettings &s = p.settings;
  const QColor selection = QColor::fromRgbF(0.1f, 0.8f, 0.9f, 1.0f);
  for (int bi = 0; bi < snapshot.bonds.size(); ++bi) {
    const SnapshotBond &b = snapshot.bonds[bi];
    const Eigen::Vector3d spreadNormal = spreadNormals[bi];
    const bool selected = p.selectedBonds.contains(bi);
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
      builder.drawMultiCylinder(v1, v3, s.bondRadius, order, s.shift, spreadNormal);

      const CapturedAtom &c2 = p.atoms.value(b.end);
      builder.setColor(c2.rgba[0], c2.rgba[1], c2.rgba[2], s.alpha);
      builder.drawMultiCylinder(v3, v2, s.bondRadius, order, s.shift, spreadNormal);
    }

    if (selected) {
      builder.setColor(selection.redF(), selection.greenF(),
                       selection.blueF(), selection.alphaF());
      builder.drawMultiCylinder(v1, v2, SEL_BOND_EXTRA_RADIUS + s.bondRadius,
                                order, s.shift, spreadNormal);
    }
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

  // Per-build neighbor pass: compute each multiple bond's local sp2 spread
  // normal once, so the modern tier follows the bond's substituent geometry
  // instead of the world frame (fix-double-bond-offset-plane, design D1/D3).
  const QVector<Eigen::Vector3d> spreadNormals = buildBondSpreadNormals(snapshot);

  // Opaque pass (engine returns early when the molecule is translucent).
  if (params.settings.alpha > 0.999) {
    emitOpaqueBonds(builder, snapshot, params, spreadNormals);
    emitOpaqueAtoms(builder, snapshot, params);
  }

  // Transparent pass (translucent atoms, selection halos, selected bonds).
  emitTransparentAtoms(builder, snapshot, params);
  emitTransparentBonds(builder, snapshot, params, spreadNormals);

  builder.end();

  result.spheres = builder.spheres();
  result.bonds = builder.bonds();
  result.lines = builder.lines();
  return result;
}

} // namespace rendering
} // namespace Avogadro
