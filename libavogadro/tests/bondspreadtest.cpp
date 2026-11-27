/**********************************************************************
  bondspreadtest - multi-bond spread-plane tests (fix-double-bond-offset-plane).

  Covers:
    - unit tests for the shared bondSpreadNormal helper (benzene in a non-xy
      world plane, terminal carbonyl, degenerate fallback, determinism);
    - a scene-level test that the snapshot tessellator spreads every double
      bond of an xz-plane benzene within the ring plane;
    - a tier-parity test that the modern emitter and the legacy
      Cylinder::drawMulti formula (axis x planeNormal) produce the same spread
      plane.

  Copyright (C) 2026 Avogadro contributors

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 *****************************************************************/

#include "config.h"

#include <QtTest>

#include <cmath>
#include <QMap>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include "libavogadro/src/rendering/bondspread.h"
#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/rendering/snapshottessellator.h"
#include "libavogadro/src/rendering/scenesnapshot.h"
#include "libavogadro/src/rendering/records.h"

using namespace Avogadro;
using namespace Avogadro::rendering;
using Eigen::Vector3d;

namespace {

const double kPi = 3.14159265358979323846;

//! Build a planar benzene ring in the world xz plane (ring normal = +Y).
//! The six carbons sit on a circle of radius R in the xz plane. Kekulé: bonds
//! 0-1, 2-3, 4-5 are double (order 2); the rest are single.
void buildBenzeneXZ(QVector<Vector3d> &carbons, QVector<SnapshotBond> &bonds)
{
  const double R = 1.4;
  carbons.clear();
  for (int k = 0; k < 6; ++k) {
    const double a = k * (2.0 * kPi / 6.0);
    carbons.append(Vector3d(R * std::cos(a), 0.0, R * std::sin(a)));
  }

  bonds.clear();
  for (int k = 0; k < 6; ++k) {
    SnapshotBond b;
    b.begin = static_cast<unsigned int>(k);
    b.end = static_cast<unsigned int>((k + 1) % 6);
    b.order = (k % 2 == 0) ? 2u : 1u; // 0-1, 2-3, 4-5 double
    b.index = static_cast<unsigned int>(k);
    bonds.append(b);
  }
}

//! Build an idealized benzene ring in the world xz plane WITH a radial
//! hydrogen on each carbon (atoms 0..5 = carbons, atoms 6..11 = hydrogens).
//! This is the real sp2 scenario: each double-bonded carbon carries a ring
//! neighbour AND a hydrogen at ±120° from the bond axis, whose raw cross
//! products cancel without sign alignment (oracle finding).
void buildBenzeneWithH(QVector<Vector3d> &atoms, QVector<SnapshotBond> &bonds)
{
  const double R = 1.4;
  const double rCH = 1.1;
  atoms.clear();
  bonds.clear();
  for (int k = 0; k < 6; ++k) {
    const double a = k * (2.0 * kPi / 6.0);
    atoms.append(Vector3d(R * std::cos(a), 0.0, R * std::sin(a)));
  }
  for (int k = 0; k < 6; ++k) {
    const double a = k * (2.0 * kPi / 6.0);
    atoms.append(Vector3d((R + rCH) * std::cos(a), 0.0, (R + rCH) * std::sin(a)));
  }
  // Kekulé ring bonds (carbons 0..5).
  for (int k = 0; k < 6; ++k) {
    SnapshotBond b;
    b.begin = static_cast<unsigned int>(k);
    b.end = static_cast<unsigned int>((k + 1) % 6);
    b.order = (k % 2 == 0) ? 2u : 1u;
    b.index = static_cast<unsigned int>(k);
    bonds.append(b);
  }
  // C-H single bonds (hydrogens 6..11).
  for (int k = 0; k < 6; ++k) {
    SnapshotBond b;
    b.begin = static_cast<unsigned int>(k);
    b.end = static_cast<unsigned int>(6 + k);
    b.order = 1u;
    b.index = static_cast<unsigned int>(6 + k);
    bonds.append(b);
  }
}

//! Given the BondInstances that belong to one multiple bond (same pickId),
//! return the spread direction: the normalized vector between the two offset
//! copies of the first half (the two instances whose starts are nearest the
//! bond's begin atom). The two copies differ by 2*shift along the spread axis.
Vector3d spreadDirOf(const QVector<BondInstance> &insts, const Vector3d &beginPos)
{
  if (insts.size() < 2)
    return Vector3d(0.0, 0.0, 0.0);
  int i0 = 0, i1 = 1;
  double best = -1.0;
  for (int i = 0; i < insts.size(); ++i) {
    const Vector3d s(insts[i].start[0], insts[i].start[1], insts[i].start[2]);
    const double d = (s - beginPos).squaredNorm();
    if (best < 0.0 || d < best) {
      best = d;
      i0 = i;
    }
  }
  double best2 = -1.0;
  for (int i = 0; i < insts.size(); ++i) {
    if (i == i0)
      continue;
    const Vector3d s(insts[i].start[0], insts[i].start[1], insts[i].start[2]);
    const double d = (s - beginPos).squaredNorm();
    if (best2 < 0.0 || d < best2) {
      best2 = d;
      i1 = i;
    }
  }
  const Vector3d d(insts[i0].start[0] - insts[i1].start[0],
                   insts[i0].start[1] - insts[i1].start[1],
                   insts[i0].start[2] - insts[i1].start[2]);
  return d.normalized();
}

} // namespace

class BondSpreadTest : public QObject
{
  Q_OBJECT

private slots:
  void benzeneSpreadLiesInRingPlane();
  void terminalCarbonylSpread();
  void degenerateFallback();
  void deterministicSpread();
  void snapshotBenzeneSpreadsInRingPlane();
  void tierParitySpreadPlane();
  void symmetricSp2WithHydrogenDoesNotCancel();
  void etheneAndKetoneDoNotCancel();
};

//! Task 1.2: a benzene ring in the world xz plane spreads each double bond in
//! the ring plane (perpendicular to the axis and to the ring normal), not out
//! of the molecular plane.
void BondSpreadTest::benzeneSpreadLiesInRingPlane()
{
  QVector<Vector3d> c;
  QVector<SnapshotBond> bonds;
  buildBenzeneXZ(c, bonds);

  for (const SnapshotBond &b : bonds) {
    if (b.order < 2)
      continue;
    QVector<Vector3d> beginNbrs, endNbrs;
    for (const SnapshotBond &o : bonds) {
      if (o.begin == b.begin && o.end != b.end)
        beginNbrs.append(c[o.end]);
      else if (o.end == b.begin && o.begin != b.end)
        beginNbrs.append(c[o.begin]);
      if (o.begin == b.end && o.end != b.begin)
        endNbrs.append(c[o.end]);
      else if (o.end == b.end && o.begin != b.begin)
        endNbrs.append(c[o.begin]);
    }

    const Vector3d n = bondSpreadNormal(c[b.begin], c[b.end], beginNbrs, endNbrs);
    QVERIFY(n.squaredNorm() > 1e-12); // a ring bond always has a plane

    const Vector3d axis = (c[b.end] - c[b.begin]).normalized();
    // The plane normal is perpendicular to the bond axis.
    QVERIFY(std::abs(n.dot(axis)) < 1e-6);

    // The actual spread direction is axis x planeNormal. It must lie in the
    // ring plane: perpendicular to the ring normal (+Y) and to the axis.
    const Vector3d spread = axis.cross(n);
    QVERIFY(spread.squaredNorm() > 1e-12);
    QVERIFY(std::abs(spread.dot(Vector3d(0.0, 1.0, 0.0))) < 1e-6);
    QVERIFY(std::abs(spread.dot(axis)) < 1e-6);
  }
}

//! Task 1.2: a terminal double bond (one substituted atom, one without) spreads
//! in the plane defined by the substituted atom's neighbour and the bond axis.
void BondSpreadTest::terminalCarbonylSpread()
{
  // C=O: begin atom C at origin, end atom O along +Z. C has one substituent at
  // +X, so the carbonyl plane is the xz plane (y = 0). O has no substituents.
  const Vector3d c(0.0, 0.0, 0.0);
  const Vector3d o(0.0, 0.0, 1.2);
  QVector<Vector3d> beginNbrs;
  beginNbrs << Vector3d(1.0, 0.0, 0.0);
  QVector<Vector3d> endNbrs; // none

  const Vector3d n = bondSpreadNormal(c, o, beginNbrs, endNbrs);
  QVERIFY(n.squaredNorm() > 1e-12); // one substituent is enough to define a plane

  const Vector3d axis = (o - c).normalized();
  QVERIFY(std::abs(n.dot(axis)) < 1e-6);

  const Vector3d spread = axis.cross(n);
  QVERIFY(spread.squaredNorm() > 1e-12);
  // The spread lies in the carbonyl plane (xz), so it is perpendicular to the
  // plane normal (y).
  QVERIFY(std::abs(spread.dot(Vector3d(0.0, 1.0, 0.0))) < 1e-6);
  QVERIFY(std::abs(spread.dot(axis)) < 1e-6);
}

//! Task 1.2: degenerate input (isolated diatomic, collinear substituents)
//! yields a deterministic zero fallback and does not crash.
void BondSpreadTest::degenerateFallback()
{
  QVector<Vector3d> empty;

  // Isolated diatomic: no substituents at all -> zero vector, no crash.
  const Vector3d n1 = bondSpreadNormal(Vector3d(0, 0, 0), Vector3d(1, 0, 0),
                                       empty, empty);
  QCOMPARE(n1.squaredNorm(), 0.0);

  // Collinear substituent (along the axis) -> cross product cancels to zero.
  QVector<Vector3d> collinear;
  collinear << Vector3d(2, 0, 0);
  const Vector3d n2 = bondSpreadNormal(Vector3d(0, 0, 0), Vector3d(1, 0, 0),
                                       collinear, empty);
  QCOMPARE(n2.squaredNorm(), 0.0);

  // Degenerate bond axis (identical positions) -> zero, no crash.
  const Vector3d n3 = bondSpreadNormal(Vector3d(1, 1, 1), Vector3d(1, 1, 1),
                                       empty, empty);
  QCOMPARE(n3.squaredNorm(), 0.0);
}

//! Task 1.2: identical input yields bitwise-identical output (determinism).
void BondSpreadTest::deterministicSpread()
{
  QVector<Vector3d> c;
  QVector<SnapshotBond> bonds;
  buildBenzeneXZ(c, bonds);

  QVector<Vector3d> beginNbrs, endNbrs;
  const SnapshotBond &b = bonds[0]; // C0-C1, double
  for (const SnapshotBond &o : bonds) {
    if (o.begin == b.begin && o.end != b.end)
      beginNbrs.append(c[o.end]);
    else if (o.end == b.begin && o.begin != b.end)
      beginNbrs.append(c[o.begin]);
    if (o.begin == b.end && o.end != b.begin)
      endNbrs.append(c[o.end]);
    else if (o.end == b.end && o.begin != b.begin)
      endNbrs.append(c[o.begin]);
  }

  const Vector3d n1 = bondSpreadNormal(c[b.begin], c[b.end], beginNbrs, endNbrs);
  const Vector3d n2 = bondSpreadNormal(c[b.begin], c[b.end], beginNbrs, endNbrs);
  QVERIFY(n1 == n2); // bitwise identical
}

//! Task 2.3 / 4.3: tessellate a snapshot of a benzene ring and assert every
//! double bond's second cylinder is displaced within the RING plane (offset
//! perpendicular to the bond axis and to the ring normal) rather than out of
//! the molecular plane. Checked for two orientations: the world xz plane and a
//! genuinely arbitrary rotated orientation, so the check is independent of how
//! the molecule is oriented in the world frame.
void BondSpreadTest::snapshotBenzeneSpreadsInRingPlane()
{
  QVector<Vector3d> c;
  QVector<SnapshotBond> bonds;
  buildBenzeneXZ(c, bonds);

  // Ring plane normal from the atom positions (cross of two adjacent edges).
  const auto ringNormal = [&c]() -> Vector3d {
    const Vector3d e1 = (c[1] - c[0]).normalized();
    const Vector3d e2 = (c[2] - c[1]).normalized();
    return e1.cross(e2).normalized();
  };

  const auto checkOrientation = [&](const QVector<Vector3d> &atoms,
                                    const Vector3d &normal) {
    SceneSnapshot snap;
    snap.revision = 1;
    for (const Vector3d &p : atoms) {
      SnapshotAtom a;
      a.pos[0] = static_cast<float>(p.x());
      a.pos[1] = static_cast<float>(p.y());
      a.pos[2] = static_cast<float>(p.z());
      a.atomicNumber = 6;
      a.index = static_cast<unsigned int>(snap.atoms.size());
      snap.atoms.append(a);
    }
    snap.bonds = bonds;

    JobParams params;
    params.settings.alpha = 1.0f;
    params.settings.showMulti = 2;
    params.settings.bondRadius = 0.1f;
    params.settings.shift = 0.15f;
    for (int i = 0; i < snap.atoms.size(); ++i) {
      CapturedAtom ca;
      ca.radius = 0.3f;
      ca.rgba[0] = ca.rgba[1] = ca.rgba[2] = ca.rgba[3] = 1.0f;
      params.atoms.append(ca);
    }

    const TessellationResult result = snapshotTessellateBSDY(snap, params);

    // Group the emitted bond instances by their bond pickId.
    QMap<quint32, QVector<BondInstance>> byBond;
    for (size_t i = 0; i < result.bonds.size(); ++i) {
      const BondInstance &bi = result.bonds.at(i);
      byBond[bi.pickId].append(bi);
    }

    // For each double bond, the two offset copies of its first half must be
    // displaced within the ring plane: perpendicular to the axis and to the
    // ring normal.
    for (const SnapshotBond &b : bonds) {
      if (b.order < 2)
        continue;
      const auto it = byBond.constFind(b.index);
      QVERIFY(it != byBond.constEnd());
      const QVector<BondInstance> &insts = it.value();

      const Vector3d beginPos(atoms[b.begin].x(), atoms[b.begin].y(),
                              atoms[b.begin].z());
      const Vector3d spread = spreadDirOf(insts, beginPos);
      QVERIFY(spread.squaredNorm() > 1e-12);

      const Vector3d axis = (atoms[b.end] - atoms[b.begin]).normalized();
      QVERIFY(std::abs(spread.dot(axis)) < 1e-3);
      QVERIFY(std::abs(spread.dot(normal)) < 1e-3);
    }
  };

  // Orientation 1: ring in the world xz plane (normal = +Y).
  checkOrientation(c, ringNormal());

  // Orientation 2: rotate the whole ring by a non-trivial rotation, so the
  // ring normal points along a genuinely arbitrary direction (not axis-aligned).
  const Eigen::Matrix3d rot = Eigen::AngleAxisd(0.7, Vector3d(1.0, 2.0, 3.0).normalized())
                                .toRotationMatrix();
  QVector<Vector3d> rotated;
  for (const Vector3d &p : c)
    rotated.append(rot * p);
  checkOrientation(rotated, (rot * ringNormal()).normalized());
}

//! Task 3.3: for the same test bond, the modern emitter (SceneBuilder) and the
//! legacy Cylinder::drawMulti formula (axis x planeNormal) produce the same
//! spread plane: the emitted offset direction is parallel to axis x planeNormal.
void BondSpreadTest::tierParitySpreadPlane()
{
  QVector<Vector3d> c;
  QVector<SnapshotBond> bonds;
  buildBenzeneXZ(c, bonds);

  const SnapshotBond &b = bonds[0]; // C0-C1, double
  QVector<Vector3d> beginNbrs, endNbrs;
  for (const SnapshotBond &o : bonds) {
    if (o.begin == b.begin && o.end != b.end)
      beginNbrs.append(c[o.end]);
    else if (o.end == b.begin && o.begin != b.end)
      beginNbrs.append(c[o.begin]);
    if (o.begin == b.end && o.end != b.begin)
      endNbrs.append(c[o.end]);
    else if (o.end == b.end && o.begin != b.begin)
      endNbrs.append(c[o.begin]);
  }

  const Vector3d planeNormal = bondSpreadNormal(c[b.begin], c[b.end],
                                                beginNbrs, endNbrs);
  const Vector3d axis = (c[b.end] - c[b.begin]).normalized();

  // Modern tier: emit through the SceneBuilder and read the spread direction.
  SceneBuilder builder;
  builder.begin();
  builder.clearStores();
  builder.drawMultiCylinder(c[b.begin], c[b.end], 0.1, 2, 0.15, planeNormal);
  builder.end();

  // Order 2 produces two instances; their start difference is the offset.
  const BondInstance &a1 = builder.bonds().at(0);
  const BondInstance &a2 = builder.bonds().at(1);
  const Vector3d modernSpread(a1.start[0] - a2.start[0],
                              a1.start[1] - a2.start[1],
                              a1.start[2] - a2.start[2]);
  const Vector3d modernDir = modernSpread.normalized();

  // Legacy tier: Cylinder::drawMulti computes ortho1 = axis x planeNormal (its
  // planeNormalVector parameter). Verify the modern direction is parallel to
  // that exact formula.
  const Vector3d legacyDir = axis.cross(planeNormal).normalized();

  QVERIFY(modernDir.squaredNorm() > 1e-12);
  QVERIFY(legacyDir.squaredNorm() > 1e-12);
  // Parallel within tolerance (allow for sign; the straddle is symmetric).
  const double dot = std::abs(modernDir.dot(legacyDir));
  QVERIFY(dot > 0.999);
}

//! Regression for the oracle finding on fix-double-bond-offset-plane: at an
//! idealized sp2 centre with an explicit hydrogen, the two substituents sit at
//! ±120° from the bond axis, so their raw `substituent x axis` cross terms are
//! anti-parallel and cancel to exactly zero — silently falling back to the old
//! world-frame behaviour for the headline scenario (benzene with H). The
//! helper sign-aligns each term, so it must return the ring plane normal, not
//! the zero sentinel.
void BondSpreadTest::symmetricSp2WithHydrogenDoesNotCancel()
{
  QVector<Vector3d> atoms;
  QVector<SnapshotBond> bonds;
  buildBenzeneWithH(atoms, bonds);

  const int begin = 0, end = 1; // C0-C1, double
  QVector<Vector3d> beginNbrs, endNbrs;
  for (const SnapshotBond &b : bonds) {
    if (b.begin == begin && b.end != end)
      beginNbrs.append(atoms[b.end]);
    else if (b.end == begin && b.begin != end)
      beginNbrs.append(atoms[b.begin]);
    if (b.begin == end && b.end != begin)
      endNbrs.append(atoms[b.end]);
    else if (b.end == end && b.begin != begin)
      endNbrs.append(atoms[b.begin]);
  }

  const Vector3d n = bondSpreadNormal(atoms[begin], atoms[end], beginNbrs, endNbrs);

  // The symmetric sp2 centre with explicit H must NOT cancel to the zero
  // sentinel (this is the regression): it must return a real plane normal.
  QVERIFY(n.squaredNorm() > 1e-12);

  // That normal is perpendicular to the bond axis and lies along the ring
  // normal (the benzene ring is in the world xz plane, so the ring normal is
  // +/-Y), not in some world-frame direction that cancels.
  const Vector3d axis = (atoms[end] - atoms[begin]).normalized();
  QVERIFY(std::abs(n.dot(axis)) < 1e-6);
  QVERIFY(std::abs(n.y()) > 0.99);
}

//! Ethene (C2H4) and acetone (C=O) both have sp2 atoms carrying TWO non-partner
//! substituents, so their raw cross terms cancel (oracle finding). Assert the
//! helper returns a plane normal in the molecular plane, not the zero sentinel
//! or a noise-dominated in-plane direction.
void BondSpreadTest::etheneAndKetoneDoNotCancel()
{
  // Ethene, planar in the world xz plane (molecular plane normal = +/-Y), the
  // C=C axis along +Z. Each carbon carries two H's at +/-120 deg from the axis.
  const Vector3d c1(0.0, 0.0, 0.0);
  const Vector3d c2(0.0, 0.0, 1.34);
  const double rCH = 1.09;
  QVector<Vector3d> beginNbrs, endNbrs;
  beginNbrs << (c1 + rCH * Vector3d(std::sin(2.0944), 0.0, std::cos(2.0944))) // +120deg
            << (c1 + rCH * Vector3d(std::sin(-2.0944), 0.0, std::cos(-2.0944))); // -120deg
  endNbrs << (c2 + rCH * Vector3d(std::sin(0.5236), 0.0, -std::cos(0.5236))) // ~+60deg
          << (c2 + rCH * Vector3d(std::sin(-0.5236), 0.0, -std::cos(-0.5236))); // ~-60deg

  const Vector3d n = bondSpreadNormal(c1, c2, beginNbrs, endNbrs);
  QVERIFY(n.squaredNorm() > 1e-12);
  const Vector3d axis = (c2 - c1).normalized();
  QVERIFY(std::abs(n.dot(axis)) < 1e-6);
  QVERIFY(std::abs(n.y()) > 0.99); // along the molecular plane normal

  // Distorted ethene: displace one H out of the plane by 0.02 A. The helper
  // must still return a plane normal, not a noise-dominated in-plane vector.
  QVector<Vector3d> beginNbrsD = beginNbrs;
  beginNbrsD[0].y() += 0.02;
  const Vector3d nd = bondSpreadNormal(c1, c2, beginNbrsD, endNbrs);
  QVERIFY(nd.squaredNorm() > 1e-12);
  QVERIFY(std::abs(nd.dot(axis)) < 1e-6);
  QVERIFY(std::abs(nd.y()) > 0.9); // still essentially the plane normal

  // Acetone-like ketone C=O with the carbonyl carbon carrying two methyl
  // substituents (both atoms substituted).
  const Vector3d kc(0.0, 0.0, 0.0);
  const Vector3d ko(0.0, 0.0, 1.21);
  QVector<Vector3d> kcNbrs;
  kcNbrs << Vector3d(1.5, 0.0, -0.6) << Vector3d(-1.5, 0.0, -0.6);
  const Vector3d kn = bondSpreadNormal(kc, ko, kcNbrs, QVector<Vector3d>());
  QVERIFY(kn.squaredNorm() > 1e-12);
  QVERIFY(std::abs(kn.dot((ko - kc).normalized())) < 1e-6);
  QVERIFY(std::abs(kn.y()) > 0.9); // in the xz molecular plane
}

QTEST_MAIN(BondSpreadTest)
#include "bondspreadtest.moc"
