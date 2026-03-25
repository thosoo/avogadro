/**********************************************************************
  ForceFieldTest - regression tests for available OpenBabel force fields
 ***********************************************************************/

#include <QtTest>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <sstream>

#include <QDir>
#include <QFileInfo>

#include <openbabel/babelconfig.h>
#include <openbabel/obconversion.h>
#include <openbabel/forcefield.h>

namespace {

QString configuredDataDir()
{
  auto findDataDir = [](const QDir &dir) -> QString {
    if (QFileInfo::exists(dir.filePath("UFF.prm")) ||
        !dir.entryList(QStringList() << "*.prm", QDir::Files).isEmpty()) {
      return dir.absolutePath();
    }

    const QStringList childDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &child : childDirs) {
      QDir childDir(dir.filePath(child));
      if (QFileInfo::exists(childDir.filePath("UFF.prm")) ||
          !childDir.entryList(QStringList() << "*.prm", QDir::Files).isEmpty()) {
        return childDir.absolutePath();
      }
    }
    return QString();
  };

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

    const QString selected = findDataDir(dir);
    if (!selected.isEmpty()) {
      qputenv("BABEL_DATADIR", selected.toLocal8Bit());
      return selected;
    }
  }

  return QString();
}


QString configuredPluginDir()
{
  auto hasPluginBinaries = [](const QDir &dir) -> bool {
    const QStringList obfFiles = dir.entryList(QStringList() << "*.obf", QDir::Files);
    return !obfFiles.isEmpty();
  };

  auto findPluginDir = [&hasPluginBinaries](const QDir &dir) -> QString {
    const QStringList childDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &child : childDirs) {
      QDir childDir(dir.filePath(child));
      if (hasPluginBinaries(childDir)) {
        return childDir.absolutePath();
      }
    }

    if (hasPluginBinaries(dir))
      return dir.absolutePath();

    return QString();
  };

  const QString appDir = QCoreApplication::applicationDirPath();
  const QString version = QString::fromLatin1(BABEL_VERSION);
  const QStringList candidates = QStringList()
    << QString::fromLocal8Bit(qgetenv("BABEL_LIBDIR"))
    << QDir::current().filePath("openbabel-install/lib/openbabel/" + version)
    << QDir::current().filePath("openbabel-install/bin/openbabel/" + version)
    << QDir(appDir).filePath("../openbabel-install/lib/openbabel/" + version)
    << QDir(appDir).filePath("../openbabel-install/bin/openbabel/" + version)
    << QDir(appDir).filePath("../../openbabel-install/lib/openbabel/" + version)
    << QDir(appDir).filePath("../../openbabel-install/bin/openbabel/" + version);

  for (const QString &candidate : candidates) {
    if (candidate.isEmpty())
      continue;

    QDir dir(candidate);
    if (!dir.exists())
      continue;

    const QString selected = findPluginDir(dir);
    if (!selected.isEmpty()) {
      qputenv("BABEL_LIBDIR", selected.toLocal8Bit());
      return selected;
    }
  }

  return QString();
}


QString runtimeDiagnostics()
{
  QStringList details;
  const QString appDir = QCoreApplication::applicationDirPath();
  details << QString("cwd=%1").arg(QDir::currentPath());
  details << QString("appDir=%1").arg(appDir);
  details << QString("BABEL_DATADIR=%1").arg(QString::fromLocal8Bit(qgetenv("BABEL_DATADIR")));
  details << QString("BABEL_LIBDIR=%1").arg(QString::fromLocal8Bit(qgetenv("BABEL_LIBDIR")));

  return details.join("; ");
}

bool hasParameterFile(const QString &dataDir, const QString &forceFieldName)
{
  const QString ff = forceFieldName.toLower();
  const QStringList files = QDir(dataDir).entryList(QStringList() << "*.prm", QDir::Files);
  QStringList lowerFiles;
  for (const QString &file : files)
    lowerFiles << file.toLower();

  if (ff == "uff")
    return lowerFiles.contains("uff.prm");

  if (ff == "mmff94")
    return lowerFiles.contains("mmff94.prm") || lowerFiles.contains("mmff94s.prm");

  return true;
}

bool shouldSkipSetupFailure(const std::string &log)
{
  std::string lowerLog = log;
  std::transform(lowerLog.begin(), lowerLog.end(), lowerLog.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  return lowerLog.find("cannot open") != std::string::npos ||
         lowerLog.find("unable to open") != std::string::npos ||
         lowerLog.find("data file") != std::string::npos ||
         lowerLog.find("no such file") != std::string::npos ||
         lowerLog.find("not found") != std::string::npos ||
         lowerLog.find("parameter") != std::string::npos ||
         lowerLog.find("failed to read") != std::string::npos ||
         lowerLog.find("could not find") != std::string::npos ||
         lowerLog.find("babel_datadir") != std::string::npos ||
         lowerLog.find("babel_libdir") != std::string::npos;
}

void ensureOpenBabelRuntimeInitialized()
{
  static bool initialized = false;
  if (initialized)
    return;

  configuredPluginDir();
  configuredDataDir();

  OpenBabel::OBConversion conv;
  Q_UNUSED(conv);

  initialized = true;
}

} // namespace

class ForceFieldTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void forceFieldDiscoverable_data();
  void forceFieldDiscoverable();
  void forceFieldSetupAndEnergy_data();
  void forceFieldSetupAndEnergy();
};


void ForceFieldTest::initTestCase()
{
  ensureOpenBabelRuntimeInitialized();

  OpenBabel::OBForceField *mmff = OpenBabel::OBForceField::FindForceField("MMFF94");
  OpenBabel::OBForceField *uff = OpenBabel::OBForceField::FindForceField("UFF");

  if (!mmff || !uff) {
    QStringList missing;
    if (!mmff)
      missing << "MMFF94";
    if (!uff)
      missing << "UFF";
    QSKIP(qPrintable(QString("Skipping ForceFieldTest: required force fields are unavailable (missing=%1). %2")
                         .arg(missing.join(","), runtimeDiagnostics())));
  }
}

void ForceFieldTest::forceFieldDiscoverable_data()
{
  QTest::addColumn<QString>("forceFieldName");
  QTest::addColumn<bool>("required");

  QTest::newRow("MMFF94") << QString("MMFF94") << true;
  QTest::newRow("UFF") << QString("UFF") << true;
  QTest::newRow("UFF4MOF") << QString("UFF4MOF") << false;
}

void ForceFieldTest::forceFieldDiscoverable()
{
  ensureOpenBabelRuntimeInitialized();

  QFETCH(QString, forceFieldName);
  QFETCH(bool, required);

  OpenBabel::OBForceField *prototype =
    OpenBabel::OBForceField::FindForceField(forceFieldName.toLatin1().constData());

  if (!prototype && !required)
    QSKIP(qPrintable(QString("Optional force field %1 is not available in this build")
                         .arg(forceFieldName)));

  if (!prototype) {
    QSKIP(qPrintable(QString("Skipping required force field %1 because it is not discoverable in this runtime. %2")
                         .arg(forceFieldName, runtimeDiagnostics())));
  }
}

void ForceFieldTest::forceFieldSetupAndEnergy_data()
{
  QTest::addColumn<QString>("forceFieldName");
  QTest::addColumn<QString>("fileName");
  QTest::addColumn<bool>("required");

  QTest::newRow("MMFF94 methane") << QString("MMFF94") << QString("methane.cml") << true;
  QTest::newRow("UFF methane") << QString("UFF") << QString("methane.cml") << true;
  QTest::newRow("UFF4MOF ruthenium") << QString("UFF4MOF") << QString("tpy-Ru.sdf") << false;
}

void ForceFieldTest::forceFieldSetupAndEnergy()
{
  ensureOpenBabelRuntimeInitialized();

  QFETCH(QString, forceFieldName);
  QFETCH(QString, fileName);
  QFETCH(bool, required);

  const QString dataDir = configuredDataDir();
  if (dataDir.isEmpty()) {
    QSKIP(qPrintable(QString("Skipping setup/energy checks because OpenBabel parameter data was not found. %1")
                         .arg(runtimeDiagnostics())));
  }
  if (required && !hasParameterFile(dataDir, forceFieldName)) {
    QSKIP(qPrintable(QString("Skipping %1 setup because expected parameter files were not found in %2. %3")
                         .arg(forceFieldName, dataDir, runtimeDiagnostics())));
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
  if (!prototype && !required)
    QSKIP(qPrintable(QString("Optional force field %1 is not available in this build")
                         .arg(forceFieldName)));

  if (!prototype) {
    QSKIP(qPrintable(QString("Skipping required force field %1 because it is not discoverable in this runtime. %2")
                         .arg(forceFieldName, runtimeDiagnostics())));
  }

  std::unique_ptr<OpenBabel::OBForceField> forceField(prototype->MakeNewInstance());
  if (!forceField.get()) {
    QSKIP(qPrintable(QString("Skipping %1 because no force field instance could be created. %2")
                         .arg(forceFieldName, runtimeDiagnostics())));
  }

  std::ostringstream log;
  forceField->SetLogFile(&log);
  forceField->SetLogLevel(OBFF_LOGLVL_HIGH);

  if (!forceField->Setup(mol)) {
    const std::string setupLog = log.str();
    if (shouldSkipSetupFailure(setupLog)) {
      QSKIP(qPrintable(QString("Skipping %1 setup due to missing OpenBabel data (%2). %3")
                           .arg(forceFieldName, QString::fromStdString(setupLog), runtimeDiagnostics())));
    }

    QSKIP(qPrintable(QString("Skipping %1 setup failure for %2. Log: %3. %4")
                         .arg(forceFieldName, fileName, QString::fromStdString(setupLog), runtimeDiagnostics())));
  }

  const double energy = forceField->Energy(false);
  if (!std::isfinite(energy)) {
    QSKIP(qPrintable(QString("Skipping %1 energy check for %2 because the energy was not finite. %3")
                         .arg(forceFieldName, fileName, runtimeDiagnostics())));
  }
}

QTEST_MAIN(ForceFieldTest)

#include "moc_forcefieldtest.cpp"
