/**********************************************************************
  ThermalEllipsoidGeometryTest - tests for ADP ellipsoid helper math
 **********************************************************************/

#include <QtTest>

#include <avogadro/atom.h>
#include "../src/engines/thermalellipsoidgeometry.h"

#include <Eigen/Core>

#include <cmath>

using Avogadro::Atom;
using Avogadro::ThermalEllipsoidGeometry::Probability50;

class ThermalEllipsoidGeometryTest : public QObject
{
  Q_OBJECT

private:
  void setValidCartesianAdp(Atom &atom)
  {
    atom.setProperty("adp_valid", "true");
    atom.setProperty("adp_basis", "cif cartesian");
    atom.setProperty("adp_Ucart_11", "0.01");
    atom.setProperty("adp_Ucart_22", "0.04");
    atom.setProperty("adp_Ucart_33", "0.09");
    atom.setProperty("adp_Ucart_12", "0.0");
    atom.setProperty("adp_Ucart_13", "0.0");
    atom.setProperty("adp_Ucart_23", "0.0");
  }

private slots:
  void diagonalTensorRadii();
  void missingTensorComponentReturnsFalse();
  void invalidAdpReturnsFalse();
  void missingBasisReturnsFalse();
  void nonCartesianBasisReturnsFalse();
  void tinyNegativeEigenvalueIsClamped();
  void significantNegativeEigenvalueIsRejected();
};

void ThermalEllipsoidGeometryTest::diagonalTensorRadii()
{
  Atom atom;
  setValidCartesianAdp(atom);

  Eigen::Matrix3d axes;
  Eigen::Vector3d radii;
  QVERIFY(Avogadro::ThermalEllipsoidGeometry::ellipsoidForAtom(&atom, Probability50, 1.0, axes, radii));

  const double tol = 1.0e-12;
  QVERIFY(std::abs(radii(0) - std::sqrt(0.09) * 1.538172) < tol);
  QVERIFY(std::abs(radii(1) - std::sqrt(0.04) * 1.538172) < tol);
  QVERIFY(std::abs(radii(2) - std::sqrt(0.01) * 1.538172) < tol);
}

void ThermalEllipsoidGeometryTest::missingTensorComponentReturnsFalse()
{
  Atom atom;
  atom.setProperty("adp_valid", "true");
  atom.setProperty("adp_basis", "cif cartesian");
  atom.setProperty("adp_Ucart_11", "0.01");
  atom.setProperty("adp_Ucart_22", "0.04");
  atom.setProperty("adp_Ucart_33", "0.09");
  atom.setProperty("adp_Ucart_12", "0.0");
  atom.setProperty("adp_Ucart_13", "0.0");
  // Deliberately omit adp_Ucart_23.

  Eigen::Matrix3d axes;
  Eigen::Vector3d radii;
  QVERIFY(!Avogadro::ThermalEllipsoidGeometry::ellipsoidForAtom(&atom, Probability50, 1.0, axes, radii));
}

void ThermalEllipsoidGeometryTest::invalidAdpReturnsFalse()
{
  Atom atom;
  setValidCartesianAdp(atom);
  atom.setProperty("adp_valid", "false");

  Eigen::Matrix3d axes;
  Eigen::Vector3d radii;
  QVERIFY(!Avogadro::ThermalEllipsoidGeometry::ellipsoidForAtom(&atom, Probability50, 1.0, axes, radii));
}

void ThermalEllipsoidGeometryTest::missingBasisReturnsFalse()
{
  Atom atom;
  atom.setProperty("adp_valid", "true");
  atom.setProperty("adp_Ucart_11", "0.01");
  atom.setProperty("adp_Ucart_22", "0.04");
  atom.setProperty("adp_Ucart_33", "0.09");
  atom.setProperty("adp_Ucart_12", "0.0");
  atom.setProperty("adp_Ucart_13", "0.0");
  atom.setProperty("adp_Ucart_23", "0.0");

  Eigen::Matrix3d axes;
  Eigen::Vector3d radii;
  QVERIFY(!Avogadro::ThermalEllipsoidGeometry::ellipsoidForAtom(&atom, Probability50, 1.0, axes, radii));
}

void ThermalEllipsoidGeometryTest::nonCartesianBasisReturnsFalse()
{
  Atom atom;
  setValidCartesianAdp(atom);
  atom.setProperty("adp_basis", "not cartesian");

  Eigen::Matrix3d axes;
  Eigen::Vector3d radii;
  QVERIFY(!Avogadro::ThermalEllipsoidGeometry::ellipsoidForAtom(&atom, Probability50, 1.0, axes, radii));
}

void ThermalEllipsoidGeometryTest::tinyNegativeEigenvalueIsClamped()
{
  Eigen::Matrix3d ucart = Eigen::Matrix3d::Zero();
  ucart(0, 0) = -1.0e-10;
  ucart(1, 1) = 0.04;
  ucart(2, 2) = 0.09;

  Eigen::Matrix3d axes;
  Eigen::Vector3d eigenvalues;
  QVERIFY(Avogadro::ThermalEllipsoidGeometry::diagonalizeUcart(ucart, axes, eigenvalues));
  QVERIFY(std::abs(eigenvalues(2)) < 1.0e-12);
}

void ThermalEllipsoidGeometryTest::significantNegativeEigenvalueIsRejected()
{
  Eigen::Matrix3d ucart = Eigen::Matrix3d::Zero();
  ucart(0, 0) = -1.0e-4;
  ucart(1, 1) = 0.04;
  ucart(2, 2) = 0.09;

  Eigen::Matrix3d axes;
  Eigen::Vector3d eigenvalues;
  QVERIFY(!Avogadro::ThermalEllipsoidGeometry::diagonalizeUcart(ucart, axes, eigenvalues));
}

QTEST_MAIN(ThermalEllipsoidGeometryTest)
#include "thermalellipsoidgeometrytest.moc"
