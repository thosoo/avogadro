/**********************************************************************
  ThermalEllipsoidGeometry - helpers for ADP / thermal ellipsoid display
 **********************************************************************/

#include "thermalellipsoidgeometry.h"

#include <avogadro/atom.h>

#include <Eigen/Eigenvalues>

#include <QString>
#include <QtCore/QVariant>

#include <cmath>

namespace Avogadro {
namespace ThermalEllipsoidGeometry {

  namespace {
    const double TinyNegativeTolerance = -1.0e-8;

    bool readDoubleProperty(const Atom *atom, const char *name, double &value)
    {
      bool ok = false;
      value = atom->property(name).toDouble(&ok);
      return ok && std::isfinite(value);
    }
  }

  double probabilityScale(Probability probability)
  {
    switch (probability) {
      case Probability90: return 2.500278;
      case Probability99: return 3.368214;
      case Probability50:
      default: return 1.538172;
    }
  }

  bool readUcart(const Atom *atom, Eigen::Matrix3d &ucart)
  {
    if (!atom || atom->property("adp_valid").toString().toLower() != QLatin1String("true"))
      return false;

    const QString basis = atom->property("adp_basis").toString();
    if (!basis.contains(QLatin1String("cartesian"), Qt::CaseInsensitive))
      return false;

    double u11, u22, u33, u12, u13, u23;
    if (!readDoubleProperty(atom, "adp_Ucart_11", u11) ||
        !readDoubleProperty(atom, "adp_Ucart_22", u22) ||
        !readDoubleProperty(atom, "adp_Ucart_33", u33) ||
        !readDoubleProperty(atom, "adp_Ucart_12", u12) ||
        !readDoubleProperty(atom, "adp_Ucart_13", u13) ||
        !readDoubleProperty(atom, "adp_Ucart_23", u23))
      return false;

    ucart << u11, u12, u13,
             u12, u22, u23,
             u13, u23, u33;
    return true;
  }

  bool diagonalizeUcart(const Eigen::Matrix3d &ucart,
                        Eigen::Matrix3d &axes,
                        Eigen::Vector3d &eigenvalues)
  {
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(ucart);
    if (solver.info() != Eigen::Success)
      return false;

    for (int i = 0; i < 3; ++i) {
      double value = solver.eigenvalues()(i);
      if (!std::isfinite(value) || value < TinyNegativeTolerance)
        return false;
    }

    for (int i = 0; i < 3; ++i) {
      const int source = 2 - i; // Eigen returns ascending; render descending.
      eigenvalues(i) = solver.eigenvalues()(source);
      if (eigenvalues(i) < 0.0)
        eigenvalues(i) = 0.0;
      axes.col(i) = solver.eigenvectors().col(source);
    }

    return true;
  }

  bool ellipsoidForAtom(const Atom *atom, Probability probability,
                        double userScale, Eigen::Matrix3d &axes,
                        Eigen::Vector3d &radii)
  {
    Eigen::Matrix3d ucart;
    Eigen::Vector3d eigenvalues;
    if (!readUcart(atom, ucart) || !diagonalizeUcart(ucart, axes, eigenvalues))
      return false;

    const double scale = probabilityScale(probability) * userScale;
    for (int i = 0; i < 3; ++i)
      radii(i) = std::sqrt(eigenvalues(i)) * scale;

    return radii.allFinite();
  }

} // namespace ThermalEllipsoidGeometry
} // namespace Avogadro
