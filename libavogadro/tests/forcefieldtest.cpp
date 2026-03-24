/**********************************************************************
  ForceFieldTest - regression tests for available OpenBabel force fields
 ***********************************************************************/

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>

#include <QDir>
#include <QFileInfo>

#include <openbabel/obconversion.h>
#include <openbabel/forcefield.h>

namespace {

QString configuredDataDir()
{
  const QString appDir = QCoreApplication::applicationDirPath();
  const QStringList candidates = QStringList()
    << QString::fromLocal8Bit(qgetenv("BABEL_DATADIR"))
    << QDir::current().filePath("openbabel-install/share/openbabel")
    << QDir::current().filePath("openbabel_ext-prefix/src/openbabel_ext/data")
    << QDir(appDir).filePath("../openbabel-install/share/openbabel")
    << QDir(appDir).filePath("../../openbabel-install/share/openbabel")
    << QDir(appDir).filePath("../../../openbabel-install/share/openbabel")
    << QDir(appDir).filePath("../openbabel_ext-prefix/src/openbabel_ext/data")
    << QDir(appDir).filePath("../../openbabel_ext-prefix/src/openbabel_ext/data");

  for (const QString &candidate : candidates) {
    if (candidate.isEmpty())
      continue;

    QDir dir(candidate);
    if (!dir.exists())
      continue;

    if (QFileInfo::exists(dir.filePath("UFF.prm")) ||
        !dir.entryList(QStringList() << "*.prm", QDir::Files).isEmpty()) {
      const QString selected = dir.absolutePath();
      qputenv("BABEL_DATADIR", selected.toLocal8Bit());
      return selected;
    }
  }

  return QString();
}

bool shouldSkipSetupFailure(const std::string &log)
{
  return log.find("Cannot open") != std::string::npos ||
         log.find("Unable to open") != std::string::npos ||
         log.find("data file") != std::string::npos;
}

} // namespace

class ForceFieldTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void forceFieldDiscoverable_data();
  void forceFieldDiscoverable();
  void forceFieldSetupAndEnergy_data();
  void forceFieldSetupAndEnergy();
};

void ForceFieldTest::forceFieldDiscoverable_data()
{
  QTest::addColumn<QString>("forceFieldName");

  QTest::newRow("MMFF94") << QString("MMFF94");
  QTest::newRow("UFF") << QString("UFF");
  QTest::newRow("UFF4MOF") << QString("UFF4MOF");
}

void ForceFieldTest::forceFieldDiscoverable()
{
  QFETCH(QString, forceFieldName);

  OpenBabel::OBConversion conv;
  Q_UNUSED(conv);

  OpenBabel::OBForceField *prototype =
    OpenBabel::OBForceField::FindForceField(forceFieldName.toLatin1().constData());

  QVERIFY2(prototype,
           qPrintable(QString("Could not find force field %1")
                          .arg(forceFieldName)));
}

void ForceFieldTest::forceFieldSetupAndEnergy_data()
{
  QTest::addColumn<QString>("forceFieldName");
  QTest::addColumn<QString>("fileName");

  QTest::newRow("MMFF94 methane") << QString("MMFF94") << QString("methane.cml");
  QTest::newRow("UFF methane") << QString("UFF") << QString("methane.cml");
  QTest::newRow("UFF4MOF ruthenium") << QString("UFF4MOF") << QString("tpy-Ru.sdf");
}

void ForceFieldTest::forceFieldSetupAndEnergy()
{
  QFETCH(QString, forceFieldName);
  QFETCH(QString, fileName);

  const QString dataDir = configuredDataDir();
  if (dataDir.isEmpty()) {
    QSKIP("Skipping setup/energy checks because OpenBabel parameter data was not found.");
  }

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

  std::ostringstream log;
  forceField->SetLogFile(&log);
  forceField->SetLogLevel(OBFF_LOGLVL_HIGH);

  if (!forceField->Setup(mol)) {
    const std::string setupLog = log.str();
    if (shouldSkipSetupFailure(setupLog)) {
      QSKIP(qPrintable(QString("Skipping %1 setup due to missing OpenBabel data (%2)")
                           .arg(forceFieldName, QString::fromStdString(setupLog))));
    }

    QVERIFY2(false,
             qPrintable(QString("Could not set up %1 for %2")
                            .arg(forceFieldName, fileName)));
  }

  const double energy = forceField->Energy(false);
  QVERIFY2(std::isfinite(energy),
           qPrintable(QString("Energy from %1 for %2 was not finite")
                          .arg(forceFieldName, fileName)));
}

QTEST_MAIN(ForceFieldTest)

#include "moc_forcefieldtest.cpp"
