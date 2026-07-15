/**********************************************************************
  main.cpp - main program, initialization and launching

  Copyright (C) 2006-2009 by Geoffrey R. Hutchison
  Copyright (C) 2006-2008 by Donald Ephraim Curtis
  Copyright (C) 2008-2009 by Marcus D. Hanwell

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

#include <avogadro/global.h>
#include <openbabel/babelconfig.h>
#include <openbabel/obconversion.h>

#ifdef ENABLE_GLSL
  #include <GL/glew.h>
#endif

// Qt Includes
#include <QApplication>
#include <QMessageBox>
#include <QTranslator>
#include <QSurfaceFormat>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QDebug>
#include <QLibraryInfo>
#include <QProcess>
#include <QFont>
#include <QFileInfo>
#include <QFile>
#include <QDir>

#include <iostream>

// get the SVN revision string
#include "config.h" // krazy:exclude=includes

// Avogadro Includes
#include "mainwindow.h"
#include "application.h"

#ifdef AVO_USE_X11
  #include <X11/Xlib.h>
#endif

#ifdef WIN32
  #include <windows.h>
  #include <stdlib.h>
#endif

#ifdef AVO_APP_BUNDLE
  #include <cstdlib>
#endif

using namespace Avogadro;


namespace {

QString firstExistingDirectory(const QStringList &candidates,
                               const QString &requiredFile = QString())
{
  foreach (const QString &candidate, candidates) {
    QDir dir(candidate);
    if (!dir.exists())
      continue;
    if (!requiredFile.isEmpty() &&
        !QFileInfo::exists(dir.filePath(requiredFile)))
      continue;
    return dir.absolutePath();
  }
  return QString();
}

void setEnvironmentIfUnset(const char *name, const QString &value)
{
  if (value.isEmpty() || !qEnvironmentVariableIsEmpty(name))
    return;
  qputenv(name, QFile::encodeName(value));
}

void configureBundledOpenBabelRuntime()
{
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString version = QString(BABEL_VERSION);

#ifdef WIN32
  const QString dataDir = firstExistingDirectory(
    QStringList()
      << QDir(appDir).filePath(QStringLiteral("data"))
      << QDir(appDir).filePath(QStringLiteral("../share/openbabel/%1")).arg(version),
    QStringLiteral("space-groups.txt"));
  const QString pluginDir = firstExistingDirectory(
    QStringList()
      << appDir
      << QDir(appDir).filePath(QStringLiteral("../lib/openbabel/%1")).arg(version));
#else
  const QString dataDir = firstExistingDirectory(
    QStringList()
      << QDir(appDir).filePath(QStringLiteral("../share/openbabel/%1")).arg(version),
    QStringLiteral("space-groups.txt"));
  const QString pluginDir = firstExistingDirectory(
    QStringList()
      << QDir(appDir).filePath(QStringLiteral("../lib/openbabel/%1")).arg(version));
#endif

  setEnvironmentIfUnset("BABEL_DATADIR", dataDir);
  setEnvironmentIfUnset("BABEL_LIBDIR", pluginDir);

  if (qEnvironmentVariableIsSet("AVOGADRO_THERMAL_ELLIPSOID_DEBUG")) {
    const QString effectiveDataDir = QString::fromLocal8Bit(qgetenv("BABEL_DATADIR"));
    const QString spaceGroupsPath =
      QDir(effectiveDataDir).filePath(QStringLiteral("space-groups.txt"));
    const QString effectivePluginDir = QString::fromLocal8Bit(qgetenv("BABEL_LIBDIR"));
    OpenBabel::OBConversion conversion;
    const bool cifAvailable = conversion.SetInFormat("cif");

    qDebug() << "[ThermalEllipsoid/OpenBabel] BABEL_DATADIR=" << effectiveDataDir;
    qDebug() << "[ThermalEllipsoid/OpenBabel] space-groups.txt=" << spaceGroupsPath;
    qDebug() << "[ThermalEllipsoid/OpenBabel] space-groups exists="
             << QFileInfo::exists(spaceGroupsPath);
    qDebug() << "[ThermalEllipsoid/OpenBabel] BABEL_LIBDIR=" << effectivePluginDir;
    qDebug() << "[ThermalEllipsoid/OpenBabel] CIF format available=" << cifAvailable;
  }
}

} // namespace

void printVersion(const QString &appName);
void printHelp(const QString &appName);

int main(int argc, char *argv[])
{
#ifdef AVO_USE_X11
  if(Library::threadedGL()) {
    std::cout << "Enabling Threads" << std::endl;
    XInitThreads();
  }
#endif



  // Check for --disable-hidpi-scaling flag before setting high-DPI attributes
  bool disableHiDpi = false;
  for (int i = 1; i < argc; ++i) {
    if (QString(argv[i]) == "--disable-hidpi-scaling") {
      disableHiDpi = true;
      break;
    }
  }
  if (!disableHiDpi) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
  }

  // set up groups for QSettings
  QCoreApplication::setOrganizationName("SourceForge");
  QCoreApplication::setOrganizationDomain("sourceforge.net");
  QCoreApplication::setApplicationName("Avogadro");

  Application app(argc, argv);
#ifdef WIN32
  // Ensure we load DLLs from the executable directory first so
  // avogadro.dll is found regardless of the current working directory.
  SetDllDirectoryW(reinterpret_cast<LPCWSTR>(QCoreApplication::applicationDirPath().utf16()));
  // Prepend the application directory to PATH so bundled DLLs are used
  QByteArray pathEnv = qgetenv("PATH");
  QString binDir = QCoreApplication::applicationDirPath();
  QString newPath = binDir + QLatin1Char(';') + QString::fromLocal8Bit(pathEnv);
  QString babelPluginDir = binDir + "/../lib/openbabel/" + QString(BABEL_VERSION);
  if (QFileInfo::exists(babelPluginDir)) {
    newPath = babelPluginDir + QLatin1Char(';') + newPath;
  }
  _putenv_s("PATH", newPath.toLocal8Bit().constData());

#endif

  // Output the untranslated application and library version - bug reports
  QString versionInfo = "Avogadro version:\t" + QString(VERSION) + "\tGit:\t"
                        + QString(SCM_REVISION) + "\nLibAvogadro version:\t"
                        + Library::version() + "\tGit:\t" + Library::scmRevision();
  qDebug() << versionInfo;

  configureBundledOpenBabelRuntime();

#ifdef AVO_APP_BUNDLE
  // Override the Qt plugin search path too
  QStringList pluginSearchPaths;
  pluginSearchPaths << QCoreApplication::applicationDirPath() + "/../plugins";
  QCoreApplication::setLibraryPaths(pluginSearchPaths);
#endif

  // Before we do much else, load translations
  // This ensures help messages and debugging info will be translated
  QStringList translationPaths;

  foreach (const QString &variable, QProcess::systemEnvironment()) {
    QStringList split1 = variable.split('=');
    if (split1[0] == "AVOGADRO_TRANSLATIONS") {
      foreach (const QString &path, split1[1].split(':'))
        translationPaths << path;
    }
  }

  translationPaths << QCoreApplication::applicationDirPath() + "/../share/avogadro/i18n/";
#ifdef Q_OS_MAC
  translationPaths << QString(INSTALL_PREFIX) + "/share/avogadro/i18n/";
#endif

  // Get the locale for translations
  QString translationCode = QLocale::system().name();

  // The QLocale::system() call on macOS returns the default locale formatting,
  // which may not reflect the preferred language. Use QLocale again to ensure
  // we honor system preferences without relying on the private QSystemLocale.
#ifdef Q_OS_MAC
  translationCode = QLocale::system().name();
#endif

  qDebug() << "Locale: " << translationCode;

  // As suggested by iwao aoyama to make sure Windows opens files with kanji characters
#ifdef WIN32
  QString lang = QLocale::languageToString(QLocale::system().language());
  std::locale::global(std::locale(lang.toLocal8Bit().constData()));
#endif

  // Load Qt translations first
  bool tryLoadingQtTranslations = false;
  QString qtFilename = "qt_" + translationCode + ".qm";
  QTranslator qtTranslator(0);
  if (qtTranslator.load(qtFilename, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    app.installTranslator(&qtTranslator);
  else
    tryLoadingQtTranslations = true;

  // Load the libavogadro translations
  QPointer <QTranslator> libTranslator = Library::createTranslator();
  if (libTranslator)
    app.installTranslator(libTranslator);

  // Load the Avogadro translations
  QTranslator avoTranslator(0);
  QString avoFilename = "avogadro_" + translationCode + ".qm";

  foreach (const QString &translationPath, translationPaths) {
    // We can't find the normal Qt translations (maybe we're in a "bundle"?)
    if (tryLoadingQtTranslations) {
      if (qtTranslator.load(qtFilename, translationPath)) {
        app.installTranslator(&qtTranslator);
        tryLoadingQtTranslations = false; // already loaded
      }
    }

    if (avoTranslator.load(avoFilename, translationPath)) {
      app.installTranslator(&avoTranslator);
      qDebug() << "Translation successfully loaded.";
    }
  }

  // Check if we just need a version or help message
  QStringList arguments = app.arguments();
  if(arguments.contains("-v") || arguments.contains("--version")) {
    printVersion(arguments[0]);
    return 0;
  }
  else if(arguments.contains("-h") || arguments.contains("-help")
    || arguments.contains("--help")) {
    printHelp(arguments[0]);
    return 0;
  }

  QOpenGLContext testContext;
  if (!testContext.create()) {
    QMessageBox::information(0, "Avogadro", "This system does not support OpenGL.");
    return -1;
  }

  QSurfaceFormat defFormat = QSurfaceFormat::defaultFormat();
  defFormat.setSamples(4);
  QSurfaceFormat::setDefaultFormat(defFormat);

  // Test what capabilities we have
  //qDebug() << /*QCoreApplication::translate("main.cpp", */"OpenGL capabilities found: "/*)*/;
  std::cout << "OpenGL capabilities found: " << std::endl;
  if (defFormat.swapBehavior() != QSurfaceFormat::SingleBuffer)
    std::cout << "\t" << "Double Buffering." << std::endl;
  if (defFormat.renderableType() == QSurfaceFormat::OpenGL)
    std::cout << "\t" << "Direct Rendering." << std::endl;
  if (defFormat.samples() > 0)
    std::cout << "\t" << "Antialiasing." << std::endl;

  // Now load any files supplied on the command-line or via launching a file.
  // Additionally, process and remove any command line arguments.
  MainWindow *window = new MainWindow();
  if (arguments.size() > 1) {
    QPoint p(100, 100), offset(40,40);
    QList<QString>::const_iterator i = arguments.constBegin();
    for (++i; i != arguments.constEnd(); ++i) {
      if (i->startsWith("--erase-config")) {
        window->setIgnoreConfig(true);
      }
      else {
        window->openFile(*i);
        // this costs us a few more function calls
        // but makes our loading look nicer
        window->show();
        app.processEvents();
      }
    }
  }
  window->show();
  return app.exec();
}

void printVersion(const QString &)
{
  #ifdef WIN32
  std::cout << "Avogadro: " << VERSION << std::endl;
  std::cout << "Qt: \t\t" << qVersion() << std::endl;
  #else
  std::wcout << QCoreApplication::translate("main.cpp", "Avogadro: \t%1 (Hash %2)\n"
      "LibAvogadro: \t%3 (Hash %4)\n"
      "Qt: \t\t%5\n").arg(VERSION, SCM_REVISION, Library::version(), Library::scmRevision(), qVersion()).toStdWString();
  std::wcout << "OpenBabel: \t" << BABEL_VERSION << std::endl;
  #endif
}

void printHelp(const QString &appName)
{
  #ifdef WIN32
  std::cout << "Usage: avogadro [options] [files]" << std::endl << std::endl;
  std::cout << "Avogadro - Advanced Molecular Editor (version " << VERSION << ')' << std::endl << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -h, --help\t\tShow help options (this)" << std::endl;
  std::cout << "  -v, --version\t\tShow version information" << std::endl;
  std::cout << "  --disable-hidpi-scaling\tDisable Qt high-DPI scaling (for GPU driver quirks)" << std::endl;
  #else
  std::wcout << QCoreApplication::translate("main.cpp", "Usage: %1 [options] [files]\n\n"
      "Avogadro - Advanced Molecular Editor (version %2)\n\n"
      "Options:\n"
      "  -h, --help\t\tShow help options (this)\n"
      "  -v, --version\t\tShow version information\n"
      "  --disable-hidpi-scaling\tDisable Qt high-DPI scaling (for GPU driver quirks)\n"
      ).arg(appName, VERSION).toStdWString();
  #endif
}
