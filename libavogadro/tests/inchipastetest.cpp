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

  QCOMPARE(mol.numAtoms(), static_cast<unsigned int>(5));
}

QTEST_MAIN(InChIPasteTest)
#include "moc_inchipastetest.cpp"
