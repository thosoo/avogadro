/*
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
*/

#include "config.h"

#include <QtTest>
#include <QFile>
#include <QDir>

#include <avogadro/moleculefile.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>

#include <openbabel/obconversion.h>
#include <openbabel/mol.h>

#include <Eigen/Core>

#include <memory>

using OpenBabel::OBMol;
using OpenBabel::OBConversion;

using Avogadro::MoleculeFile;
using Avogadro::Molecule;
using Avogadro::Atom;

using Eigen::Vector3d;

namespace
{
QString tempFilePath(const QString &name)
{
  return QDir::temp().absoluteFilePath(name);
}
}

class MoleculeFileTest : public QObject
{
  Q_OBJECT

private:
  Molecule* m_molecule;

private slots:
  void initTestCase() {}
  void cleanupTestCase() {}

  void init();
  void cleanup();

  void readWriteMolecule();
  void readFile();
  void readWriteConformers();
  void replaceMolecule();
  void appendMolecule();
};

void MoleculeFileTest::init()
{
  m_molecule = new Molecule;
  Atom* c = m_molecule->addAtom();
  c->setAtomicNumber(6);
  c->setPos(Vector3d(1., 2., 3.));
  Atom* n = m_molecule->addAtom();
  n->setAtomicNumber(7);
  n->setPos(Vector3d(4., 5., 6.));
  Atom* o = m_molecule->addAtom();
  o->setAtomicNumber(8);
  o->setPos(Vector3d(7., 8., 9.));
}

void MoleculeFileTest::cleanup()
{
  delete m_molecule;
  m_molecule = nullptr;
}

void MoleculeFileTest::readWriteMolecule()
{
  QString filename = tempFilePath("moleculefiletest_tmp.sdf");

  QVERIFY(MoleculeFile::writeMolecule(m_molecule, filename));
  QVERIFY(MoleculeFile::writeMolecule(m_molecule, filename, "sdf"));
  QString error;
  QVERIFY(MoleculeFile::writeMolecule(m_molecule, filename, "sdf", "", &error));
  QVERIFY(error.isEmpty());

  Molecule* newMolecule = MoleculeFile::readMolecule(filename);
  QVERIFY(newMolecule);
  QCOMPARE(newMolecule->numAtoms(), static_cast<unsigned int>(3));
  delete newMolecule;

  QVERIFY(MoleculeFile::writeMolecule(m_molecule, filename, "xyz", "", &error));
  QVERIFY(error.isEmpty());
  newMolecule = MoleculeFile::readMolecule(filename, "xyz", "", &error);
  QVERIFY(error.isEmpty());
  QVERIFY(newMolecule);
  QCOMPARE(newMolecule->numAtoms(), static_cast<unsigned int>(3));
  delete newMolecule;

  QVERIFY(!MoleculeFile::writeMolecule(m_molecule, filename, "invalid_format", "", &error));
  QVERIFY(!error.isEmpty());
  error.clear();
  QVERIFY(!MoleculeFile::readMolecule(filename, "invalid_format", "", &error));
  QVERIFY(!error.isEmpty());
}

void MoleculeFileTest::readFile()
{
  QString filename = tempFilePath("moleculefiletest_tmp.sdf");
  const QByteArray fname = QFile::encodeName(filename);

  OBMol mol = m_molecule->OBMol();
  OBConversion conv;
  conv.SetOutFormat("sdf");

  {
    std::ofstream ofs(fname.constData(), std::ios::out | std::ios::binary | std::ios::trunc);
    QVERIFY(ofs);
    conv.Write(&mol, &ofs);
    conv.Write(&mol, &ofs);
    conv.Write(&mol, &ofs);
    conv.Write(&mol, &ofs);
  }

  std::unique_ptr<MoleculeFile> moleculeFile{MoleculeFile::readFile(fname.constData())};
  QVERIFY(moleculeFile);
  QVERIFY(moleculeFile->errors().isEmpty());
  QCOMPARE(moleculeFile->isConformerFile(), true);
  QCOMPARE(moleculeFile->numMolecules(), static_cast<unsigned int>(1));
  QCOMPARE(moleculeFile->conformers().size(), static_cast<std::vector<int>::size_type>(4));

  {
    std::ofstream ofs(fname.constData(), std::ios::out | std::ios::binary | std::ios::trunc);
    QVERIFY(ofs);
    conv.Write(&mol, &ofs);
    conv.Write(&mol, &ofs);
    mol.NewAtom();
    conv.Write(&mol, &ofs);
    conv.Write(&mol, &ofs);
  }

  moleculeFile.reset(MoleculeFile::readFile(fname.constData()));
  QVERIFY(moleculeFile);
  QVERIFY(moleculeFile->errors().isEmpty());
  QCOMPARE(moleculeFile->isConformerFile(), false);
  QCOMPARE(moleculeFile->numMolecules(), static_cast<unsigned int>(4));
  QCOMPARE(moleculeFile->conformers().size(), static_cast<std::vector<int>::size_type>(0));
}

void MoleculeFileTest::readWriteConformers()
{
  std::vector<Vector3d> conformer;
  foreach(Atom* atom, m_molecule->atoms()) {
    Q_UNUSED(atom)
    conformer.push_back(Vector3d(0., 1., 2.));
  }

  std::vector<std::vector<Vector3d>*> conformers;
  conformers.push_back(new std::vector<Vector3d>(conformer));
  conformers.push_back(new std::vector<Vector3d>(conformer));
  conformers.push_back(new std::vector<Vector3d>(conformer));
  conformers.push_back(new std::vector<Vector3d>(conformer));
  m_molecule->setAllConformers(conformers);

  QString filename = tempFilePath("moleculefiletest_tmp.sdf");
  const QByteArray fname = QFile::encodeName(filename);

  QVERIFY(MoleculeFile::writeConformers(m_molecule, filename));

  std::unique_ptr<MoleculeFile> moleculeFile{MoleculeFile::readFile(fname.constData())};
  QVERIFY(moleculeFile);
  QVERIFY(moleculeFile->errors().isEmpty());
  QCOMPARE(moleculeFile->isConformerFile(), true);
  QCOMPARE(moleculeFile->numMolecules(), static_cast<unsigned int>(1));
  QCOMPARE(moleculeFile->conformers().size(), static_cast<std::vector<int>::size_type>(4));
}

static void writeSmilesFile(const QByteArray& fname)
{
  std::ofstream ofs(fname.constData(), std::ios::out | std::ios::binary | std::ios::trunc);
  ofs << "c1ccccc1\tphenyl\n"
      << "c1ccccc1N\taniline\n"
      << "Cc1ccccc1\ttoluene\n";
}

void MoleculeFileTest::replaceMolecule()
{
  QString filename = tempFilePath("moleculefiletest_tmp.smi");
  const QByteArray fname = QFile::encodeName(filename);

  writeSmilesFile(fname);

  std::unique_ptr<MoleculeFile> moleculeFile{MoleculeFile::readFile(fname.constData())};
  QVERIFY(moleculeFile);
  QVERIFY(moleculeFile->errors().isEmpty());
  QCOMPARE(moleculeFile->isConformerFile(), false);
  QCOMPARE(moleculeFile->numMolecules(), static_cast<unsigned int>(3));

  Molecule* phenyl = moleculeFile->molecule(0);
  QCOMPARE(phenyl->numAtoms(), static_cast<unsigned int>(6));
  delete phenyl;

  Molecule* aniline = moleculeFile->molecule(1);
  QCOMPARE(aniline->numAtoms(), static_cast<unsigned int>(7));
  QCOMPARE(aniline->atom(6)->atomicNumber(), 7);
  delete aniline;

  Molecule* toluene = moleculeFile->molecule(2);
  QCOMPARE(toluene->numAtoms(), static_cast<unsigned int>(7));
  QCOMPARE(toluene->atom(6)->atomicNumber(), 6);
  delete toluene;

  aniline = moleculeFile->molecule(1);
  aniline->addAtom();
  aniline->addAtom();
  QVERIFY(moleculeFile->replaceMolecule(1, aniline, filename));
  delete aniline;

  aniline = moleculeFile->molecule(1);
  QVERIFY(aniline);
  QCOMPARE(aniline->numAtoms(), static_cast<unsigned int>(9));
  QCOMPARE(aniline->atom(6)->atomicNumber(), 7);
  delete aniline;
  toluene = moleculeFile->molecule(2);
  QVERIFY(toluene);
  QCOMPARE(toluene->numAtoms(), static_cast<unsigned int>(7));
  QCOMPARE(toluene->atom(6)->atomicNumber(), 6);
  delete toluene;

  phenyl = moleculeFile->molecule(0);
  phenyl->addAtom();
  phenyl->addAtom();
  QVERIFY(moleculeFile->replaceMolecule(0, phenyl, filename));
  delete phenyl;
  phenyl = moleculeFile->molecule(0);
  QVERIFY(phenyl);
  QCOMPARE(phenyl->numAtoms(), static_cast<unsigned int>(8));
  delete phenyl;
}

void MoleculeFileTest::appendMolecule()
{
  QString filename = tempFilePath("moleculefiletest_tmp.smi");
  const QByteArray fname = QFile::encodeName(filename);

  writeSmilesFile(fname);

  std::unique_ptr<MoleculeFile> moleculeFile{MoleculeFile::readFile(fname.constData())};
  QVERIFY(moleculeFile);
  QVERIFY(moleculeFile->errors().isEmpty());
  QCOMPARE(moleculeFile->isConformerFile(), false);
  QCOMPARE(moleculeFile->numMolecules(), static_cast<unsigned int>(3));
}

QTEST_MAIN(MoleculeFileTest)
#include "moc_moleculefiletest.cpp"

