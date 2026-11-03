// D5 slab-based cube evaluation regression (fix-orbital-visualization,
// OpenSpec task 5.4).
//
// Pins that partitioning the MO grid evaluation across worker slabs produces
// bitwise-identical grid values regardless of slab count (1, 4, auto), that
// repeated auto runs are bitwise identical, and that all values are finite
// (including a point far outside every screening cutoff and a point exactly on
// a nucleus).

#include <QtTest/QtTest>
#include <QCoreApplication>

#include "surfaces/openqube/gaussianset.h"
#include "surfaces/openqube/cube.h"

#include <cmath>
#include <vector>

using namespace OpenQube;
// NB: OpenQube::Cube is used explicitly -- Avogadro::Cube also exists and a
// plain using-declaration for OpenQube::Cube would be ambiguous.

class CubeEvalTest : public QObject
{
  Q_OBJECT

private:
  // Build a small synthetic GaussianSet: 2 atoms (BOHR coords, matching the
  // ORCA parser's m_coordFactor = 1 convention), one S shell on atom 0 and one
  // P shell on atom 1, one GTO each, one MO (state 1).
  static GaussianSet *makeBasis()
  {
    GaussianSet *gs = new GaussianSet;
    gs->addAtom(Eigen::Vector3d(0, 0, 0), 1);
    gs->addAtom(Eigen::Vector3d(1.0, 0, 0), 1);
    unsigned int b0 = gs->addBasis(0, S);
    unsigned int b1 = gs->addBasis(1, P);
    gs->addGTO(b0, 1.0, 1.0);
    gs->addGTO(b1, 1.0, 1.0);
    // MO 1: S coefficient then Px, Py, Pz.
    std::vector<double> mo;
    mo.push_back(1.0);
    mo.push_back(0.5);
    mo.push_back(0.2);
    mo.push_back(0.1);
    gs->addMOs(mo);
    return gs;
  }

  // Build a 10^3 grid cube over [-2, 2.5]^3 Angstrom and evaluate MO state 1
  // with the given slab count. Returns the cube (caller owns); nullptr on
  // failure with @a err set.
  static OpenQube::Cube *evalCube(int slabCount, QString &err)
  {
    GaussianSet::testSlabCount = slabCount;
    GaussianSet *gs = makeBasis();
    OpenQube::Cube *cube = new OpenQube::Cube;
    Eigen::Vector3d min(-2.0, -2.0, -2.0);
    Eigen::Vector3i dim(10, 10, 10);
    double spacing = 0.5;
    if (!cube->setLimits(min, dim, spacing)) {
      err = "setLimits failed";
      delete cube;
      delete gs;
      return nullptr;
    }
    bool ok = gs->calculateCubeMO(cube, 1);
    // Let the queued completion (cube unlock) fire before deleting the basis.
    QCoreApplication::processEvents();
    delete gs;
    if (!ok) {
      err = "calculateCubeMO failed";
      delete cube;
      return nullptr;
    }
    return cube;
  }

  static bool finiteAll(const std::vector<float> &v)
  {
    for (double x : v)
      if (!std::isfinite(x))
        return false;
    return true;
  }

  static bool bitwiseEqual(const std::vector<float> &a,
                           const std::vector<float> &b)
  {
    if (a.size() != b.size())
      return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (a[i] != b[i]) // bitwise; NaN != itself, but we expect no NaN
        return false;
    return true;
  }

  // The grid point at (x=4,y=4,z=4) sits exactly on atom 0 (the origin).
  static unsigned int nucleusIndex()
  {
    return 4 * 10 * 10 + 4 * 10 + 4;
  }
  // The corner (x=0,y=0,z=0) is far from both atoms (beyond every screening
  // cutoff), so it is screened to a ~0 value.
  static unsigned int farCornerIndex() { return 0; }

private slots:
  void testSlabPartitionBitwiseIdentical();
  void testAutoRunsBitwiseIdentical();
  void testFiniteness();
};

// slab partition 1 vs 4 must give bitwise-identical grid values.
void CubeEvalTest::testSlabPartitionBitwiseIdentical()
{
  QString err;
  OpenQube::Cube *c1 = evalCube(1, err);
  QVERIFY2(c1 != nullptr, err.toUtf8().constData());
  OpenQube::Cube *c4 = evalCube(4, err);
  QVERIFY2(c4 != nullptr, err.toUtf8().constData());

  const std::vector<float> &a = *c1->data();
  const std::vector<float> &b = *c4->data();
  QCOMPARE(a.size(), b.size());
  QVERIFY2(bitwiseEqual(a, b),
           "slab partitions 1 vs 4 produced different grid values");
  delete c1;
  delete c4;
}

// Two auto (testSlabCount = 0) runs must be bitwise identical.
void CubeEvalTest::testAutoRunsBitwiseIdentical()
{
  QString err;
  OpenQube::Cube *c1 = evalCube(0, err);
  QVERIFY2(c1 != nullptr, err.toUtf8().constData());
  OpenQube::Cube *c2 = evalCube(0, err);
  QVERIFY2(c2 != nullptr, err.toUtf8().constData());

  const std::vector<float> &a = *c1->data();
  const std::vector<float> &b = *c2->data();
  QCOMPARE(a.size(), b.size());
  QVERIFY2(bitwiseEqual(a, b),
           "two auto runs produced different grid values");
  delete c1;
  delete c2;
}

// All grid values are finite; the nucleus point and the far-corner point are
// both finite (no NaN/Inf).
void CubeEvalTest::testFiniteness()
{
  QString err;
  OpenQube::Cube *c = evalCube(4, err);
  QVERIFY2(c != nullptr, err.toUtf8().constData());
  const std::vector<float> &v = *c->data();
  QVERIFY2(finiteAll(v), "grid contains NaN/Inf");
  // Point exactly on a nucleus: finite and non-zero (the S shell at the
  // origin gives a real contribution), so the test is not trivially passing
  // on an all-zero grid.
  QVERIFY(std::isfinite(v[nucleusIndex()]));
  QVERIFY2(std::abs(v[nucleusIndex()]) > 1e-9,
           qPrintable(QString("nucleus value unexpectedly zero: %1")
                          .arg(v[nucleusIndex()])));
  // Point far outside all screening cutoffs: finite (screened to ~0).
  QVERIFY(std::isfinite(v[farCornerIndex()]));
  delete c;
}

QTEST_MAIN(CubeEvalTest)
#include "cubeevaltest.moc"
