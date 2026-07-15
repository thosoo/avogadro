/**********************************************************************
  ThermalEllipsoidGeometryTest - tests for ADP ellipsoid helper math
 **********************************************************************/

#include <QtTest>

#include <QDir>
#include <QFileInfo>

#include <avogadro/atom.h>
#include <avogadro/molecule.h>
#include "../src/engines/thermalellipsoidgeometry.h"

#include <openbabel/atom.h>
#include <openbabel/generic.h>
#include <openbabel/mol.h>
#include <openbabel/obconversion.h>

#include <Eigen/Core>
#include <Eigen/LU>

#include <cmath>
#include <string>

using Avogadro::Atom;
using Avogadro::Molecule;
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
  void setOBAtomImportsFloatingPointPairData();
  void ellipsoidForAtomUsesImportedFloatingPointPairData();
  void cifImportProducesRenderableThermalEllipsoid();
  void openBabelCifEllipsoidFixturesProduceRenderableAtoms();
};

namespace {

void addStringPair(OpenBabel::OBAtom &obatom, const char *name, const char *value)
{
  OpenBabel::OBPairData *pair = new OpenBabel::OBPairData;
  pair->SetAttribute(name);
  pair->SetValue(value);
  obatom.SetData(pair);
}

void addFloatingPointPair(OpenBabel::OBAtom &obatom, const char *name, double value)
{
  OpenBabel::OBPairFloatingPoint *pair = new OpenBabel::OBPairFloatingPoint;
  pair->SetAttribute(name);
  pair->SetValue(value);
  obatom.SetData(pair);
}

void addValidCartesianAdp(OpenBabel::OBAtom &obatom)
{
  addStringPair(obatom, "adp_valid", "true");
  addStringPair(obatom, "adp_basis", "cif cartesian");
  addFloatingPointPair(obatom, "adp_Ucart_11", 0.01);
  addFloatingPointPair(obatom, "adp_Ucart_22", 0.04);
  addFloatingPointPair(obatom, "adp_Ucart_33", 0.09);
  addFloatingPointPair(obatom, "adp_Ucart_12", 0.0);
  addFloatingPointPair(obatom, "adp_Ucart_13", 0.0);
  addFloatingPointPair(obatom, "adp_Ucart_23", 0.0);
}

bool obPairAsDouble(OpenBabel::OBAtom *atom, const char *name, double &value)
{
  if (!atom)
    return false;

  OpenBabel::OBGenericData *data = atom->GetData(name);
  if (!data)
    return false;

  if (OpenBabel::OBPairFloatingPoint *fp =
        dynamic_cast<OpenBabel::OBPairFloatingPoint *>(data)) {
    value = fp->GetGenericValue();
    return true;
  }

  if (OpenBabel::OBPairData *pair = dynamic_cast<OpenBabel::OBPairData *>(data)) {
    bool ok = false;
    value = QString(pair->GetValue().c_str()).toDouble(&ok);
    return ok;
  }

  return false;
}


void verifyOpenBabelDataDir()
{
  const QByteArray babelDataDir = qgetenv("BABEL_DATADIR");
  QVERIFY2(!babelDataDir.isEmpty(),
           "BABEL_DATADIR is not set for the thermal ellipsoid test");

  const QString dataDir = QString::fromLocal8Bit(babelDataDir);
  const QString spaceGroupsPath =
    QDir(dataDir).filePath(QStringLiteral("space-groups.txt"));

  QVERIFY2(QFileInfo::exists(spaceGroupsPath),
           qPrintable(QStringLiteral(
             "Open Babel data file is missing: %1; BABEL_DATADIR=%2")
             .arg(spaceGroupsPath, dataDir)));
}

QString obPairAsString(OpenBabel::OBAtom *atom, const char *name)
{
  if (!atom)
    return QString();
  if (OpenBabel::OBPairData *pair =
        dynamic_cast<OpenBabel::OBPairData *>(atom->GetData(name)))
    return QString(pair->GetValue().c_str());
  return QString();
}

} // namespace

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

void ThermalEllipsoidGeometryTest::setOBAtomImportsFloatingPointPairData()
{
  OpenBabel::OBAtom obatom;
  obatom.SetAtomicNum(6);
  obatom.SetVector(1.0, 2.0, 3.0);
  addStringPair(obatom, "adp_valid", "true");
  addStringPair(obatom, "adp_basis", "cif cartesian");
  addFloatingPointPair(obatom, "adp_Ucart_11", 0.01);

  Molecule molecule;
  Atom *atom = molecule.addAtom();
  QVERIFY(atom->setOBAtom(&obatom));

  QCOMPARE(atom->property("adp_valid").toString(), QString("true"));
  QCOMPARE(atom->property("adp_basis").toString(), QString("cif cartesian"));
  QVERIFY(std::abs(atom->property("adp_Ucart_11").toDouble() - 0.01) < 1.0e-12);
}

void ThermalEllipsoidGeometryTest::ellipsoidForAtomUsesImportedFloatingPointPairData()
{
  OpenBabel::OBAtom obatom;
  obatom.SetAtomicNum(6);
  obatom.SetVector(1.0, 2.0, 3.0);
  addValidCartesianAdp(obatom);

  Molecule molecule;
  Atom *atom = molecule.addAtom();
  QVERIFY(atom->setOBAtom(&obatom));

  Eigen::Matrix3d axes;
  Eigen::Vector3d radii;
  QVERIFY(Avogadro::ThermalEllipsoidGeometry::ellipsoidForAtom(atom, Probability50, 1.0, axes, radii));

  const double tol = 1.0e-12;
  QVERIFY(std::abs(radii(0) - std::sqrt(0.09) * 1.538172) < tol);
  QVERIFY(std::abs(radii(1) - std::sqrt(0.04) * 1.538172) < tol);
  QVERIFY(std::abs(radii(2) - std::sqrt(0.01) * 1.538172) < tol);

  const Eigen::Matrix3d transform = axes * radii.asDiagonal();
  QVERIFY(transform.allFinite());
  QVERIFY(std::isfinite(transform.determinant()));
  QVERIFY(transform.determinant() > 0.0);
}

void ThermalEllipsoidGeometryTest::cifImportProducesRenderableThermalEllipsoid()
{
  verifyOpenBabelDataDir();

  static const char cif[] = R"cif(data_adp_real_cif_minimal
_cell_length_a 6.9827
_cell_length_b 11.8748
_cell_length_c 15.1296
_cell_angle_alpha 90
_cell_angle_beta 98.397
_cell_angle_gamma 90
_space_group_name_H-M_alt 'P 1'

loop_
_atom_site_label
_atom_site_type_symbol
_atom_site_fract_x
_atom_site_fract_y
_atom_site_fract_z
_atom_site_U_iso_or_equiv
_atom_site_adp_type
_atom_site_occupancy
C1 C 0.8169(3) 0.4433(2) 0.71499(15) 0.0194(4) Uani 1
C2 C 0.7692(3) 0.3488(2) 0.66269(15) 0.0179(4) Uani 1

loop_
_atom_site_aniso_label
_atom_site_aniso_U_11
_atom_site_aniso_U_22
_atom_site_aniso_U_33
_atom_site_aniso_U_23
_atom_site_aniso_U_13
_atom_site_aniso_U_12
C1 0.0231(11) 0.0235(11) 0.0118(10) 0.0011(9) 0.0036(8) 0.0017(9)
C2 0.0198(10) 0.0185(10) 0.0159(10) 0.0031(8) 0.0045(8) 0.0016(8)
)cif";

  OpenBabel::OBConversion conv;
  QVERIFY2(conv.SetInFormat("cif"), "Open Babel CIF input format is unavailable");

  OpenBabel::OBMol obmol;
  QVERIFY2(conv.ReadString(&obmol, cif), "Open Babel failed to read the CIF");
  QVERIFY2(obmol.NumAtoms() >= 1, "Open Babel failed to read atoms from the CIF");

  OpenBabel::OBAtom *obatom = obmol.GetAtom(1);
  QVERIFY(obatom != nullptr);

  const char *cartFields[6] = {
    "adp_Ucart_11", "adp_Ucart_22", "adp_Ucart_33",
    "adp_Ucart_12", "adp_Ucart_13", "adp_Ucart_23"
  };

  double openBabelMaxCart = 0.0;
  for (int i = 0; i < 6; ++i) {
    OpenBabel::OBGenericData *data = obatom->GetData(cartFields[i]);
    QVERIFY2(data != nullptr, "Open Babel did not produce adp_Ucart_*");

    bool valueOk = false;
    double value = 0.0;
    if (OpenBabel::OBPairFloatingPoint *fp =
          dynamic_cast<OpenBabel::OBPairFloatingPoint *>(data)) {
      value = fp->GetGenericValue();
      valueOk = true;
    }
    else if (OpenBabel::OBPairData *pair =
               dynamic_cast<OpenBabel::OBPairData *>(data)) {
      value = QString(pair->GetValue().c_str()).toDouble(&valueOk);
    }

    QVERIFY2(valueOk, "Open Babel produced non-numeric adp_Ucart_*");
    QVERIFY(std::isfinite(value));
    openBabelMaxCart = std::max(openBabelMaxCart, std::abs(value));
  }
  QVERIFY2(openBabelMaxCart > 0.0, "Open Babel produced only zero adp_Ucart_*");

  Molecule molecule;
  Atom *atom = molecule.addAtom();
  QVERIFY(atom != nullptr);
  QVERIFY(atom->setOBAtom(obatom));

  QCOMPARE(atom->property("adp_valid").toString(), QString("true"));
  QCOMPARE(atom->property("adp_basis").toString(), QString("cif cartesian"));
  QCOMPARE(atom->property("adp_input_type").toString(), QString("U"));
  QCOMPARE(atom->property("adp_type").toString(), QString("Uani"));

  bool ok = false;

  QVERIFY(std::abs(atom->property("adp_U_11").toDouble(&ok) - 0.0231) < 1.0e-12);
  QVERIFY(ok);
  QVERIFY(std::abs(atom->property("adp_U_22").toDouble(&ok) - 0.0235) < 1.0e-12);
  QVERIFY(ok);
  QVERIFY(std::abs(atom->property("adp_U_33").toDouble(&ok) - 0.0118) < 1.0e-12);
  QVERIFY(ok);
  QVERIFY(std::abs(atom->property("adp_U_23").toDouble(&ok) - 0.0011) < 1.0e-12);
  QVERIFY(ok);
  QVERIFY(std::abs(atom->property("adp_U_13").toDouble(&ok) - 0.0036) < 1.0e-12);
  QVERIFY(ok);
  QVERIFY(std::abs(atom->property("adp_U_12").toDouble(&ok) - 0.0017) < 1.0e-12);
  QVERIFY(ok);

  double maxCart = 0.0;
  for (int i = 0; i < 6; ++i) {
    bool cartOk = false;
    const double value = atom->property(cartFields[i]).toDouble(&cartOk);
    QVERIFY2(cartOk, "Atom::setOBAtom() did not import adp_Ucart_*");
    QVERIFY2(std::isfinite(value), "Atom::setOBAtom() imported non-finite adp_Ucart_*");
    maxCart = std::max(maxCart, std::abs(value));
  }
  QVERIFY2(maxCart > 0.0, "Atom::setOBAtom() imported only zero adp_Ucart_*");

  Eigen::Matrix3d axes;
  Eigen::Vector3d radii;

  QVERIFY2(Avogadro::ThermalEllipsoidGeometry::ellipsoidForAtom(
             atom,
             Avogadro::ThermalEllipsoidGeometry::Probability50,
             1.0,
             axes,
             radii),
           "ThermalEllipsoidGeometry::ellipsoidForAtom() rejected the imported atom");

  QVERIFY(radii.allFinite());
  QVERIFY(radii.maxCoeff() > 0.0);
}

void ThermalEllipsoidGeometryTest::openBabelCifEllipsoidFixturesProduceRenderableAtoms()
{
  struct FixtureExpectation
  {
    const char *filename;
    unsigned int atoms;
    unsigned int atomsWithAdps;
  };

  const FixtureExpectation fixtures[] = {
    { "cif-ellipsoids/1548467.cif", 19, 19 },
    { "cif-ellipsoids/4335632.cif", 5, 3 },
    { "cif-ellipsoids/4124388.cif", 11, 5 }
  };

  verifyOpenBabelDataDir();

  OpenBabel::OBConversion conv;
  QVERIFY2(conv.SetInFormat("cif"), "Open Babel CIF input format is unavailable");

  const char *rawFields[6] = {
    "adp_U_11", "adp_U_22", "adp_U_33",
    "adp_U_12", "adp_U_13", "adp_U_23"
  };

  const char *cartFields[6] = {
    "adp_Ucart_11", "adp_Ucart_22", "adp_Ucart_33",
    "adp_Ucart_12", "adp_Ucart_13", "adp_Ucart_23"
  };

  for (const FixtureExpectation &fixture : fixtures) {
    OpenBabel::OBMol obmol;
    const std::string filename = std::string(TESTDATADIR) + fixture.filename;
    const QByteArray readError = QString("Open Babel failed to read fixture %1")
                                 .arg(fixture.filename).toLocal8Bit();
    QVERIFY2(conv.ReadFile(&obmol, filename), readError.constData());
    QCOMPARE(obmol.NumAtoms(), fixture.atoms);

    Molecule molecule;
    unsigned int atomsWithAdps = 0;
    unsigned int renderableAtoms = 0;
    double maxRenderableRadius = 0.0;

    for (unsigned int atomIndex = 1; atomIndex <= obmol.NumAtoms(); ++atomIndex) {
      OpenBabel::OBAtom *obatom = obmol.GetAtom(atomIndex);
      QVERIFY(obatom != nullptr);

      if (!obatom->GetData("adp_U_11"))
        continue;

      QCOMPARE(obPairAsString(obatom, "adp_valid"), QString("true"));
      QCOMPARE(obPairAsString(obatom, "adp_basis"), QString("cif cartesian"));
      QCOMPARE(obPairAsString(obatom, "adp_input_type"), QString("U"));
      const QString source = obPairAsString(obatom, "adp_source");
      QVERIFY(source == QString("mmcif_atom_site_aniso") ||
              source == QString("cif_atom_site_aniso"));

      double maxRaw = 0.0;
      double maxCart = 0.0;
      for (int field = 0; field < 6; ++field) {
        double raw = 0.0;
        QVERIFY2(obPairAsDouble(obatom, rawFields[field], raw), "Open Babel did not produce numeric raw ADP data");
        QVERIFY(std::isfinite(raw));
        maxRaw = std::max(maxRaw, std::abs(raw));

        double cart = 0.0;
        QVERIFY2(obPairAsDouble(obatom, cartFields[field], cart), "Open Babel did not produce numeric Cartesian ADP data");
        QVERIFY(std::isfinite(cart));
        maxCart = std::max(maxCart, std::abs(cart));
      }
      QVERIFY(maxRaw > 0.0);
      QVERIFY(maxCart > 0.0);

      Atom *atom = molecule.addAtom();
      QVERIFY(atom != nullptr);
      QVERIFY2(atom->setOBAtom(obatom), "Atom::setOBAtom() failed for CIF fixture atom");

      for (int field = 0; field < 6; ++field) {
        bool ok = false;
        const double cart = atom->property(cartFields[field]).toDouble(&ok);
        QVERIFY2(ok, "Atom::setOBAtom() did not import numeric adp_Ucart_*");
        QVERIFY(std::isfinite(cart));
      }

      Eigen::Matrix3d axes;
      Eigen::Vector3d radii;
      QVERIFY2(Avogadro::ThermalEllipsoidGeometry::ellipsoidForAtom(
                 atom,
                 Avogadro::ThermalEllipsoidGeometry::Probability50,
                 1.0,
                 axes,
                 radii),
               "ThermalEllipsoidGeometry::ellipsoidForAtom() rejected a CIF fixture atom");
      QVERIFY(radii.allFinite());
      QVERIFY(radii.maxCoeff() > 0.0);
      const Eigen::Matrix3d transform = axes * radii.asDiagonal();
      QVERIFY(transform.allFinite());
      QVERIFY(std::isfinite(transform.determinant()));
      QVERIFY(transform.determinant() > 0.0);

      ++atomsWithAdps;
      ++renderableAtoms;
      maxRenderableRadius = std::max(maxRenderableRadius, radii.maxCoeff());
    }

    QCOMPARE(atomsWithAdps, fixture.atomsWithAdps);
    QCOMPARE(renderableAtoms, fixture.atomsWithAdps);
    QVERIFY(maxRenderableRadius > 0.0);
  }
}

QTEST_MAIN(ThermalEllipsoidGeometryTest)
#include "thermalellipsoidgeometrytest.moc"
