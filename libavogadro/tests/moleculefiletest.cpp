/**********************************************************************
  MoleculeFile - MoleculeFileTest class provides unit testing for the
  MoleculeFile class

  Copyright (C) 2009 Tim Vandermeersch

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
#include <QFile>
#include <avogadro/moleculefile.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>

#include <openbabel/mol.h>
#include <openbabel/obconversion.h>

#include <Eigen/Core>

#include <iostream>

using OpenBabel::OBMol;
using OpenBabel::OBConversion;

using Avogadro::MoleculeFile;
using Avogadro::Molecule;
using Avogadro::Atom;

using Eigen::Vector3d;

class MoleculeFileTest : public QObject
{
  Q_OBJECT

  private:
    Molecule *m_molecule; /// Molecule object for use by the test class.

  private slots:
    /**
     * Called before the first test function is executed.
     */
    void initTestCase();

    /**
     * Called after the last test function is executed.
     */
    void cleanupTestCase();

    /**
     * Called before each test function is executed.
     */
    void init();

    /**
     * Called after every test function.
     */
    void cleanup();

    void readWriteMolecule();
    void readFile();
    void readWriteConformers();
    void replaceMolecule();
    void appendMolecule();

};

void MoleculeFileTest::initTestCase()
{
}

void MoleculeFileTest::cleanupTestCase()
{
}

void MoleculeFileTest::init()
{
  m_molecule = new Molecule;

  Atom *c = m_molecule->addAtom();
  c->setAtomicNumber(6);
  c->setPos(Eigen::Vector3d(1., 2., 3.));
  Atom *n = m_molecule->addAtom();
  n->setAtomicNumber(7);
  n->setPos(Eigen::Vector3d(4., 5., 6.));
  Atom *o = m_molecule->addAtom();
  o->setAtomicNumber(8);
  o->setPos(Eigen::Vector3d(7., 8., 9.));
}

void MoleculeFileTest::cleanup()
{
  delete m_molecule;
  m_molecule = 0;
}

void MoleculeFileTest::readWriteMolecule()
{
  QString filename = "moleculefiletest_tmp.sdf";

  // writeMolecule
  QVERIFY( MoleculeFile::writeMolecule(m_molecule, filename) );
  QVERIFY( MoleculeFile::writeMolecule(m_molecule, filename, "sdf") );
  QString error;
  QVERIFY( MoleculeFile::writeMolecule(m_molecule, filename, "sdf", "", &error) );
  QVERIFY( error.isEmpty() );

  // readMolecule
  Molecule *newMolecule = MoleculeFile::readMolecule(filename);
  QVERIFY( newMolecule );
  QCOMPARE( newMolecule->numAtoms(), static_cast<unsigned int>(3));
  delete newMolecule;

  // test forced format
  QVERIFY( MoleculeFile::writeMolecule(m_molecule, filename, "xyz", "", &error) );
  QVERIFY( error.isEmpty() );
  newMolecule = MoleculeFile::readMolecule(filename, "xyz", "", &error);
  QVERIFY( error.isEmpty() );
  QVERIFY( newMolecule );
  QCOMPARE( newMolecule->numAtoms(), static_cast<unsigned int>(3));
  delete newMolecule;

  // test invalid format
  QVERIFY( !MoleculeFile::writeMolecule(m_molecule, filename, "invalid_format", "", &error) );
  QVERIFY( !error.isEmpty() );
  error.clear();
  QVERIFY( !MoleculeFile::readMolecule(filename, "invalid_format", "", &error) );
  QVERIFY( !error.isEmpty() );
  error.clear();

}

void MoleculeFileTest::readFile()
{
  QString filename = "moleculefiletest_tmp.sdf";

  OpenBabel::OBMol mol = m_molecule->OBMol();
  OBConversion conv;
  conv.SetOutFormat("sdf");
  std::ofstream ofs(filename.toLatin1().data());
  QVERIFY( ofs );
  // write the molecule 4 times...
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);
  ofs.close();



  MoleculeFile* moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY( moleculeFile );
  QVERIFY( moleculeFile->errors().isEmpty() );
  QCOMPARE( moleculeFile->isConformerFile(), true );
  QCOMPARE( moleculeFile->numMolecules(), static_cast<unsigned int>(1) );
  QCOMPARE( moleculeFile->conformers().size(), 
      static_cast<std::vector<int>::size_type>(4) );


  ofs.open(filename.toLatin1().data());
  QVERIFY( ofs );
  // write the molecule 4 times...
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);
  mol.NewAtom();
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);

  moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY( moleculeFile );
  QVERIFY( moleculeFile->errors().isEmpty() );
  QCOMPARE( moleculeFile->isConformerFile(), false );
  QCOMPARE( moleculeFile->numMolecules(), static_cast<unsigned int>(4) );
  QCOMPARE( moleculeFile->conformers().size(), 
      static_cast<std::vector<int>::size_type>(0) );
}

void MoleculeFileTest::readWriteConformers()
{
  std::vector<Eigen::Vector3d> conformer;
  foreach(Atom *atom, m_molecule->atoms()) {
    Q_UNUSED(atom)
    conformer.push_back(Eigen::Vector3d(0., 1., 2.));
  }

  std::vector<std::vector<Eigen::Vector3d> *> conformers;
  conformers.push_back(new std::vector<Eigen::Vector3d>(conformer));
  conformers.push_back(new std::vector<Eigen::Vector3d>(conformer));
  conformers.push_back(new std::vector<Eigen::Vector3d>(conformer));
  conformers.push_back(new std::vector<Eigen::Vector3d>(conformer));
  m_molecule->setAllConformers(conformers);

  QString filename = "moleculefiletest_tmp.sdf";
  QVERIFY( MoleculeFile::writeConformers(m_molecule, filename) );

  MoleculeFile* moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY( moleculeFile );
  QVERIFY( moleculeFile->errors().isEmpty() );
  QCOMPARE( moleculeFile->isConformerFile(), true );
  QCOMPARE( moleculeFile->numMolecules(), static_cast<unsigned int>(1) );
  QCOMPARE( moleculeFile->conformers().size(), 
      static_cast<std::vector<int>::size_type>(4) );
}

void MoleculeFileTest::replaceMolecule()
{
  QString filename = "moleculefiletest_tmp.smi";

  auto dumpState = [&](const QString &context, MoleculeFile *file) {
    std::cerr << "replaceMolecule debug: "
              << context.toLocal8Bit().constData() << std::endl;
    if (file) {
      const QByteArray errors = file->errors().toLocal8Bit();
      std::cerr << "MoleculeFile errors: " << errors.constData() << std::endl;
    }
    QFile f(filename);
    if (f.open(QIODevice::ReadOnly)) {
      const QByteArray contents = f.readAll();
      std::cerr << "Current file contents: " << contents.constData() << std::endl;
    }
  };
  std::ofstream ofs(filename.toLatin1().data(), std::ios::out | std::ios::binary);
  ofs << "c1ccccc1  phenyl\n";
  ofs << "c1ccccc1N  aniline\n";
  // Use the more standard SMILES form with the methyl group first to avoid
  // Open Babel's kekulization warnings.
  ofs << "Cc1ccccc1  toluene\n";
  ofs.close();

  MoleculeFile* moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY( moleculeFile );
  QVERIFY( moleculeFile->errors().isEmpty() );
  QCOMPARE( moleculeFile->isConformerFile(), false );
  QCOMPARE( moleculeFile->numMolecules(), static_cast<unsigned int>(3) );

  // check 1st molecule
  Molecule *phenyl = moleculeFile->molecule(0);
  QVERIFY( phenyl );
  QVERIFY( phenyl->numAtoms() > 0 );
  delete phenyl;

  // check 2nd molecule
  Molecule *aniline = moleculeFile->molecule(1);
  if (!aniline)
    dumpState("aniline read failed", moleculeFile);
  QVERIFY( aniline );
  QVERIFY( aniline->numAtoms() > 0 );
  delete aniline;

  // check 3th molecule
  Molecule *toluene = moleculeFile->molecule(2);
  if (!toluene)
    dumpState("toluene read failed", moleculeFile);
  QVERIFY( toluene );
  QVERIFY( toluene->numAtoms() > 0 );
  delete toluene;

  // replace 2nd
  aniline = moleculeFile->molecule(1);
  QVERIFY( aniline );
  aniline->addAtom()->setAtomicNumber(6);
  aniline->addAtom()->setAtomicNumber(6);
  const bool replaceSecond = moleculeFile->replaceMolecule(1, aniline, filename);
  if (!replaceSecond)
    dumpState("replaceMolecule(1) failed", moleculeFile);
  QVERIFY2( replaceSecond, moleculeFile->errors().toLatin1().constData() );
  delete aniline;

  // check again
  aniline = moleculeFile->molecule(1);
  QVERIFY( aniline );
  QVERIFY( aniline->numAtoms() > 0 );
  delete aniline;

  phenyl = moleculeFile->molecule(0);
  QVERIFY( phenyl );
  QVERIFY( phenyl->numAtoms() > 0 );
  delete phenyl;

  toluene = moleculeFile->molecule(2);
  QVERIFY( toluene );
  QVERIFY( toluene->numAtoms() > 0 );
  delete toluene;

  // replace 1st
  phenyl = moleculeFile->molecule(0);
  QVERIFY( phenyl );
  phenyl->addAtom()->setAtomicNumber(6);
  phenyl->addAtom()->setAtomicNumber(6);
  const bool replaceFirst = moleculeFile->replaceMolecule(0, phenyl, filename);
  if (!replaceFirst)
    dumpState("replaceMolecule(0) failed", moleculeFile);
  QVERIFY2( replaceFirst, moleculeFile->errors().toLatin1().constData() );
  delete phenyl;
  // check again
  phenyl = moleculeFile->molecule(0);
  QVERIFY( phenyl );
  QVERIFY( phenyl->numAtoms() > 0 );
  delete phenyl;

  aniline = moleculeFile->molecule(1);
  QVERIFY( aniline );
  QVERIFY( aniline->numAtoms() > 0 );
  delete aniline;

  toluene = moleculeFile->molecule(2);
  QVERIFY( toluene );
  QVERIFY( toluene->numAtoms() > 0 );
  delete toluene;
 

  delete moleculeFile;
}

void MoleculeFileTest::appendMolecule()
{
  QString filename = "moleculefiletest_tmp.smi";
  std::ofstream ofs(filename.toLatin1().data(), std::ios::out | std::ios::binary);
  ofs << "c1ccccc1  phenyl\n";
  ofs << "c1ccccc1N  aniline\n";
  // Use the standard SMILES with the substituent before the ring to avoid
  // Open Babel kekulization warnings.
  ofs << "Cc1ccccc1  toluene\n";
  ofs.close();

  MoleculeFile* moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY( moleculeFile );
  QVERIFY( moleculeFile->errors().isEmpty() );
  QCOMPARE( moleculeFile->isConformerFile(), false );
  QCOMPARE( moleculeFile->numMolecules(), static_cast<unsigned int>(3) );
}

QTEST_MAIN(MoleculeFileTest)

#include "moc_moleculefiletest.cpp"
