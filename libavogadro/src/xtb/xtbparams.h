/**********************************************************************
  XtbParams - xTB parameter-data locator

  Locates the directory holding the GFN-FF parameter data
  (.param_gfnff.xtb) so the xTB library can load it at runtime. The data is
  bundled next to the application on Windows (share/xtb) and shipped by the
  distribution package on Linux (/usr/share/xtb); a user-set XTBHOME or
  XTBPATH always wins.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 ***********************************************************************/

#ifndef XTBPARAMS_H
#define XTBPARAMS_H

#include <avogadro/global.h>

#include <QString>

namespace Avogadro {
namespace XtbParams {

/**
 * Ensure the GFN-FF parameter data directory is reachable via XTBHOME/XTBPATH.
 *
 * @param diagnostic optional out-parameter; on failure receives a message
 *   naming the probed paths.
 * @return true when GFN-FF parameters can be located (or a user-set
 *   XTBHOME/XTBPATH already exists); false otherwise.
 */
A_EXPORT bool ensureGfnffParameters(QString* diagnostic = nullptr);

} // namespace XtbParams
} // namespace Avogadro

#endif
