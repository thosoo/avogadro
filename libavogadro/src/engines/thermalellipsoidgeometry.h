/**********************************************************************
  ThermalEllipsoidGeometry - helpers for ADP / thermal ellipsoid display
 **********************************************************************/

#ifndef THERMALELLIPSOIDGEOMETRY_H
#define THERMALELLIPSOIDGEOMETRY_H

#include <Eigen/Core>

namespace Avogadro {

class Atom;

namespace ThermalEllipsoidGeometry {

  enum Probability { Probability50 = 0, Probability90 = 1, Probability99 = 2 };

  double probabilityScale(Probability probability);
  bool readUcart(const Atom *atom, Eigen::Matrix3d &ucart);
  bool diagonalizeUcart(const Eigen::Matrix3d &ucart,
                        Eigen::Matrix3d &axes,
                        Eigen::Vector3d &eigenvalues);
  bool ellipsoidForAtom(const Atom *atom, Probability probability,
                        double userScale, Eigen::Matrix3d &axes,
                        Eigen::Vector3d &radii);

} // namespace ThermalEllipsoidGeometry
} // namespace Avogadro

#endif
