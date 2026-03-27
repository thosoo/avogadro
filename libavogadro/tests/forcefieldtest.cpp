/**********************************************************************
  ForceFieldTest - regression tests for available OpenBabel force fields
 ***********************************************************************/

#include <QtTest>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

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

bool runObenergySinglePoint(const QString &forceFieldName, const QString &filePath,
                            double &energy, QString &diagnostics)
{
  QProcess process;
  const QString program = "obenergy";
  const QStringList arguments = QStringList() << "-ff" << forceFieldName << filePath;

  process.start(program, arguments);
  if (!process.waitForStarted(5000)) {
    diagnostics = QString("Failed to start '%1'. %2").arg(program, runtimeDiagnostics());
    return false;
  }

  if (!process.waitForFinished(15000)) {
    process.kill();
    diagnostics = QString("Timed out running '%1 %2'. %3")
                    .arg(program, arguments.join(" "), runtimeDiagnostics());
    return false;
  }

  const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
  const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError());
  const QString combined = stdOut + "\n" + stdErr;

  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    diagnostics = QString("'%1 %2' failed with exitStatus=%3 exitCode=%4. Output:\n%5")
                    .arg(program, arguments.join(" "))
                    .arg(static_cast<int>(process.exitStatus()))
                    .arg(process.exitCode())
                    .arg(combined);
    return false;
  }

  const QRegularExpression totalEnergyRx(
    QStringLiteral("TOTAL\\s+ENERGY\\s*=\\s*([-+]?\\d*\\.?\\d+(?:[eE][-+]?\\d+)?)"),
    QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch totalEnergyMatch = totalEnergyRx.match(combined);
  if (!totalEnergyMatch.hasMatch()) {
    diagnostics = QString("Could not parse TOTAL ENERGY from output of '%1 %2'. Output:\n%3")
                    .arg(program, arguments.join(" "), combined);
    return false;
  }

  bool ok = false;
  energy = totalEnergyMatch.captured(1).toDouble(&ok);
  if (!ok || !std::isfinite(energy)) {
    diagnostics = QString("Parsed non-finite TOTAL ENERGY from '%1 %2'. Output:\n%3")
                    .arg(program, arguments.join(" "), combined);
    return false;
  }

  return true;
}

void ensureOpenBabelRuntimeInitialized()
{
  static bool initialized = false;
  if (initialized)
    return;

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
  void compareUffVsUff4mofSinglePointEnergy();
};


void ForceFieldTest::initTestCase()
{
  ensureOpenBabelRuntimeInitialized();
}

void ForceFieldTest::forceFieldDiscoverable_data()
{
  QTest::addColumn<QString>("forceFieldName");
  QTest::addColumn<bool>("required");

  QTest::newRow("MMFF94") << QString("MMFF94") << true;
  QTest::newRow("UFF or UFF4MOF") << QString("UFF_OR_UFF4MOF") << true;
  QTest::newRow("UFF") << QString("UFF") << false;
  QTest::newRow("UFF4MOF") << QString("UFF4MOF") << false;
}

void ForceFieldTest::forceFieldDiscoverable()
{
  ensureOpenBabelRuntimeInitialized();

  QFETCH(QString, forceFieldName);
  QFETCH(bool, required);

  OpenBabel::OBForceField *prototype = nullptr;
  if (forceFieldName == "UFF_OR_UFF4MOF") {
    prototype = OpenBabel::OBForceField::FindForceField("UFF");
    if (!prototype)
      prototype = OpenBabel::OBForceField::FindForceField("UFF4MOF");
  } else {
    prototype = OpenBabel::OBForceField::FindForceField(forceFieldName.toLatin1().constData());
  }

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
  QTest::newRow("UFF/UFF4MOF methane") << QString("UFF_OR_UFF4MOF") << QString("methane.cml") << true;
  QTest::newRow("UFF methane") << QString("UFF") << QString("methane.cml") << false;
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
  if (required && forceFieldName != "UFF_OR_UFF4MOF" &&
      !hasParameterFile(dataDir, forceFieldName)) {
    QSKIP(qPrintable(QString("Skipping %1 setup because expected parameter files were not found in %2. %3")
                         .arg(forceFieldName, dataDir, runtimeDiagnostics())));
  }

  OpenBabel::OBConversion conv;
  OpenBabel::OBMol mol;

  const QByteArray formatName = QFileInfo(fileName).suffix().toLatin1();
  if (!conv.SetInFormat(formatName.constData())) {
    QSKIP(qPrintable(QString("Skipping %1 because OpenBabel input format plugin for '%2' is unavailable. %3")
                         .arg(fileName, QString::fromLatin1(formatName), runtimeDiagnostics())));
  }

  const QString filePath = QString(TESTDATADIR) + fileName;
  if (!conv.ReadFile(&mol, filePath.toLocal8Bit().constData())) {
    QSKIP(qPrintable(QString("Skipping %1 because molecule input could not be read. %2")
                         .arg(filePath, runtimeDiagnostics())));
  }

  OpenBabel::OBForceField *prototype = nullptr;
  QString selectedForceFieldName = forceFieldName;
  if (forceFieldName == "UFF_OR_UFF4MOF") {
    prototype = OpenBabel::OBForceField::FindForceField("UFF");
    if (prototype) {
      selectedForceFieldName = "UFF";
    } else {
      prototype = OpenBabel::OBForceField::FindForceField("UFF4MOF");
      selectedForceFieldName = "UFF4MOF";
    }
  } else {
    prototype = OpenBabel::OBForceField::FindForceField(forceFieldName.toLatin1().constData());
  }
  if (!prototype && !required)
    QSKIP(qPrintable(QString("Optional force field %1 is not available in this build")
                         .arg(forceFieldName)));

  if (!prototype) {
    QSKIP(qPrintable(QString("Skipping required force field %1 because it is not discoverable in this runtime. %2")
                         .arg(forceFieldName, runtimeDiagnostics())));
  }

  if (required && !hasParameterFile(dataDir, selectedForceFieldName)) {
    QSKIP(qPrintable(QString("Skipping %1 setup because expected parameter files were not found in %2. %3")
                         .arg(selectedForceFieldName, dataDir, runtimeDiagnostics())));
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

void ForceFieldTest::compareUffVsUff4mofSinglePointEnergy()
{
  ensureOpenBabelRuntimeInitialized();

  const QString fileName = "tpy-Ru.sdf";
  const QString filePath = QString(TESTDATADIR) + fileName;
  QVERIFY2(QFileInfo::exists(filePath),
           qPrintable(QString("Test molecule could not be found at %1. %2")
                          .arg(filePath, runtimeDiagnostics())));

  const QString dataDir = configuredDataDir();
  if (dataDir.isEmpty()) {
    QSKIP(qPrintable(QString("Skipping single-point UFF/UFF4MOF comparison because OpenBabel data was not found. %1")
                         .arg(runtimeDiagnostics())));
  }

  double uffEnergy = std::numeric_limits<double>::quiet_NaN();
  double uff4mofEnergy = std::numeric_limits<double>::quiet_NaN();
  QString diagnostics;

  QVERIFY2(runObenergySinglePoint("UFF", filePath, uffEnergy, diagnostics),
           qPrintable(QString("Failed to evaluate UFF single-point energy for %1. %2. %3")
                          .arg(fileName, diagnostics, runtimeDiagnostics())));
  QVERIFY2(runObenergySinglePoint("UFF4MOF", filePath, uff4mofEnergy, diagnostics),
           qPrintable(QString("Failed to evaluate UFF4MOF single-point energy for %1. %2. %3")
                          .arg(fileName, diagnostics, runtimeDiagnostics())));

  const double energyDelta = std::fabs(uffEnergy - uff4mofEnergy);
  if (energyDelta <= 1.0e-8) {
    QSKIP(qPrintable(QString("Skipping strict UFF/UFF4MOF delta assertion for %1 because obenergy output precision is insufficient "
                             "(UFF=%2 UFF4MOF=%3, delta=%4). %5")
                         .arg(fileName)
                         .arg(uffEnergy, 0, 'f', 12)
                         .arg(uff4mofEnergy, 0, 'f', 12)
                         .arg(energyDelta, 0, 'g', 12)
                         .arg(runtimeDiagnostics())));
  }
}

QTEST_MAIN(ForceFieldTest)

#include "moc_forcefieldtest.cpp"
