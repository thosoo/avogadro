/**********************************************************************
  XtbParams - xTB parameter-data locator

  See xtbparams.h. This file is part of the Avogadro molecular editor
  project; licensed under the GNU General Public License version 2.
 ***********************************************************************/

#include "xtbparams.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Avogadro {
namespace XtbParams {

bool ensureGfnffParameters(QString* diagnostic)
{
  // Respect the user's environment: if they already point XTBHOME or XTBPATH
  // at their data, leave it untouched.
  if (qEnvironmentVariableIsSet("XTBHOME") || qEnvironmentVariableIsSet("XTBPATH")) {
    return true;
  }

  QStringList probes;
  // Bundled location (populated by the Windows installer).
  probes << QCoreApplication::applicationDirPath() + QStringLiteral("/share/xtb");
  // The distribution package's data (Ubuntu's xtb package).
  probes << QStringLiteral("/usr/share/xtb");

  for (const QString& dir : probes) {
    const QFileInfo dirInfo(dir);
    if (dirInfo.isDir() &&
        QFileInfo(dir + QStringLiteral("/.param_gfnff.xtb")).isFile()) {
      qputenv("XTBHOME", dir.toUtf8());
      qputenv("XTBPATH", dir.toUtf8());
      return true;
    }
  }

  if (diagnostic) {
    *diagnostic = QStringLiteral("GFN-FF parameter data not found; probed %1")
                    .arg(probes.join(QStringLiteral(", ")));
  }
  return false;
}

QString gfnffParameterPath()
{
  // Resolve the source parameter file (environment first, then bundled /
  // distribution locations).
  QString source;
  const QStringList envDirs = {
    qEnvironmentVariable("XTBHOME"),
    qEnvironmentVariable("XTBPATH"),
  };
  for (const QString& dir : envDirs) {
    if (dir.isEmpty())
      continue;
    const QFileInfo candidate(dir + QStringLiteral("/.param_gfnff.xtb"));
    if (candidate.isFile()) {
      source = candidate.absoluteFilePath();
      break;
    }
  }
  if (source.isEmpty()) {
    const QStringList probes = {
      QCoreApplication::applicationDirPath() + QStringLiteral("/share/xtb"),
      QStringLiteral("/usr/share/xtb"),
    };
    for (const QString& dir : probes) {
      const QFileInfo candidate(dir + QStringLiteral("/.param_gfnff.xtb"));
      if (candidate.isFile()) {
        source = candidate.absoluteFilePath();
        break;
      }
    }
  }
  if (source.isEmpty())
    return QString();

#ifdef Q_OS_WIN
  // The bundled MinGW libgfortran runtime cannot open the parameter file when
  // its path contains a space ("Program Files") or a leading dot
  // (".param_gfnff.xtb"): the file probe fast-fails before the file is found.
  // Copy the parameter data to a space-free, dot-free temp path and pass that
  // instead. A failed copy falls back to the original source path.
  const bool problematic = source.contains(QLatin1Char(' ')) ||
                           QFileInfo(source).fileName().startsWith(QLatin1Char('.'));
  if (!problematic)
    return source;

  const QString temp =
    QDir::tempPath() + QStringLiteral("/avogadro_param_gfnff.xtb");
  // QFile::copy does not overwrite: reuse an existing cached copy, else copy.
  if (QFile::exists(temp))
    return temp;
  if (QFile::copy(source, temp))
    return temp;
  return source;
#else
  return source;
#endif
}

} // namespace XtbParams
} // namespace Avogadro
