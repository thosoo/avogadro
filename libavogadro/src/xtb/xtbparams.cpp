/**********************************************************************
  XtbParams - xTB parameter-data locator

  See xtbparams.h. This file is part of the Avogadro molecular editor
  project; licensed under the GNU General Public License version 2.
 ***********************************************************************/

#include "xtbparams.h"

#include <QCoreApplication>
#include <QDir>
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

} // namespace XtbParams
} // namespace Avogadro
