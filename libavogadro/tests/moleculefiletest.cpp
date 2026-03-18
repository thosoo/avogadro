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
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>
#include <avogadro/moleculefile.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>

#include <openbabel/mol.h>
#include <openbabel/obconversion.h>

#include <Eigen/Core>

#include <sstream>

using OpenBabel::OBMol;
using OpenBabel::OBConversion;

using Avogadro::MoleculeFile;
using Avogadro::Molecule;
using Avogadro::Atom;

using Eigen::Vector3d;


namespace {
unsigned int countAtomsWithAtomicNumber(Molecule *molecule, unsigned int atomicNumber)
{
  unsigned int count = 0;
  foreach (Atom *atom, molecule->atoms()) {
    if (atom->atomicNumber() == atomicNumber)
      ++count;
  }
  return count;
}

QString moleculeAtomSummary(Molecule *molecule)
{
  if (!molecule)
    return QStringLiteral("<null molecule>");

  return QStringLiteral("numAtoms=%1 C=%2 N=%3 O=%4")
      .arg(molecule->numAtoms())
      .arg(countAtomsWithAtomicNumber(molecule, 6))
      .arg(countAtomsWithAtomicNumber(molecule, 7))
      .arg(countAtomsWithAtomicNumber(molecule, 8));
}

QString replaceDebugContext(MoleculeFile *moleculeFile, const char *label, Molecule *molecule)
{
  QStringList debug;
  debug << QString::fromLatin1(label);
  debug << moleculeAtomSummary(molecule);
  if (moleculeFile && !moleculeFile->errors().isEmpty())
    debug << QStringLiteral("errors=%1").arg(moleculeFile->errors());
  return debug.join(QStringLiteral("; "));
}

Atom *addBondedTypedAtom(Molecule *molecule, Atom *anchor, unsigned int atomicNumber)
{
  Atom *atom = molecule->addAtom();
  atom->setAtomicNumber(atomicNumber);
  molecule->addBond(anchor, atom, 1);
  return atom;
}

bool writeSmilesAsSdf(const QString &smiles, const QString &title, OBConversion &conv,
                     std::ofstream &ofs)
{
  OBConversion smilesConv;
  if (!smilesConv.SetInFormat("smi"))
    return false;

  OBMol mol;
  std::stringstream input;
  input << smiles.toStdString() << " " << title.toStdString();
  if (!smilesConv.Read(&mol, &input))
    return false;

  std::string titleString = title.toStdString();
  mol.SetTitle(titleString);
  return conv.Write(&mol, &ofs);
}
}

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
#ifdef WIN32
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString obInstallDir = QDir(appDir).absoluteFilePath("../openbabel-install");

  QString babelDataDir = QDir(obInstallDir).absoluteFilePath("share/openbabel");
  QDir dataRoot(babelDataDir);
  const QStringList dataVersions = dataRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                      QDir::Name);
  if (!dataVersions.isEmpty())
    babelDataDir = dataRoot.absoluteFilePath(dataVersions.first());

  QString babelLibDir;
  QDir libRoot(QDir(obInstallDir).absoluteFilePath("lib/openbabel"));
  const QStringList libVersions = libRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                    QDir::Name);
  if (!libVersions.isEmpty()) {
    babelLibDir = libRoot.absoluteFilePath(libVersions.first());
  } else {
    QDir binPluginRoot(QDir(obInstallDir).absoluteFilePath("bin/openbabel"));
    const QStringList binVersions = binPluginRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                            QDir::Name);
    if (!binVersions.isEmpty())
      babelLibDir = binPluginRoot.absoluteFilePath(binVersions.first());
    else
      babelLibDir = QDir(obInstallDir).absoluteFilePath("bin");
  }

  qputenv("BABEL_DATADIR", QDir::toNativeSeparators(babelDataDir).toLocal8Bit());
  qputenv("BABEL_LIBDIR", QDir::toNativeSeparators(babelLibDir).toLocal8Bit());

  qDebug() << "moleculefiletest BABEL_DATADIR" << QDir::toNativeSeparators(babelDataDir);
  qDebug() << "moleculefiletest BABEL_LIBDIR" << QDir::toNativeSeparators(babelLibDir);
#endif
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

  auto readDebugContext = [&filename]() {
    QFileInfo info(filename);
    QStringList debug;
    debug << QString("cwd=%1").arg(QDir::currentPath());
    debug << QString("file=%1 exists=%2 size=%3")
                 .arg(info.absoluteFilePath())
                 .arg(info.exists())
                 .arg(info.exists() ? QString::number(info.size()) : QString("n/a"));

    const QString babelDataDir = QString::fromLocal8Bit(qgetenv("BABEL_DATADIR"));
    const QString babelLibDir = QString::fromLocal8Bit(qgetenv("BABEL_LIBDIR"));
    debug << QString("BABEL_DATADIR=%1").arg(babelDataDir.isEmpty() ? QString("<unset>") : babelDataDir);
    debug << QString("BABEL_LIBDIR=%1").arg(babelLibDir.isEmpty() ? QString("<unset>") : babelLibDir);

    const QStringList pathEntries = QString::fromLocal8Bit(qgetenv("PATH"))
                                      .split(QDir::listSeparator(), Qt::SkipEmptyParts);
    if (!pathEntries.isEmpty())
      debug << QString("PATH[0]=%1").arg(pathEntries.first());

    return debug.join(QStringLiteral("; "));
  };

  OpenBabel::OBMol mol = m_molecule->OBMol();
  OBConversion conv;
  conv.SetOutFormat("sdf");
  std::ofstream ofs(filename.toLatin1().data());
  QVERIFY2(ofs, qPrintable(QString("Failed to open output stream: %1").arg(readDebugContext())));
  // write the molecule 4 times...
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);
  ofs.close();

  MoleculeFile* moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY2(moleculeFile, qPrintable(QString("readFile returned null. %1").arg(readDebugContext())));
  QVERIFY2(moleculeFile->errors().isEmpty(),
           qPrintable(QString("Unexpected read errors: %1 | %2")
                        .arg(moleculeFile->errors(), readDebugContext())));
  QCOMPARE( moleculeFile->isConformerFile(), true );
  QCOMPARE( moleculeFile->numMolecules(), static_cast<unsigned int>(1) );
  QCOMPARE( moleculeFile->conformers().size(),
      static_cast<std::vector<int>::size_type>(4) );


  ofs.open(filename.toLatin1().data());
  QVERIFY2(ofs, qPrintable(QString("Failed to reopen output stream: %1").arg(readDebugContext())));
  // write the molecule 4 times...
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);
  mol.NewAtom();
  conv.Write(&mol, &ofs);
  conv.Write(&mol, &ofs);

  // Close the stream before reopening the file on Windows.
  ofs.close();

  moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY2(moleculeFile, qPrintable(QString("Second readFile returned null. %1").arg(readDebugContext())));
  QVERIFY2(moleculeFile->errors().isEmpty(),
           qPrintable(QString("Unexpected second-read errors: %1 | %2")
                        .arg(moleculeFile->errors(), readDebugContext())));
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
  QString filename = "moleculefiletest_tmp.sdf";
  OBConversion sdfConv;
  QVERIFY2(sdfConv.SetOutFormat("sdf"),
           "replaceMolecule(): failed to configure SDF output format");
  std::ofstream ofs(filename.toLatin1().data());
  QVERIFY2(ofs, "replaceMolecule(): failed to open temporary SDF file");
  QVERIFY2(writeSmilesAsSdf("c1ccccc1", "phenyl", sdfConv, ofs),
           "replaceMolecule(): failed to write phenyl SDF entry");
  QVERIFY2(writeSmilesAsSdf("c1ccccc1N", "aniline", sdfConv, ofs),
           "replaceMolecule(): failed to write aniline SDF entry");
  QVERIFY2(writeSmilesAsSdf("Cc1ccccc1", "toluene", sdfConv, ofs),
           "replaceMolecule(): failed to write toluene SDF entry");
  ofs.close();

  MoleculeFile* moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY2(moleculeFile, "replaceMolecule(): readFile returned null");
  QVERIFY2(moleculeFile->errors().isEmpty(), qPrintable(moleculeFile->errors()));
  QCOMPARE( moleculeFile->isConformerFile(), false );
  QCOMPARE( moleculeFile->numMolecules(), static_cast<unsigned int>(3) );

  // check 1st molecule
  Molecule *phenyl = moleculeFile->molecule(0);
  QVERIFY2(phenyl, qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): phenyl molecule is null", phenyl)));
  QCOMPARE( phenyl->numAtoms(), static_cast<unsigned int>(6) );
  QCOMPARE( countAtomsWithAtomicNumber(phenyl, 6), static_cast<unsigned int>(6) );
  delete phenyl;

  // check 2nd molecule
  Molecule *aniline = moleculeFile->molecule(1);
  QVERIFY2(aniline, qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): aniline molecule is null", aniline)));
  QCOMPARE( aniline->numAtoms(), static_cast<unsigned int>(7) );
  QCOMPARE( countAtomsWithAtomicNumber(aniline, 7), static_cast<unsigned int>(1) );
  delete aniline;

  // check 3rd molecule
  Molecule *toluene = moleculeFile->molecule(2);
  QVERIFY2(toluene, qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): toluene molecule is null", toluene)));
  QCOMPARE( toluene->numAtoms(), static_cast<unsigned int>(7) );
  QCOMPARE( countAtomsWithAtomicNumber(toluene, 6), static_cast<unsigned int>(7) );
  delete toluene;

  // replace 2nd
  aniline = moleculeFile->molecule(1);
  Atom *anilineAnchor = aniline->atom(0);
  QVERIFY2(anilineAnchor, "replaceMolecule(): aniline anchor atom is null");
  addBondedTypedAtom(aniline, anilineAnchor, 6);
  addBondedTypedAtom(aniline, anilineAnchor, 6);
  QVERIFY2(moleculeFile->replaceMolecule(1, aniline, filename),
           qPrintable(QString("replaceMolecule(): replacing aniline failed: %1")
                        .arg(moleculeFile->errors())));
  delete aniline;

  // check again
  aniline = moleculeFile->molecule(1);
  QVERIFY2(aniline,
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): aniline null after replace", aniline)));
  QVERIFY2(aniline->numAtoms() == static_cast<unsigned int>(9),
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): unexpected aniline atom count after replace", aniline)));
  QVERIFY2(countAtomsWithAtomicNumber(aniline, 7) == static_cast<unsigned int>(1),
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): unexpected aniline nitrogen count after replace", aniline)));
  QVERIFY2(countAtomsWithAtomicNumber(aniline, 6) == static_cast<unsigned int>(8),
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): unexpected aniline carbon count after replace", aniline)));
  QCOMPARE( aniline->numAtoms(), static_cast<unsigned int>(9) );
  QCOMPARE( countAtomsWithAtomicNumber(aniline, 7), static_cast<unsigned int>(1) );
  QCOMPARE( countAtomsWithAtomicNumber(aniline, 6), static_cast<unsigned int>(8) );
  delete aniline;
  toluene = moleculeFile->molecule(2);
  QVERIFY2(toluene,
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): toluene null after aniline replace", toluene)));
  QVERIFY2(toluene->numAtoms() == static_cast<unsigned int>(7),
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): unexpected toluene atom count after aniline replace", toluene)));
  QVERIFY2(countAtomsWithAtomicNumber(toluene, 6) == static_cast<unsigned int>(7),
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): unexpected toluene carbon count after aniline replace", toluene)));
  QCOMPARE( toluene->numAtoms(), static_cast<unsigned int>(7) );
  QCOMPARE( countAtomsWithAtomicNumber(toluene, 6), static_cast<unsigned int>(7) );
  delete toluene;

  // replace 1st
  phenyl = moleculeFile->molecule(0);
  Atom *phenylAnchor = phenyl->atom(0);
  QVERIFY2(phenylAnchor, "replaceMolecule(): phenyl anchor atom is null");
  addBondedTypedAtom(phenyl, phenylAnchor, 6);
  addBondedTypedAtom(phenyl, phenylAnchor, 6);
  QVERIFY2(moleculeFile->replaceMolecule(0, phenyl, filename),
           qPrintable(QString("replaceMolecule(): replacing phenyl failed: %1")
                        .arg(moleculeFile->errors())));
  delete phenyl;
  // check again
  phenyl = moleculeFile->molecule(0);
  QVERIFY2(phenyl,
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): phenyl null after replace", phenyl)));
  QVERIFY2(phenyl->numAtoms() == static_cast<unsigned int>(8),
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): unexpected phenyl atom count after replace", phenyl)));
  QVERIFY2(countAtomsWithAtomicNumber(phenyl, 6) == static_cast<unsigned int>(8),
           qPrintable(replaceDebugContext(moleculeFile, "replaceMolecule(): unexpected phenyl carbon count after replace", phenyl)));
  QCOMPARE( phenyl->numAtoms(), static_cast<unsigned int>(8) );
  QCOMPARE( countAtomsWithAtomicNumber(phenyl, 6), static_cast<unsigned int>(8) );
  delete phenyl;
 

  delete moleculeFile;
}

void MoleculeFileTest::appendMolecule()
{
  QString filename = "moleculefiletest_tmp.smi";
  std::ofstream ofs(filename.toLatin1().data());
  ofs << "c1ccccc1  phenyl" << std::endl;
  ofs << "c1ccccc1N  aniline" << std::endl;
  // Use the standard SMILES with the substituent before the ring to avoid
  // Open Babel kekulization warnings.
  ofs << "Cc1ccccc1  toluene" << std::endl;
  ofs.close();

  MoleculeFile* moleculeFile = MoleculeFile::readFile(filename.toLatin1().data());
  QVERIFY2( moleculeFile, "appendMolecule(): readFile returned null" );
  QVERIFY2( moleculeFile->errors().isEmpty(), qPrintable(moleculeFile->errors()) );
  QCOMPARE( moleculeFile->isConformerFile(), false );
  QCOMPARE( moleculeFile->numMolecules(), static_cast<unsigned int>(3) );

  for (unsigned int i = 0; i < moleculeFile->numMolecules(); ++i) {
    Molecule *molecule = moleculeFile->molecule(i);
    QVERIFY2(molecule, qPrintable(QString("appendMolecule(): molecule %1 is null").arg(i)));
    qDebug() << "appendMolecule() entry" << i << moleculeAtomSummary(molecule);
    delete molecule;
  }
}

QTEST_MAIN(MoleculeFileTest)

#include "moc_moleculefiletest.cpp"
