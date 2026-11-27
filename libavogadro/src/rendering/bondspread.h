/**********************************************************************
  bondspread.h - shared multi-bond spread-direction computation.

  The direction in which a multiple bond's parallel cylinders (or lines) are
  offset is a chemistry question: the pair should lie in the bond's local sp2
  plane, perpendicular to the bond axis, rather than in a direction dictated by
  the world coordinate frame (modern tier) or by the molecule's global
  best-fit plane (legacy tier).

  This header provides ONE pure computation of that direction from the bond's
  own substituent geometry, so the rendering tiers agree by construction. It is
  header-only (inline) so both the rendering path and the legacy engines can
  include it without a new link dependency.

  The function returns the bond's local sp2 PLANE NORMAL — the unit vector
  perpendicular to the bond's local plane (i.e. perpendicular to the bond axis,
  since the axis lies in that plane). The actual spread direction used to offset
  the parallel cylinders/lines is the in-plane vector `axis x planeNormal`, which
  the callers compute (the modern emitter in SceneBuilder::appendBond, the legacy
  Cylinder::drawMulti) so both tiers agree by construction.

  It returns the zero vector (the "empty" sentinel) when the geometry is
  degenerate — no substituents at all, or a cross-product sum that cancels to
  zero (collinear substituents) — so the caller can apply its own tier-specific
  fallback (design D2/D3):
    - modern tier  -> axis.unitOrthogonal()  (today's behaviour)
    - legacy tier  -> the widget's molecule normal (today's behaviour)

  Determinism (design D4): the computation is pure floating-point math on the
  supplied positions; the same input yields the same output, and the sum over
  substituents is order-insensitive up to floating-point association.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 *********************************************************************/

#ifndef AVOGADRO_RENDERING_BONDSPREAD_H
#define AVOGADRO_RENDERING_BONDSPREAD_H

#include <QVector>

#include <Eigen/Core>

namespace Avogadro {
namespace rendering {

//! Compute the local sp2 plane normal for a multiple bond.
//!
//! For each substituent position \a p attached to the begin atom, accumulate
//! `normalize(p - bondBeginPos) x axis`; for each substituent \a q attached to
//! the end atom, accumulate `normalize(q - bondEndPos) x axis`. The cross
//! product with the axis keeps every term perpendicular to the bond axis by
//! construction, so the normalized sum is the local plane normal — regardless
//! of the molecule's orientation in the world frame and independent of the
//! whole-molecule best-fit plane. The in-plane spread direction the callers
//! actually offset by is `axis x planeNormal`.
//!
//! Each term is sign-aligned to the running sum before it is added: at an
//! idealized sp2 centre the two substituents sit on opposite sides of the
//! axis (±120°), so their raw cross terms are anti-parallel and would cancel
//! exactly (e.g. a benzene carbon that also carries a hydrogen). Aligning each
//! term to the accumulated sum keeps every substituent reinforcing the same
//! plane normal instead of cancelling, so the result is the ring plane rather
//! than the zero sentinel. The alignment is deterministic: the orientation is
//! fixed by the first non-degenerate term (neighbor order is stable for a
//! given molecule state).
//!
//! The caller supplies the neighbor positions already split by which bonded
//! atom they attach to, and excludes the bonded partner.
//!
//! @return a unit vector perpendicular to the bond axis (the local plane
//!         normal), or the zero vector when the geometry is degenerate (no
//!         substituents, or a cross-product sum that cancels to zero) so the
//!         caller can apply its tier-specific fallback. Never crashes on
//!         degenerate input.
inline Eigen::Vector3d bondSpreadNormal(const Eigen::Vector3d &bondBeginPos,
                                        const Eigen::Vector3d &bondEndPos,
                                        const QVector<Eigen::Vector3d> &beginNeighbors,
                                        const QVector<Eigen::Vector3d> &endNeighbors)
{
  const Eigen::Vector3d axis = bondEndPos - bondBeginPos;
  const double axisLen = axis.norm();
  if (axisLen < 1e-9)
    return Eigen::Vector3d(0.0, 0.0, 0.0); // degenerate bond axis

  const Eigen::Vector3d axisDir = axis / axisLen;
  Eigen::Vector3d sum(0.0, 0.0, 0.0);

  const auto accumulate = [&axisDir, &sum](const Eigen::Vector3d &neighborPos,
                                           const Eigen::Vector3d &atomPos) {
    const Eigen::Vector3d v = neighborPos - atomPos;
    const double vLen = v.norm();
    if (vLen < 1e-9)
      return; // neighbor coincides with the atom; no direction to contribute
    Eigen::Vector3d term = (v / vLen).cross(axisDir);
    // Sign-align to the running sum (see the doc comment): flip a term that
    // opposes the accumulated plane-normal direction so symmetric sp2
    // substituents reinforce rather than cancel.
    if (sum.squaredNorm() > 1e-24 && term.dot(sum) < 0.0)
      term = -term;
    sum += term;
  };

  for (const Eigen::Vector3d &p : beginNeighbors)
    accumulate(p, bondBeginPos);
  for (const Eigen::Vector3d &q : endNeighbors)
    accumulate(q, bondEndPos);

  const double sumLen = sum.norm();
  if (sumLen < 1e-9)
    return Eigen::Vector3d(0.0, 0.0, 0.0); // no substituents or collinear/degenerate
  return sum / sumLen;
}

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_BONDSPREAD_H
