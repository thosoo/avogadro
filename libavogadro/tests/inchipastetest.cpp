#include "config.h"
#include <QtTest>
#include <avogadro/molecule.h>
#include <openbabel/mol.h>
#include <openbabel/builder.h>
#include <openbabel/obconversion.h>

using Avogadro::Molecule;
using OpenBabel::OBMol;
using OpenBabel::OBBuilder;
using OpenBabel::OBConversion;

class InChIPasteTest : public QObject
{
  Q_OBJECT
private slots:
  void buildFromInChI();
  void heterocycleInChI();
};

void InChIPasteTest::buildFromInChI()
{
  OBMol obmol;
  OBConversion conv;
  QVERIFY(conv.SetInFormat("inchi"));
  QVERIFY(conv.ReadString(&obmol, "InChI=1S/CH4/h1H4"));

  OBBuilder builder;
  builder.Build(obmol);

  Molecule mol;
  mol.setOBMol(&obmol);
  mol.addHydrogens();

  QCOMPARE(mol.numAtoms(), static_cast<unsigned int>(5));
}

void InChIPasteTest::heterocycleInChI()
{
  struct Case { const char* inchi; unsigned int atoms; } cases[] = {
    {"InChI=1S/C5H5N/c1-2-4-6-5-3-1/h1-5H", 11}, // Pyridine
    {"InChI=1S/C4H4O/c1-2-4-5-3-1/h1-4H", 9},    // Furan
    {"InChI=1S/C4H4S/c1-2-4-5-3-1/h1-4H", 9},    // Thiophene
    {"InChI=1S/C4H4N2/c1-2-5-4-6-3-1/h1-4H", 10}, // Pyrimidine
    {"InChI=1S/C8H7N/c1-2-4-8-7(3-1)5-6-9-8/h1-6,9H", 16}, // Indole
    {"InChI=1S/C13H5N5S/c14-6-8-5-9(7-15)12-13(18-19-17-12)11(8)10-3-1-2-4-16-10/h1-5H", 24} // large heterocycle
  };

  OBConversion conv;
  QVERIFY(conv.SetInFormat("inchi"));
  OBBuilder builder;

  for (const auto &c : cases) {
    OBMol obmol;
    QVERIFY(conv.ReadString(&obmol, c.inchi));
    builder.Build(obmol);

    Molecule mol;
    mol.setOBMol(&obmol);
    mol.addHydrogens();

    QCOMPARE(mol.numAtoms(), c.atoms);
  }
}

QTEST_MAIN(InChIPasteTest)
#include "moc_inchipastetest.cpp"
