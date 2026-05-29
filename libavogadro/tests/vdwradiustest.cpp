/**********************************************************************
  VdwRadiusTest - Unit tests for Molecular Properties vdW radius helpers

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>
 **********************************************************************/

#include "config.h"

#include <QtTest>
#include <QSurfaceFormat>

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/glwidget.h>
#include <avogadro/molecule.h>
#include <avogadro/primitivelist.h>

#include "vdwradiuscalculator.h"

#include <openbabel/elements.h>

#include <Eigen/Core>

using Avogadro::Atom;
using Avogadro::Bond;
using Avogadro::GLWidget;
using Avogadro::Molecule;
using Avogadro::Primitive;
using Avogadro::PrimitiveList;
using Eigen::Vector3d;

class VdwRadiusTest : public QObject
{
  Q_OBJECT

private:
  static void compareRadius(double actual, double expected,
                            double tolerance = 1.0e-6)
  {
    QVERIFY2(qAbs(actual - expected) < tolerance,
             qPrintable(QString("Expected %1, got %2")
                        .arg(expected, 0, 'f', 6)
                        .arg(actual, 0, 'f', 6)));
  }

  static void setCarbonRadius(Atom *atom)
  {
    atom->setCustomRadius(1.70);
  }

  static void setHydrogenRadius(Atom *atom)
  {
    atom->setCustomRadius(1.20);
  }

  static Molecule *createIdealizedMethane(Bond **chBond = 0)
  {
    Molecule *molecule = new Molecule;
    Atom *carbon = molecule->addAtom(6, Vector3d(0.0, 0.0, 0.0));
    setCarbonRadius(carbon);

    const double ch = 1.09;
    Atom *hydrogen1 = molecule->addAtom(1, Vector3d(ch, ch, ch).normalized() * ch);
    Atom *hydrogen2 = molecule->addAtom(1, Vector3d(ch, -ch, -ch).normalized() * ch);
    Atom *hydrogen3 = molecule->addAtom(1, Vector3d(-ch, ch, -ch).normalized() * ch);
    Atom *hydrogen4 = molecule->addAtom(1, Vector3d(-ch, -ch, ch).normalized() * ch);
    setHydrogenRadius(hydrogen1);
    setHydrogenRadius(hydrogen2);
    setHydrogenRadius(hydrogen3);
    setHydrogenRadius(hydrogen4);

    Bond *bond = molecule->addBond(carbon, hydrogen1, 1);
    molecule->addBond(carbon, hydrogen2, 1);
    molecule->addBond(carbon, hydrogen3, 1);
    molecule->addBond(carbon, hydrogen4, 1);

    if (chBond)
      *chBond = bond;

    return molecule;
  }

private slots:
  void singleCarbonAtom();
  void twoCarbons();
  void openBabelVdwLookup();
  void customRadiusOverridesOpenBabel();
  void idealizedMethaneWholeMolecule();
  void methaneCarbonHydrogenBondSelection();
  void waterWholeMolecule();
  void fallbackToWholeMolecule();
  void bondSelectionDeduplicatesAtoms();
  void glWidgetSelectionChangedSignal();
};

void VdwRadiusTest::singleCarbonAtom()
{
  Molecule molecule;
  Atom *carbon = molecule.addAtom(6, Vector3d(0.0, 0.0, 0.0));
  setCarbonRadius(carbon);

  QList<Atom*> atoms;
  atoms.append(carbon);

  compareRadius(Avogadro::MolecularProperties::vdwEnclosingRadius(atoms), 1.700);
}

void VdwRadiusTest::twoCarbons()
{
  Molecule molecule;
  Atom *carbon1 = molecule.addAtom(6, Vector3d(-1.0, 0.0, 0.0));
  Atom *carbon2 = molecule.addAtom(6, Vector3d(1.0, 0.0, 0.0));
  setCarbonRadius(carbon1);
  setCarbonRadius(carbon2);

  QList<Atom*> atoms;
  atoms.append(carbon1);
  atoms.append(carbon2);

  compareRadius(Avogadro::MolecularProperties::vdwEnclosingRadius(atoms), 2.700);
}

void VdwRadiusTest::openBabelVdwLookup()
{
  Molecule molecule;
  Atom *carbon = molecule.addAtom(6, Vector3d(0.0, 0.0, 0.0));

  compareRadius(Avogadro::MolecularProperties::atomVdwRadius(carbon),
                OpenBabel::OBElements::GetVdwRad(6));
}

void VdwRadiusTest::customRadiusOverridesOpenBabel()
{
  Molecule molecule;
  Atom *carbon = molecule.addAtom(6, Vector3d(0.0, 0.0, 0.0));
  carbon->setCustomRadius(2.34);

  compareRadius(Avogadro::MolecularProperties::atomVdwRadius(carbon), 2.34);
}

void VdwRadiusTest::idealizedMethaneWholeMolecule()
{
  Molecule *molecule = createIdealizedMethane();
  QList<Atom*> atoms = Avogadro::MolecularProperties::atomsForVdwRadius(molecule, 0);

  compareRadius(Avogadro::MolecularProperties::vdwEnclosingRadius(atoms), 2.290);
  delete molecule;
}

void VdwRadiusTest::methaneCarbonHydrogenBondSelection()
{
  Bond *bond = 0;
  Molecule *molecule = createIdealizedMethane(&bond);
  {
    GLWidget widget(molecule, QSurfaceFormat());

    PrimitiveList selected;
    selected.append(static_cast<Primitive*>(bond));
    widget.setSelected(selected, true);

    QList<Atom*> atoms = Avogadro::MolecularProperties::atomsForVdwRadius(molecule, &widget);
    QCOMPARE(atoms.size(), 2);
    compareRadius(Avogadro::MolecularProperties::vdwEnclosingRadius(atoms), 2.245);
  }

  delete molecule;
}

void VdwRadiusTest::waterWholeMolecule()
{
  Molecule molecule;
  Atom *oxygen = molecule.addAtom(8, Vector3d(0.000000, 0.000000, 0.000000));
  Atom *hydrogen1 = molecule.addAtom(1, Vector3d(0.957200, 0.000000, 0.000000));
  Atom *hydrogen2 = molecule.addAtom(1, Vector3d(-0.239987, 0.926627, 0.000000));
  oxygen->setCustomRadius(1.52);
  setHydrogenRadius(hydrogen1);
  setHydrogenRadius(hydrogen2);

  QList<Atom*> atoms;
  atoms.append(oxygen);
  atoms.append(hydrogen1);
  atoms.append(hydrogen2);

  compareRadius(Avogadro::MolecularProperties::vdwEnclosingRadius(atoms),
                1.982, 1.0e-3);
}

void VdwRadiusTest::fallbackToWholeMolecule()
{
  Molecule *molecule = createIdealizedMethane();
  QList<Atom*> atoms = Avogadro::MolecularProperties::atomsForVdwRadius(molecule, 0);
  QCOMPARE(atoms.size(), 5);

  {
    GLWidget widget(molecule, QSurfaceFormat());
    atoms = Avogadro::MolecularProperties::atomsForVdwRadius(molecule, &widget);
    QCOMPARE(atoms.size(), 5);
  }

  delete molecule;
}

void VdwRadiusTest::bondSelectionDeduplicatesAtoms()
{
  Molecule molecule;
  Atom *carbon1 = molecule.addAtom(6, Vector3d(0.0, 0.0, 0.0));
  Atom *carbon2 = molecule.addAtom(6, Vector3d(1.0, 0.0, 0.0));
  Bond *bond = molecule.addBond(carbon1, carbon2, 1);
  GLWidget widget(&molecule, QSurfaceFormat());

  PrimitiveList selected;
  selected.append(static_cast<Primitive*>(carbon1));
  selected.append(static_cast<Primitive*>(bond));
  widget.setSelected(selected, true);

  QList<Atom*> atoms = Avogadro::MolecularProperties::atomsForVdwRadius(&molecule, &widget);
  QCOMPARE(atoms.size(), 2);
  QVERIFY(atoms.contains(carbon1));
  QVERIFY(atoms.contains(carbon2));
}

void VdwRadiusTest::glWidgetSelectionChangedSignal()
{
  Molecule molecule;
  Atom *carbon1 = molecule.addAtom(6, Vector3d(0.0, 0.0, 0.0));
  Atom *carbon2 = molecule.addAtom(6, Vector3d(1.0, 0.0, 0.0));
  GLWidget widget(&molecule, QSurfaceFormat());
  QSignalSpy spy(&widget, SIGNAL(selectionChanged()));

  PrimitiveList selection;
  selection.append(static_cast<Primitive*>(carbon1));
  widget.setSelected(selection, true);
  QCOMPARE(spy.count(), 1);

  // Selecting the already-selected atom does not change the selection.
  widget.setSelected(selection, true);
  QCOMPARE(spy.count(), 1);

  widget.toggleSelected(selection);
  QCOMPARE(spy.count(), 2);

  PrimitiveList secondSelection;
  secondSelection.append(static_cast<Primitive*>(carbon2));
  widget.setSelected(secondSelection, true);
  QCOMPARE(spy.count(), 3);

  widget.toggleSelected();
  QCOMPARE(spy.count(), 4);

  widget.clearSelected();
  QCOMPARE(spy.count(), 5);
}

QTEST_MAIN(VdwRadiusTest)

#include "moc_vdwradiustest.cpp"
