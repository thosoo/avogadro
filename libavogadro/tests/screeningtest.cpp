// Regression / correctness test for OpenSpec task 5.1 / D7 distance screening
// (fix-surface-visualization-performance).
//
// The screen skips shells whose atom is beyond rMax2 = -ln(kScreenTolerance)/
// alphaMin. This pins that screening changes the computed grid only within a
// tight tolerance, that it is deterministic, and that the electron-density path
// behaves the same way.
//
// kScreenTolerance is a settable inline variable (see gaussianset.h): setting
// it to 0.0 makes -ln(0) -> +inf, so every rMax2 is infinite and no shell is
// ever skipped -- i.e. a fully unscreened reference. The screened run uses the
// default 1e-12.
//
// The screening cutoffs are computed once per basis instance inside
// initCalculation[ForOrca], so each run below loads a FRESH GaussianSet with
// the wanted tolerance in effect; rerunning on the same instance would reuse
// the old cutoffs and make the comparison vacuous.

#include <QtTest/QtTest>
#include <QCoreApplication>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/moleculefile.h>

#include "surfaces/openqube/gaussianset.h"
#include "surfaces/openqube/cube.h"
#include "surfaces/openqube/basissetloader.h"

#include <cmath>
#include <vector>

using namespace Avogadro;
using OpenQube::GaussianSet;
using OpenQube::BasisSetLoader;
// NB: OpenQube::Cube is used explicitly -- Avogadro::Cube also exists and a
// plain using-declaration for OpenQube::Cube is ambiguous.

class ScreeningTest : public QObject
{
  Q_OBJECT

private:
  QString m_dataDir;
  MoleculeFile *m_mf = nullptr;
  Molecule *m_mol = nullptr;

  // Compute a padded bounding box over the molecule atoms (Angstrom).
  static Eigen::Vector3d boxMin(Molecule *mol, double pad)
  {
    Eigen::Vector3d mn = Eigen::Vector3d::Constant(1e18);
    foreach (Atom *a, mol->atoms()) {
      mn = mn.cwiseMin(*a->pos());
    }
    return mn - Eigen::Vector3d::Constant(pad);
  }
  static Eigen::Vector3d boxMax(Molecule *mol, double pad)
  {
    Eigen::Vector3d mx = Eigen::Vector3d::Constant(-1e18);
    foreach (Atom *a, mol->atoms()) {
      mx = mx.cwiseMax(*a->pos());
    }
    return mx + Eigen::Vector3d::Constant(pad);
  }

  // Load a fresh basis set for the test molecule. Call AFTER setting
  // kScreenTolerance: the cutoffs are fixed at the first initCalculation.
  GaussianSet *makeBasis()
  {
    return dynamic_cast<GaussianSet *>(BasisSetLoader::LoadBasisSet(m_dataDir));
  }

  // Build a small grid cube over the molecule and fill it with the MO for the
  // given 1-based state (or the density when @a density) using @a basis.
  OpenQube::Cube *fillCube(GaussianSet *basis, unsigned int state, bool density,
                 QByteArray &err)
  {
    OpenQube::Cube *cube = new OpenQube::Cube;
    const double spacing = 0.5;
    const double pad = 2.5;
    if (!cube->setLimits(boxMin(m_mol, pad), boxMax(m_mol, pad), spacing)) {
      err = "setLimits failed";
      delete cube;
      return nullptr;
    }
    bool ok;
    if (density)
      ok = basis->calculateCubeDensity(cube);
    else
      ok = basis->calculateCubeMO(cube, state);
    if (!ok) {
      err = density ? "calculateCubeDensity failed" : "calculateCubeMO failed";
      delete cube;
      return nullptr;
    }
    // Let any queued completion (cube unlock) fire before the next step.
    QCoreApplication::processEvents();
    return cube;
  }

  static double maxAbsDiff(const std::vector<float> &a,
                           const std::vector<float> &b)
  {
    if (a.size() != b.size())
      return -1.0;
    double maxAbs = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
      double d = std::abs(a[i] - b[i]);
      if (d > maxAbs)
        maxAbs = d;
    }
    return maxAbs;
  }

private slots:
  void initTestCase();
  void cleanupTestCase();
  void testScreenedMatchesUnscreened();
  void testDeterministic();
  void testDensityScreened();
};

void ScreeningTest::initTestCase()
{
  m_dataDir = QString::fromUtf8(TESTDATA_DIR) + "/koffein_orca.out";
  m_mf = MoleculeFile::readFile(m_dataDir.toUtf8().constData());
  QVERIFY(m_mf != nullptr);
  m_mol = m_mf->molecule(0);
  QVERIFY(m_mol != nullptr);
}

void ScreeningTest::cleanupTestCase()
{
  delete m_mf; // owns m_mol
  m_mf = nullptr;
  m_mol = nullptr;
}

// (a) screened vs unscreened agree within tolerance.
void ScreeningTest::testScreenedMatchesUnscreened()
{
  QByteArray err;
  GaussianSet::kScreenTolerance = 1e-12;
  GaussianSet *basis1 = makeBasis();
  QVERIFY2(basis1 != nullptr, "basis set did not load");
  OpenQube::Cube *screened = fillCube(basis1, 1, false, err);
  delete basis1;
  QVERIFY2(screened != nullptr, err.constData());

  // Fully unscreened reference (infinite cutoff -> no shell ever skipped).
  GaussianSet::kScreenTolerance = 0.0;
  GaussianSet *basis2 = makeBasis();
  QVERIFY2(basis2 != nullptr, "reference basis set did not load");
  OpenQube::Cube *unscreened = fillCube(basis2, 1, false, err);
  delete basis2;
  GaussianSet::kScreenTolerance = 1e-12;
  QVERIFY2(unscreened != nullptr, err.constData());

  const std::vector<float> &a = *screened->data();
  const std::vector<float> &b = *unscreened->data();
  QCOMPARE(a.size(), b.size());

  // Each skipped shell contributes < kScreenTolerance (1e-12); the summed
  // difference must stay far below float-storage noise.
  double maxAbs = maxAbsDiff(a, b);
  QVERIFY2(maxAbs >= 0.0, "grid sizes differ");
  QVERIFY2(maxAbs < 1e-9,
           qPrintable(QString("max abs diff screened vs unscreened = %1")
                          .arg(maxAbs)));

  delete screened;
  delete unscreened;
}

// (b) screening is deterministic: two identical runs are bitwise equal.
void ScreeningTest::testDeterministic()
{
  QByteArray err;
  GaussianSet::kScreenTolerance = 1e-12;
  GaussianSet *basis1 = makeBasis();
  QVERIFY2(basis1 != nullptr, "basis set did not load");
  OpenQube::Cube *c1 = fillCube(basis1, 1, false, err);
  delete basis1;
  QVERIFY2(c1 != nullptr, err.constData());

  GaussianSet *basis2 = makeBasis();
  QVERIFY2(basis2 != nullptr, "second basis set did not load");
  OpenQube::Cube *c2 = fillCube(basis2, 1, false, err);
  delete basis2;
  QVERIFY2(c2 != nullptr, err.constData());

  const std::vector<float> &a = *c1->data();
  const std::vector<float> &b = *c2->data();
  QCOMPARE(a.size(), b.size());
  QVERIFY(maxAbsDiff(a, b) == 0.0);

  delete c1;
  delete c2;
}

// (c) the electron-density path agrees with/without screening within tolerance.
void ScreeningTest::testDensityScreened()
{
  QByteArray err;
  GaussianSet::kScreenTolerance = 1e-12;
  GaussianSet *basis1 = makeBasis();
  QVERIFY2(basis1 != nullptr, "basis set did not load");
  OpenQube::Cube *screened = fillCube(basis1, 0, true, err);
  delete basis1;
  QVERIFY2(screened != nullptr, err.constData());

  GaussianSet::kScreenTolerance = 0.0;
  GaussianSet *basis2 = makeBasis();
  QVERIFY2(basis2 != nullptr, "reference basis set did not load");
  OpenQube::Cube *unscreened = fillCube(basis2, 0, true, err);
  delete basis2;
  GaussianSet::kScreenTolerance = 1e-12;
  QVERIFY2(unscreened != nullptr, err.constData());

  const std::vector<float> &a = *screened->data();
  const std::vector<float> &b = *unscreened->data();
  double maxAbs = maxAbsDiff(a, b);
  QVERIFY2(maxAbs >= 0.0, "grid sizes differ");
  QVERIFY2(maxAbs < 1e-9,
           qPrintable(QString("density max abs diff = %1").arg(maxAbs)));

  delete screened;
  delete unscreened;
}

QTEST_MAIN(ScreeningTest)
#include "screeningtest.moc"
