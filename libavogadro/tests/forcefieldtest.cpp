/**********************************************************************
  ForceFieldTest - regression tests for available OpenBabel force fields
 ***********************************************************************/

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>

#include <QDir>
#include <QFileInfo>

#include <openbabel/obconversion.h>
#include <openbabel/forcefield.h>
#include <openbabel/plugin.h>

class ForceFieldTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void forceFieldIsListed_data();
  void forceFieldIsListed();
  void forceFieldSetup_data();
  void forceFieldSetup();
};


void ForceFieldTest::initTestCase()
{
  const QString appDir = QCoreApplication::applicationDirPath();
  // OpenBabel appends BABEL_VERSION when BABEL_DATADIR is set, so point it
  // at the parent openbabel data directory rather than the versioned child.
  const QStringList dataCandidates = QStringList()
    << QDir(appDir).filePath("../openbabel-install/share/openbabel")
    << QDir::current().filePath("openbabel-install/share/openbabel");

  for (const QString &candidate : dataCandidates) {
    if (QFileInfo::exists(candidate)) {
      qputenv("BABEL_DATADIR", QDir(candidate).absolutePath().toLocal8Bit());
      break;
    }
  }
}

void ForceFieldTest::forceFieldIsListed_data()
{
  QTest::addColumn<QString>("forceFieldName");

  QTest::newRow("MMFF94") << QString("MMFF94");
  QTest::newRow("UFF") << QString("UFF");
  QTest::newRow("UFF4MOF") << QString("UFF4MOF");
}

void ForceFieldTest::forceFieldIsListed()
{
  QFETCH(QString, forceFieldName);

  OpenBabel::OBConversion conv;
  Q_UNUSED(conv);

  std::vector<std::string> forceFields;
  OpenBabel::OBPlugin::ListAsVector("forcefields", "ids", forceFields);

  QVERIFY2(std::find(forceFields.begin(), forceFields.end(),
                     forceFieldName.toStdString()) != forceFields.end(),
           qPrintable(QString("Force field %1 was not listed by OpenBabel")
                          .arg(forceFieldName)));
}

void ForceFieldTest::forceFieldSetup_data()
{
  QTest::addColumn<QString>("forceFieldName");
  QTest::addColumn<QString>("fileName");

  QTest::newRow("MMFF94 methane") << QString("MMFF94") << QString("methane.cml");
  QTest::newRow("UFF methane") << QString("UFF") << QString("methane.cml");
  QTest::newRow("UFF4MOF tpy-Ru") << QString("UFF4MOF") << QString("tpy-Ru.sdf");
}

void ForceFieldTest::forceFieldSetup()
{
  QFETCH(QString, forceFieldName);
  QFETCH(QString, fileName);

  OpenBabel::OBConversion conv;
  OpenBabel::OBMol mol;

  const QByteArray formatName = QFileInfo(fileName).suffix().toLatin1();
  QVERIFY2(conv.SetInFormat(formatName.constData()),
           qPrintable(QString("Could not determine input format for %1")
                          .arg(fileName)));

  const QString filePath = QString(TESTDATADIR) + fileName;
  QVERIFY2(conv.ReadFile(&mol, filePath.toLocal8Bit().constData()),
           qPrintable(QString("Could not read molecule from %1")
                          .arg(filePath)));

  OpenBabel::OBForceField *prototype =
    OpenBabel::OBForceField::FindForceField(forceFieldName.toLatin1().constData());
  QVERIFY2(prototype,
           qPrintable(QString("Could not find force field %1")
                          .arg(forceFieldName)));

  std::unique_ptr<OpenBabel::OBForceField> forceField(prototype->MakeNewInstance());
  QVERIFY2(forceField.get(),
           qPrintable(QString("Could not create force field instance for %1")
                          .arg(forceFieldName)));

  QVERIFY2(forceField->Setup(mol),
           qPrintable(QString("Could not set up %1 for %2")
                          .arg(forceFieldName, fileName)));

  const double energy = forceField->Energy(false);
  QVERIFY2(std::isfinite(energy),
           qPrintable(QString("Energy from %1 for %2 was not finite")
                          .arg(forceFieldName, fileName)));
}

QTEST_MAIN(ForceFieldTest)

#include "moc_forcefieldtest.cpp"
