/**********************************************************************
  Placement helper for the DrawTool click-to-extend gesture.

  Implements design D2 (pure placement math), D3 (hybridization heuristic
  over Avogadro's own bond list) and D4 (covalent-radii bond length). No GL,
  no OB molecule graph round-trip: the only OpenBabel dependency is the
  stateless OBElements::GetCovalentRad() radius lookup.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 **********************************************************************/

#include "placement.h"

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/molecule.h>

#include <Eigen/Geometry>

#include <openbabel/elements.h>

#include <cmath>

namespace Avogadro {

  namespace {

    // Guarded-normalize threshold.
    const double kEps = 1e-6;
    // √2/2, used by the degree-2 Sp3 anti-bisector formula.
    const double kSqrtHalf = std::sqrt(0.5);
    // π as a local constant: M_PI is not guaranteed on MSVC unless
    // _USE_MATH_DEFINES is set before <math.h> is first pulled in (Eigen/Core
    // may do so before global.h is reached), so avoid the dependency.
    const double kPi = 3.14159265358979323846;

    //! Bond angle (radians) corresponding to a hybridization.
    double hybAngle(SketchHyb hyb)
    {
      switch (hyb) {
      case SketchHyb::Sp:
        return kPi; // 180°
      case SketchHyb::Sp2:
        return 2.0 * kPi / 3.0; // 120°
      case SketchHyb::Sp3:
      default:
        return std::acos(-1.0 / 3.0); // 109.471°
      }
    }

    //! Unit vector along `v`, or `fallback` when v is degenerate (< kEps).
    //! `fallback` is expected to be a unit vector.
    Eigen::Vector3d normalized(const Eigen::Vector3d &v,
                               const Eigen::Vector3d &fallback)
    {
      double n = v.norm();
      if (n < kEps)
        return fallback;
      return v / n;
    }

    //! A cardinal axis that is not parallel to `ref`.
    Eigen::Vector3d firstCardinalNonParallel(const Eigen::Vector3d &ref)
    {
      const Eigen::Vector3d u = normalized(ref, Eigen::Vector3d(1, 0, 0));
      const Eigen::Vector3d candidates[3] = {
        Eigen::Vector3d(1, 0, 0),
        Eigen::Vector3d(0, 1, 0),
        Eigen::Vector3d(0, 0, 1),
      };
      for (const Eigen::Vector3d &c : candidates) {
        if (std::abs(u.dot(c)) < 0.9)
          return c;
      }
      return Eigen::Vector3d(0, 1, 0);
    }

  } // namespace

  Eigen::Vector3d placeSubstituent(
    const Eigen::Vector3d &origin,
    const std::vector<Eigen::Vector3d> &heavyNeighbors,
    SketchHyb hyb,
    double bondLength,
    const Eigen::Vector3d &preferredDir)
  {
    const Eigen::Vector3d fallbackAxis(1, 0, 0);
    Eigen::Vector3d nHat;

    const size_t n = heavyNeighbors.size();

    if (n == 0) {
      // No reference geometry: use the preferred direction, else +X.
      nHat = normalized(preferredDir, fallbackAxis);
    } else if (n == 1) {
      // u = unit vector from the single neighbor to the origin (along the
      // existing bond). The new bond is placed at the hybridization angle
      // from that bond.
      Eigen::Vector3d u = normalized(origin - heavyNeighbors[0], fallbackAxis);
      // Deterministic azimuth around the cone: preferredDir if provided, else
      // the first cardinal axis not parallel to u. (The caller's neighbor
      // substituent reference is not part of this helper's contract.)
      Eigen::Vector3d refDir = preferredDir;
      if (refDir.norm() < kEps)
        refDir = firstCardinalNonParallel(u);
      Eigen::Vector3d v = u.cross(refDir);
      if (v.norm() < kEps)
        v = u.cross(firstCardinalNonParallel(u));
      Eigen::Vector3d vHat2 =
        normalized(u.cross(normalized(v, Eigen::Vector3d(0, 1, 0))),
                   Eigen::Vector3d(0, 1, 0));
      const double theta = hybAngle(hyb);
      // n̂ = normalize(u - v̂₂·tan(π - θ)); θ=180° -> tan(0)=0 -> n̂ = u.
      nHat = normalized(u - vHat2 * std::tan(kPi - theta), u);
    } else if (n == 2) {
      const Eigen::Vector3d b1 = normalized(heavyNeighbors[0] - origin, fallbackAxis);
      const Eigen::Vector3d b2 = normalized(heavyNeighbors[1] - origin, fallbackAxis);
      if (hyb == SketchHyb::Sp3) {
        // bis = normalize(b₁+b₂), v̂ = normalize(cross(b₁,b₂)); the design
        // formula n̂ = normalize(v̂ - bis·(√2/2)) gives ~109.47° to both
        // existing bonds when the existing angle is tetrahedral. (The design
        // doc writes "+" but the "+" sign yields 70.5°; "−" is the correct
        // tetrahedral 4th-vertex side.)
        const Eigen::Vector3d bis = normalized(b1 + b2, fallbackAxis);
        const Eigen::Vector3d vHat = normalized(b1.cross(b2), Eigen::Vector3d(0, 0, 1));
        nHat = normalized(vHat - bis * kSqrtHalf, fallbackAxis);
      } else {
        // Sp2 / Sp: place on the far side of the two existing bonds so the new
        // bond is ~120° (Sp2) to both, instead of squeezing between them.
        const Eigen::Vector3d anti = -(b1 + b2);
        if (anti.norm() >= kEps) {
          nHat = anti / anti.norm();
        } else {
          const Eigen::Vector3d cr = b1.cross(b2);
          if (cr.norm() >= kEps)
            nHat = cr / cr.norm();
          else
            nHat = normalized(preferredDir, fallbackAxis);
        }
      }
    } else {
      // n >= 3 heavy neighbors: the remaining tetrahedral vertex.
      Eigen::Vector3d sum = Eigen::Vector3d::Zero();
      for (const Eigen::Vector3d &p : heavyNeighbors)
        sum += normalized(p - origin, fallbackAxis);
      Eigen::Vector3d anti = -sum;
      if (anti.norm() >= kEps)
        nHat = anti / anti.norm();
      else
        nHat = normalized(preferredDir, fallbackAxis);
    }

    return origin + nHat * bondLength;
  }

  SketchHyb sketchHybridization(Atom *atom, Molecule *molecule)
  {
    if (!atom || !molecule)
      return SketchHyb::Sp3;

    const unsigned long atomId = atom->id();
    int order2Count = 0;
    bool anyOrderGe3 = false;

    const QList<Bond *> &bonds = molecule->bonds();
    for (Bond *b : bonds) {
      if (!b)
        continue;
      if (b->beginAtomId() != atomId && b->endAtomId() != atomId)
        continue;
      const short order = b->order();
      if (order >= 3)
        anyOrderGe3 = true;
      else if (order == 2)
        ++order2Count;
    }

    if (anyOrderGe3)
      return SketchHyb::Sp;
    if (order2Count >= 2)
      return SketchHyb::Sp;
    if (order2Count == 1)
      return SketchHyb::Sp2;
    return SketchHyb::Sp3;
  }

  double covalentBondLength(int elementA, int elementB)
  {
    const double ra = OpenBabel::OBElements::GetCovalentRad(elementA);
    const double rb = OpenBabel::OBElements::GetCovalentRad(elementB);
    if (ra <= 0.0 || rb <= 0.0)
      return 1.5;
    return ra + rb;
  }

} // namespace Avogadro
