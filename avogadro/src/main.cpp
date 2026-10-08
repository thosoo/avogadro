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

  // Ask hybrid-GPU drivers (NVIDIA Optimus / AMD PowerXpress) to route this
  // process to the high-performance GPU. The driver reads these named exports
  // from the executable before the first OpenGL context is created. File scope
  // in the primary exe only; no runtime code required. Windows' per-user
  // Graphics-settings preference, when present, overrides these hints.
  extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
  extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

#ifdef AVO_APP_BUNDLE
  #include <cstdlib>
#endif

using namespace Avogadro;

#ifdef WIN32

// Best-effort, Windows-only fallback for the driver-level hint exports above:
// register a per-user DirectX GPU preference that Windows consults at process
// launch. We only write when no preference exists for this executable and a
// second (dedicated) adapter is present. An existing preference is never
// touched. Every call is non-fatal; any failure is a single qDebug line.
static void registerUserGpuPreference()
{
  // The Settings UI stores the value name as a backslash path; normalize ours
  // to match so the two views stay consistent.
  const QString valueName =
    QCoreApplication::applicationFilePath().replace(QLatin1Char('/'),
                                                    QLatin1Char('\\'));

  HKEY key = 0;
  const wchar_t *subKey = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";

  // Only read here first: if a preference already exists, leave it alone.
  if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
    DWORD type = 0, size = 0;
    if (RegGetValueW(key, nullptr, reinterpret_cast<LPCWSTR>(valueName.utf16()),
                     RRF_RT_REG_SZ, &type, nullptr, &size) == ERROR_SUCCESS) {
      // A preference exists (any value: 0, 1, or 2). Do not modify it.
      RegCloseKey(key);
      return;
    }
    // ERROR_FILE_NOT_FOUND: no preference for us yet, continue to write path.
    RegCloseKey(key);
  }
  // Key missing entirely, or no entry for us: treat as "no preference".

  // Only act on a hybrid system: probe for a second active adapter.
  int activeAdapters = 0;
  DISPLAY_DEVICE dd;
  ZeroMemory(&dd, sizeof(dd));
  dd.cb = sizeof(dd);
  for (DWORD i = 0; i < 32; ++i) {
    if (EnumDisplayDevicesW(NULL, i, &dd, 0)) {
#if _WIN32_WINNT >= 0x0601
      if (dd.StateFlags & DISPLAY_DEVICE_ACTIVE) {
#else
      if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
#endif
        ++activeAdapters;
      }
    }
    else {
      break;
    }
  }
  if (activeAdapters < 2) {
    // Single-adapter machine: nothing to force here.
    return;
  }

  // Write GpuPreference=2; (high performance). Best effort; ignore errors.
  if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_SET_VALUE, &key)
        == ERROR_SUCCESS) {
    const wchar_t *value = L"GpuPreference=2;";
    const DWORD byteLen = (static_cast<DWORD>(lstrlenW(value)) + 1) * sizeof(wchar_t);
    const LONG rc = RegSetValueExW(key, reinterpret_cast<LPCWSTR>(valueName.utf16()),
                                   0, REG_SZ,
                                   reinterpret_cast<const BYTE *>(value), byteLen);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
      qDebug() << "registerUserGpuPreference: RegSetValueExW failed" << rc;
    }
  }
}

#endif

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

  // Best-effort per-user GPU preference fallback (Windows hybrid-GPU only).
  // Launch 1 relies on the driver-level hint exports above; this write takes
  // effect on subsequent launches. Never overwrites an existing preference.
  registerUserGpuPreference();

#endif

  // Output the untranslated application and library version - bug reports
  QString versionInfo = "Avogadro version:\t" + QString(VERSION) + "\tGit:\t"
                        + QString(SCM_REVISION) + "\nLibAvogadro version:\t"
                        + Library::version() + "\tGit:\t" + Library::scmRevision();
  qDebug() << versionInfo;

#ifdef WIN32
#ifndef AVO_APP_BUNDLE
  // Point OpenBabel at the bundled data/plugins installed by the
  // Windows installer so file formats and forcefields are available.
  QString babelDataDir = QCoreApplication::applicationDirPath();
  QString babelShareDir = babelDataDir + "/../share/openbabel/" + QString(BABEL_VERSION);
  if (QFileInfo::exists(babelShareDir))
    babelDataDir = babelShareDir;

  QString babelLibDir = QCoreApplication::applicationDirPath() +
                        "/../lib/openbabel/" + QString(BABEL_VERSION);

  qDebug() << "BABEL_DATADIR" << babelDataDir;
  _putenv_s("BABEL_DATADIR", babelDataDir.toLocal8Bit().constData());

  if (QFileInfo::exists(babelLibDir)) {
    qDebug() << "BABEL_LIBDIR" << babelLibDir;
    _putenv_s("BABEL_LIBDIR", babelLibDir.toLocal8Bit().constData());
  }

  OpenBabel::OBConversion conversion;
  const bool cmlReaderAvailable = conversion.SetInFormat("cml");
  qDebug() << "OpenBabel CML reader available at startup:" << cmlReaderAvailable;
#endif
#endif

#ifdef AVO_APP_BUNDLE
  // Set up the babel data and plugin directories for Mac - relocatable
  // This also works for the Windows package, but BABEL_LIBDIR is ignored

  // Point BABEL_DATADIR and BABEL_LIBDIR at the OpenBabel directories
  // including the OpenBabel version.
  QByteArray babelDataDir(
      (QCoreApplication::applicationDirPath() + "/../share/openbabel/" +
       QString(BABEL_VERSION)).toLatin1());
  QByteArray babelLibDir(
      (QCoreApplication::applicationDirPath() + "/../lib/openbabel/" +
       QString(BABEL_VERSION)).toLatin1());

#ifdef _MSC_VER
  int res1 = _putenv_s("BABEL_DATADIR", babelDataDir.data());
  int res2 = _putenv_s("BABEL_LIBDIR", babelLibDir.data());
#else
  int res1 = setenv("BABEL_DATADIR", babelDataDir.data(), 1);
  int res2 = setenv("BABEL_LIBDIR", babelLibDir.data(), 1);
#endif

  qDebug() << "BABEL_LIBDIR" << babelLibDir.data();

  if (res1 != 0 || res2 != 0)
    qDebug() << "Error: setenv failed." << res1 << res2;

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
