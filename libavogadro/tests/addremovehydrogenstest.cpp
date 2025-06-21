#include "config.h"
#include <QtTest>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>

using Avogadro::Molecule;
using Avogadro::Atom;

class AddRemoveHydrogensTest : public QObject
{
  Q_OBJECT
private slots:
  void addRemove();
};

void AddRemoveHydrogensTest::addRemove()
{
  Molecule mol;
  Atom *c = mol.addAtom();
  c->setAtomicNumber(6);

  mol.addHydrogens(c);
  QCOMPARE(mol.numAtoms(), static_cast<unsigned int>(5));

  mol.removeHydrogens(c);
  QCOMPARE(mol.numAtoms(), static_cast<unsigned int>(1));
}

QTEST_MAIN(AddRemoveHydrogensTest)
#include "moc_addremovehydrogenstest.cpp"
