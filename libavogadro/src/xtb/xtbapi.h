/**********************************************************************
  XtbApi - Runtime loader for the xTB C API

  Resolves the xTB shared library at runtime via QLibrary instead of
  linking it at build time, so the application builds and ships on every
  platform and the xTB backends are usable exactly when the library is
  resolvable.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 ***********************************************************************/

#ifndef XTBAPI_H
#define XTBAPI_H

#include "xtb.h"

#include <QString>

namespace Avogadro {

/**
 * @class XtbApi
 * @brief Runtime loader for the xTB C API.
 *
 * All members are static; there are no instances. The library is loaded
 * once (guarded by a static QMutex) and cached. Each accessor returns a
 * function pointer to the corresponding xTB entry point, or nullptr when
 * the library is unavailable.
 */
class XtbApi
{
public:
  // Function-pointer typedefs (mirror the declarations in xtb.h).
  typedef xtb_TEnvironment (*XtbNewEnvironment)();
  typedef void (*XtbDelEnvironment)(xtb_TEnvironment*);
  typedef int (*XtbCheckEnvironment)(xtb_TEnvironment);
  typedef void (*XtbGetError)(xtb_TEnvironment, char*, const int*);
  typedef void (*XtbSetVerbosity)(xtb_TEnvironment, int);
  typedef xtb_TMolecule (*XtbNewMolecule)(xtb_TEnvironment, const int*,
                                          const int*, const double*,
                                          const double*, const int*,
                                          const double*, const bool*);
  typedef void (*XtbDelMolecule)(xtb_TMolecule*);
  typedef void (*XtbUpdateMolecule)(xtb_TEnvironment, xtb_TMolecule,
                                    const double*, const double*);
  typedef xtb_TCalculator (*XtbNewCalculator)();
  typedef void (*XtbDelCalculator)(xtb_TCalculator*);
  typedef void (*XtbLoadGFN0xTB)(xtb_TEnvironment, xtb_TMolecule,
                                 xtb_TCalculator, char*);
  typedef void (*XtbLoadGFN1xTB)(xtb_TEnvironment, xtb_TMolecule,
                                 xtb_TCalculator, char*);
  typedef void (*XtbLoadGFN2xTB)(xtb_TEnvironment, xtb_TMolecule,
                                 xtb_TCalculator, char*);
  typedef void (*XtbLoadGFNFF)(xtb_TEnvironment, xtb_TMolecule,
                               xtb_TCalculator, char*);
  typedef void (*XtbSetAccuracy)(xtb_TEnvironment, xtb_TCalculator, double);
  typedef void (*XtbSetMaxIter)(xtb_TEnvironment, xtb_TCalculator, int);
  typedef void (*XtbSetElectronicTemp)(xtb_TEnvironment, xtb_TCalculator,
                                       double);
  typedef void (*XtbSinglepoint)(xtb_TEnvironment, xtb_TMolecule,
                                 xtb_TCalculator, xtb_TResults);
  typedef xtb_TResults (*XtbNewResults)();
  typedef void (*XtbDelResults)(xtb_TResults*);
  typedef void (*XtbGetEnergy)(xtb_TEnvironment, xtb_TResults, double*);
  typedef void (*XtbGetGradient)(xtb_TEnvironment, xtb_TResults, double*);

  /// True when the xTB library was resolved and the entry points are usable.
  static bool isAvailable();

  /// Human-readable description of why loading failed (empty when available).
  static QString lastError();

  // Accessors: return the function pointer, or nullptr when unavailable.
  static XtbNewEnvironment xtb_newEnvironment();
  static XtbDelEnvironment xtb_delEnvironment();
  static XtbCheckEnvironment xtb_checkEnvironment();
  static XtbGetError xtb_getError();
  static XtbSetVerbosity xtb_setVerbosity();
  static XtbNewMolecule xtb_newMolecule();
  static XtbDelMolecule xtb_delMolecule();
  static XtbUpdateMolecule xtb_updateMolecule();
  static XtbNewCalculator xtb_newCalculator();
  static XtbDelCalculator xtb_delCalculator();
  static XtbLoadGFN0xTB xtb_loadGFN0xTB();
  static XtbLoadGFN1xTB xtb_loadGFN1xTB();
  static XtbLoadGFN2xTB xtb_loadGFN2xTB();
  static XtbLoadGFNFF xtb_loadGFNFF();
  static XtbSetAccuracy xtb_setAccuracy();
  static XtbSetMaxIter xtb_setMaxIter();
  static XtbSetElectronicTemp xtb_setElectronicTemp();
  static XtbSinglepoint xtb_singlepoint();
  static XtbNewResults xtb_newResults();
  static XtbDelResults xtb_delResults();
  static XtbGetEnergy xtb_getEnergy();
  static XtbGetGradient xtb_getGradient();
};

} // namespace Avogadro

#endif // XTBAPI_H
