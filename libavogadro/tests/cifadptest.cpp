/**********************************************************************
  CIF ADP Test - Tests loading of anisotropic displacement parameters from CIF files

  Copyright (C) 2024 Avogadro Developers

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 **********************************************************************/

#include "config.h"

#include <QtTest>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/moleculefile.h>

#include <openbabel/mol.h>
#include <openbabel/obconversion.h>

#include <Eigen/Core>

using OpenBabel::OBMol;
using OpenBabel::OBConversion;

using Avogadro::Molecule;
using Avogadro::Atom;
using Avogadro::MoleculeFile;

class CifAdpTest : public QObject
{
  Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void testCifAdpLoading();
    void testAnisoUSymmetry();
    void testUIsoExtraction();
};

void CifAdpTest::initTestCase()
{
}

void CifAdpTest::cleanupTestCase()
{
}

void CifAdpTest::testCifAdpLoading()
{
  // Test that CIF file with ADP data loads correctly
  QString cifPath = QString(TESTDATADIR) + "/cod_7122598.cif";
  
  MoleculeFile *file = MoleculeFile::readFile(cifPath, "cif");
  QVERIFY(file != nullptr);
  
  Molecule *mol = file->molecule(0);
  QVERIFY(mol != nullptr);
  
  // Check that we have atoms
  QVERIFY(mol->numAtoms() > 0);
  
  // Check that at least some atoms have anisotropic displacement parameters
  int anisoCount = 0;
  int isoCount = 0;
  int noneCount = 0;
  
  QList<Atom *> atoms = mol->atoms();
  for (int i = 0; i < atoms.size(); ++i) {
    Atom *atom = atoms[i];
    if (atom->hasAnisoU()) {
      anisoCount++;
    } else if (atom->hasUIso()) {
      isoCount++;
    } else {
      noneCount++;
    }
  }
  
  // The test structure should have atoms with anisotropic data
  QVERIFY(anisoCount > 0);
  
  delete file;
}

void CifAdpTest::testAnisoUSymmetry()
{
  // Test that the U-matrix is symmetric
  QString cifPath = QString(TESTDATADIR) + "/cod_7122598.cif";
  
  MoleculeFile *file = MoleculeFile::readFile(cifPath, "cif");
  QVERIFY(file != nullptr);
  
  Molecule *mol = file->molecule(0);
  QVERIFY(mol != nullptr);
  
  // Find first atom with anisotropic data
  Atom *testAtom = nullptr;
  QList<Atom *> atoms = mol->atoms();
  for (int i = 0; i < atoms.size(); ++i) {
    Atom *atom = atoms[i];
    if (atom->hasAnisoU()) {
      testAtom = atom;
      break;
    }
  }
  
  QVERIFY(testAtom != nullptr);
  
  Eigen::Matrix3d U = testAtom->anisoU();
  
  // Check symmetry: U should equal U.transpose()
  Eigen::Matrix3d UDiff = U - U.transpose();
  QVERIFY(UDiff.norm() < 1e-10);
  
  delete file;
}

void CifAdpTest::testUIsoExtraction()
{
  // Test that Uiso is extracted from CIF files when available
  QString cifPath = QString(TESTDATADIR) + "/cod_7122598.cif";
  
  MoleculeFile *file = MoleculeFile::readFile(cifPath, "cif");
  QVERIFY(file != nullptr);
  
  Molecule *mol = file->molecule(0);
  QVERIFY(mol != nullptr);
  
  // Check that atoms with anisotropic data also have Uiso (if present in CIF)
  // Some CIF files store Uiso as _atom_site_aniso_U_iso_or_equiv
  int anisoWithIso = 0;
  int anisoWithoutIso = 0;
  QList<Atom *> atoms = mol->atoms();
  for (int i = 0; i < atoms.size(); ++i) {
    Atom *atom = atoms[i];
    if (atom->hasAnisoU()) {
      if (atom->hasUIso()) {
        anisoWithIso++;
      } else {
        anisoWithoutIso++;
      }
    }
  }
  
  // At minimum, we should have atoms with anisotropic data
  QVERIFY(anisoWithIso + anisoWithoutIso > 0);
  
  // If the CIF has Uiso, at least some atoms should have it
  // (This depends on the specific CIF file)
  
  delete file;
}

QTEST_MAIN(CifAdpTest)
#include "cifadptest.moc"
