#include "config.h"
#include <QtTest>

#include <openbabel/obconversion.h>
#include <openbabel/builder.h>
#include <openbabel/mol.h>

using OpenBabel::OBConversion;
using OpenBabel::OBMol;
using OpenBabel::OBBuilder;

class InsertFragmentTest : public QObject
{
  Q_OBJECT

private slots:
  void validSmilesBuild();
  void invalidSmilesBuild();
};

void InsertFragmentTest::validSmilesBuild()
{
  OBConversion conv;
  QVERIFY(conv.SetInFormat("smi"));
  OBMol mol;
  QVERIFY(conv.ReadString(&mol, "CCO"));

  OBBuilder builder;
  QVERIFY(builder.Build(mol));
  QVERIFY(mol.NumAtoms() > 0);
}

void InsertFragmentTest::invalidSmilesBuild()
{
  OBConversion conv;
  QVERIFY(conv.SetInFormat("smi"));
  OBMol mol;
  QVERIFY(!conv.ReadString(&mol, "C1C"));

  // OBBuilder assumes the input molecule is valid. Avoid calling
  // Build() when the SMILES conversion fails as this can crash.
  QCOMPARE(mol.NumAtoms(), static_cast<unsigned int>(0));
}

QTEST_MAIN(InsertFragmentTest)
#include "moc_insertfragmenttest.cpp"
