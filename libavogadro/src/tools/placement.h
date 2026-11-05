/**********************************************************************
  Placement helper for the DrawTool click-to-extend gesture.

  Pure geometry / hybridization classification, with no GL or OB molecule
  graph dependency beyond the stateless covalent-radius lookup. Compiled
  directly into the drawtool plugin (drawtool_SRCS) and into the headless
  unit test (libavogadro/tests/placementtest.cpp), so nothing here needs
  A_EXPORT.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 **********************************************************************/

#ifndef PLACEMENT_H
#define PLACEMENT_H

#include <Eigen/Core>

#include <vector>

namespace Avogadro {

  class Atom;
  class Molecule;

  //! Hybridization classification used to pick a substituent bond angle.
  enum class SketchHyb {
    Sp,   //!< linear, 180°
    Sp2,  //!< trigonal planar, 120°
    Sp3   //!< tetrahedral, 109.471°
  };

  /*!
   * Returns the world position for a new substituent bonded at `origin`.
   *
   * The direction is chosen so the new bond makes the angle corresponding to
   * `hyb` (Sp -> 180°, Sp2 -> 120°, Sp3 -> 109.471°) with the existing
   * heavy-atom bonds, and the magnitude is exactly `bondLength`.
   *
   * `heavyNeighbors` holds the positions of the clicked atom's NON-HYDROGEN
   * neighbors (the caller filters explicit hydrogens out; this helper never
   * sees them). Case table (dᵢ = normalize(pᵢ - origin), bᵢ = normalize(dᵢ)):
   *
   *   0 heavy nbrs:  n̂ = preferredDir, else (1,0,0)
   *   1 heavy nbr:   u = normalize(origin - p₁); pick a deterministic
   *                  azimuth reference (preferredDir if non-zero, else the
   *                  first cardinal axis not parallel to u);
   *                  n̂ = normalize(u - v̂₂·tan(π - θ)); θ=180° -> n̂ = u
   *   2 heavy nbrs:  Sp3: bis=normalize(b₁+b₂), v̂=normalize(cross(b₁,b₂)),
   *                  n̂ = normalize(v̂ - bis·(√2/2))  (~109.47° to both when
   *                  the existing angle is tetrahedral);
   *                  Sp2/Sp: n̂ = -normalize(b₁+b₂) (anti-bisector, ~120° to
   *                  both for Sp2), degenerate -> cross(b₁,b₂), then +X
   *   3+ heavy nbrs: n̂ = -normalize(Σ bᵢ) (remaining tetrahedral vertex);
   *                  norm < 1e-6 -> preferredDir, else +X
   *
   * Every normalize is guarded (norm < 1e-6 -> the documented fallback), so
   * the returned position is finite and deterministic for any input.
   */
  Eigen::Vector3d placeSubstituent(
    const Eigen::Vector3d &origin,
    const std::vector<Eigen::Vector3d> &heavyNeighbors,
    SketchHyb hyb,
    double bondLength,
    const Eigen::Vector3d &preferredDir = Eigen::Vector3d::Zero());

  /*!
   * Classifies an atom's hybridization from the bond orders recorded in
   * `molecule` (design D3):
   *   any bond order >= 3         -> Sp   (incl. metal-metal quadruple)
   *   two or more order-2 bonds   -> Sp
   *   exactly one order-2 bond    -> Sp2
   *   otherwise                   -> Sp3
   * Bonds to explicit hydrogens are single bonds and do not change the
   * classification, so they are naturally ignored.
   */
  SketchHyb sketchHybridization(Atom *atom, Molecule *molecule);

  /*!
   * Covalent bond length as the sum of the two covalent radii
   * (OpenBabel::OBElements::GetCovalentRad). Falls back to 1.5 Å when either
   * radius is 0 (unknown element).
   */
  double covalentBondLength(int elementA, int elementB);

} // namespace Avogadro

#endif
