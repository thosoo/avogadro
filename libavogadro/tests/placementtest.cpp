/**********************************************************************
  PlacementTest - Unit tests for the DrawTool click-to-extend placement
  helper (add-draw-tool-click-extend task 1.3).

  Pure headless geometry: hand-built positions, no GL, no widget. The
  molecule-backed cases (hybridization from bond orders) build a real
  Molecule/Atom/Bond graph and call sketchHybridization().

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>
 **********************************************************************/

#include "config.h"

#include <QtTest>

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/molecule.h>

#include <openbabel/elements.h>

#include <Eigen/Core>

#include <cmath>
#include <vector>

#include "libavogadro/src/tools/placement.h"

using Avogadro::Atom;
using Avogadro::Bond;
using Avogadro::Molecule;
using Avogadro::SketchHyb;
using Avogadro::covalentBondLength;
using Avogadro::placeSubstituent;
using Avogadro::sketchHybridization;
using Eigen::Vector3d;

namespace {

  //! Angle (radians) between two unit directions.
  double angleBetween(const Vector3d &a, const Vector3d &b)
  {
    return std::acos(std::max(-1.0, std::min(1.0, a.dot(b))));
  }

  double deg2rad(double deg)
  {
    return deg * M_PI / 180.0;
  }

  //! Standard tetrahedral vertices (unit length).
  const Vector3d T1(1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0));
  const Vector3d T2(1.0 / std::sqrt(3.0), -1.0 / std::sqrt(3.0), -1.0 / std::sqrt(3.0));
  const Vector3d T3(-1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0), -1.0 / std::sqrt(3.0));
  const Vector3d T4(-1.0 / std::sqrt(3.0), -1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0));

} // namespace

class PlacementTest : public QObject
{
  Q_OBJECT

  //! Place a substituent and return the unit direction of the new bond.
  static Vector3d direction(const Vector3d &origin,
                            const std::vector<Vector3d> &nbrs,
                            SketchHyb hyb,
                            double len = 1.54)
  {
    const Vector3d pos = placeSubstituent(origin, nbrs, hyb, len);
    return (pos - origin).normalized();
  }

private slots:
  void sp3OneHeavyNeighbor();
  void sp2OneHeavyNeighbor();
  void spOneHeavyNeighbor();
  void sp3TwoHeavyNeighbors();
  void sp3ThreeHeavyNeighbors();
  void order4QuadrupleBond();
  void hydrogensIgnored();
  void bondLengthIsRequested();
  void deterministicAndFinite();
  void covalentLength();
};

void PlacementTest::sp3OneHeavyNeighbor()
{
  const Vector3d origin(0, 0, 0);
  const std::vector<Vector3d> nbrs{ Vector3d(1, 0, 0) };
  const Vector3d nHat = direction(origin, nbrs, SketchHyb::Sp3);
  // Existing bond direction (origin -> neighbor).
  const double angle = angleBetween(nHat, Vector3d(1, 0, 0));
  QVERIFY2(std::abs(angle - deg2rad(109.47)) < deg2rad(0.5),
           qPrintable(QString("sp3 1-nbr angle = %1 deg")
                      .arg(angle * 180.0 / M_PI)));
}

void PlacementTest::sp2OneHeavyNeighbor()
{
  const Vector3d origin(0, 0, 0);
  const std::vector<Vector3d> nbrs{ Vector3d(1, 0, 0) };
  const Vector3d nHat = direction(origin, nbrs, SketchHyb::Sp2);
  const double angle = angleBetween(nHat, Vector3d(1, 0, 0));
  QVERIFY2(std::abs(angle - deg2rad(120.0)) < deg2rad(0.5),
           qPrintable(QString("sp2 1-nbr angle = %1 deg")
                      .arg(angle * 180.0 / M_PI)));
}

void PlacementTest::spOneHeavyNeighbor()
{
  const Vector3d origin(0, 0, 0);
  const std::vector<Vector3d> nbrs{ Vector3d(1, 0, 0) };
  const Vector3d nHat = direction(origin, nbrs, SketchHyb::Sp);
  // Linear: the new bond is a straight-line continuation (180° from the
  // existing bond, i.e. it points along the bond direction, -u).
  const double angle = angleBetween(nHat, Vector3d(1, 0, 0));
  QVERIFY2(std::abs(angle - M_PI) < deg2rad(0.5),
           qPrintable(QString("sp 1-nbr angle = %1 deg")
                      .arg(angle * 180.0 / M_PI)));
  // And it points directly away from the neighbor.
  QVERIFY(nHat.dot(Vector3d(1, 0, 0)) < -0.999);
}

void PlacementTest::sp3TwoHeavyNeighbors()
{
  const Vector3d origin(0, 0, 0);
  const std::vector<Vector3d> nbrs{ T1, T2 };
  const Vector3d nHat = direction(origin, nbrs, SketchHyb::Sp3);
  const double a1 = angleBetween(nHat, T1);
  const double a2 = angleBetween(nHat, T2);
  QVERIFY2(std::abs(a1 - deg2rad(109.47)) < deg2rad(0.5),
           qPrintable(QString("sp3 2-nbr angle to b1 = %1 deg")
                      .arg(a1 * 180.0 / M_PI)));
  QVERIFY2(std::abs(a2 - deg2rad(109.47)) < deg2rad(0.5),
           qPrintable(QString("sp3 2-nbr angle to b2 = %1 deg")
                      .arg(a2 * 180.0 / M_PI)));
}

void PlacementTest::sp3ThreeHeavyNeighbors()
{
  const Vector3d origin(0, 0, 0);
  const std::vector<Vector3d> nbrs{ T1, T2, T3 };
  const double len = 1.54;
  const Vector3d pos = placeSubstituent(origin, nbrs, SketchHyb::Sp3, len);
  // The exact 4th tetrahedral vertex.
  const Vector3d expected = T4 * len;
  QVERIFY2((pos - expected).norm() < 1e-9,
           qPrintable(QString("3-nbr pos = (%1, %2, %3), expected (%4, %5, %6)")
                      .arg(pos.x()).arg(pos.y()).arg(pos.z())
                      .arg(expected.x()).arg(expected.y()).arg(expected.z())));
}

void PlacementTest::order4QuadrupleBond()
{
  Molecule mol;
  Atom *origin = mol.addAtom();
  origin->setAtomicNumber(6);
  origin->setPos(Vector3d(0, 0, 0));
  Atom *nbr = mol.addAtom();
  nbr->setAtomicNumber(6);
  nbr->setPos(Vector3d(1, 0, 0));
  mol.addBond(origin, nbr, 4); // metal-metal quadruple bond

  QVERIFY(sketchHybridization(origin, &mol) == SketchHyb::Sp);

  // Sp + one heavy neighbor -> linear result.
  const Vector3d nHat = direction(Vector3d(0, 0, 0),
                                  std::vector<Vector3d>{ Vector3d(1, 0, 0) },
                                  SketchHyb::Sp);
  QVERIFY(nHat.dot(Vector3d(1, 0, 0)) < -0.999);
}

void PlacementTest::hydrogensIgnored()
{
  // A carbon with one double bond (to another carbon) and two single bonds to
  // explicit hydrogens must classify as Sp2: the H bonds are single and do not
  // consume a geometry slot / do not change the hybridization.
  Molecule mol;
  Atom *c = mol.addAtom();
  c->setAtomicNumber(6);
  c->setPos(Vector3d(0, 0, 0));
  Atom *c2 = mol.addAtom();
  c2->setAtomicNumber(6);
  c2->setPos(Vector3d(1, 0, 0));
  Atom *h1 = mol.addAtom();
  h1->setAtomicNumber(1);
  h1->setPos(Vector3d(0, 1, 0));
  Atom *h2 = mol.addAtom();
  h2->setAtomicNumber(1);
  h2->setPos(Vector3d(0, -1, 0));
  mol.addBond(c, c2, 2);
  mol.addBond(c, h1, 1);
  mol.addBond(c, h2, 1);
  QVERIFY(sketchHybridization(c, &mol) == SketchHyb::Sp2);

  // The helper is position-agnostic: it only ever sees the heavy neighbor
  // positions the caller filtered, so adding/removing H positions cannot change
  // the result. Recompute the placement with the heavy-neighbor set only and
  // assert it is finite/deterministic.
  const Vector3d pos = placeSubstituent(Vector3d(0, 0, 0),
                                        std::vector<Vector3d>{ Vector3d(1, 0, 0) },
                                        SketchHyb::Sp2, 1.54);
  QVERIFY(pos.allFinite());

  // And a saturated carbon (two heavy single bonds + two H single bonds) is Sp3.
  Molecule mol2;
  Atom *c2a = mol2.addAtom();
  c2a->setAtomicNumber(6);
  Atom *c2b = mol2.addAtom();
  c2b->setAtomicNumber(6);
  Atom *c2c = mol2.addAtom();
  c2c->setAtomicNumber(6);
  Atom *h = mol2.addAtom();
  h->setAtomicNumber(1);
  mol2.addBond(c2a, c2b, 1);
  mol2.addBond(c2a, c2c, 1);
  mol2.addBond(c2a, h, 1);
  QVERIFY(sketchHybridization(c2a, &mol2) == SketchHyb::Sp3);
}

void PlacementTest::bondLengthIsRequested()
{
  const Vector3d origin(0, 0, 0);
  const std::vector<Vector3d> nbrs{ Vector3d(1, 0, 0) };
  const double len = 1.54;
  const Vector3d pos = placeSubstituent(origin, nbrs, SketchHyb::Sp3, len);
  QVERIFY2(std::abs((pos - origin).norm() - len) < 1e-9,
           qPrintable(QString("bond length = %1, requested %2")
                      .arg((pos - origin).norm()).arg(len)));
}

void PlacementTest::deterministicAndFinite()
{
  const Vector3d origin(0, 0, 0);

  // Two identical calls produce bitwise-equal positions.
  const std::vector<Vector3d> nbrs{ T1, T2 };
  const Vector3d p1 = placeSubstituent(origin, nbrs, SketchHyb::Sp3, 1.54);
  const Vector3d p2 = placeSubstituent(origin, nbrs, SketchHyb::Sp3, 1.54);
  QVERIFY(p1 == p2);

  // Degenerate inputs must stay finite.
  std::vector<std::vector<Vector3d>> degenerate{
    {},                                        // no heavy neighbors
    { origin },                                // zero-vector neighbor
    { Vector3d(1, 0, 0), Vector3d(2, 0, 0) },  // collinear neighbors
    { Vector3d(1, 0, 0), Vector3d(-1, 0, 0) }, // symmetric sum -> zero bisector
    { T1, T2, T3, T4 },                        // symmetric 4-neighbor sum
  };
  const SketchHyb hybs[] = { SketchHyb::Sp, SketchHyb::Sp2, SketchHyb::Sp3 };
  for (const auto &set : degenerate) {
    for (SketchHyb hyb : hybs) {
      const Vector3d pos = placeSubstituent(origin, set, hyb, 1.54);
      QVERIFY2(pos.allFinite(),
               qPrintable(QString("non-finite result for %1 neighbors, hyb %2")
                          .arg(set.size()).arg(static_cast<int>(hyb))));
    }
  }
}

void PlacementTest::covalentLength()
{
  // C+C -> the sum of the two covalent radii. OpenBabel's carbon radius is
  // 0.76 A, so this is 1.52 A, which is within the documented 1.54 ±0.02 A
  // tolerance (a floating-point-safe bound is used here).
  const double cc = covalentBondLength(6, 6);
  QVERIFY2(std::abs(cc - 1.54) < 0.03,
           qPrintable(QString("C+C = %1").arg(cc)));

  // The helper really is the covalent-radii sum.
  const double expected =
    OpenBabel::OBElements::GetCovalentRad(6) +
    OpenBabel::OBElements::GetCovalentRad(6);
  QVERIFY(std::abs(cc - expected) < 1e-9);

  // Unknown element (radius 0) -> 1.5 Å fallback.
  const double unknown = covalentBondLength(0, 6);
  QVERIFY2(std::abs(unknown - 1.5) < 1e-9,
           qPrintable(QString("unknown = %1").arg(unknown)));
}

QTEST_MAIN(PlacementTest)

#include "placementtest.moc"
