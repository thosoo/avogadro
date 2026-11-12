/**********************************************************************
  XtbApi - Runtime loader for the xTB C API

  Resolves the xTB shared library at runtime via QLibrary. See xtbapi.h
  for the public interface.

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.
 ***********************************************************************/

#include "xtbapi.h"

#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

namespace Avogadro {

namespace {

// Guards the one-time load.
QMutex g_loadMutex;

// Cached resolution state.
bool g_tried = false;
bool g_available = false;
QString g_error;

// Resolved function pointers (nullptr when unavailable).
// The loaded QLibrary is retained for process lifetime (never deleted) so the
// library stays loaded and the resolved function pointers remain valid.
QLibrary* g_loadedLib = nullptr;
XtbApi::XtbNewEnvironment fn_xtb_newEnvironment = nullptr;
XtbApi::XtbDelEnvironment fn_xtb_delEnvironment = nullptr;
XtbApi::XtbCheckEnvironment fn_xtb_checkEnvironment = nullptr;
XtbApi::XtbGetError fn_xtb_getError = nullptr;
XtbApi::XtbSetVerbosity fn_xtb_setVerbosity = nullptr;
XtbApi::XtbNewMolecule fn_xtb_newMolecule = nullptr;
XtbApi::XtbDelMolecule fn_xtb_delMolecule = nullptr;
XtbApi::XtbUpdateMolecule fn_xtb_updateMolecule = nullptr;
XtbApi::XtbNewCalculator fn_xtb_newCalculator = nullptr;
XtbApi::XtbDelCalculator fn_xtb_delCalculator = nullptr;
XtbApi::XtbLoadGFN0xTB fn_xtb_loadGFN0xTB = nullptr;
XtbApi::XtbLoadGFN1xTB fn_xtb_loadGFN1xTB = nullptr;
XtbApi::XtbLoadGFN2xTB fn_xtb_loadGFN2xTB = nullptr;
XtbApi::XtbLoadGFNFF fn_xtb_loadGFNFF = nullptr;
XtbApi::XtbSetAccuracy fn_xtb_setAccuracy = nullptr;
XtbApi::XtbSetMaxIter fn_xtb_setMaxIter = nullptr;
XtbApi::XtbSetElectronicTemp fn_xtb_setElectronicTemp = nullptr;
XtbApi::XtbSinglepoint fn_xtb_singlepoint = nullptr;
XtbApi::XtbNewResults fn_xtb_newResults = nullptr;
XtbApi::XtbDelResults fn_xtb_delResults = nullptr;
XtbApi::XtbGetEnergy fn_xtb_getEnergy = nullptr;
XtbApi::XtbGetGradient fn_xtb_getGradient = nullptr;

// Resolves a single symbol, returning a void* that the caller casts.
void* resolve(QLibrary& lib, const char* name)
{
  return reinterpret_cast<void*>(lib.resolve(name));
}

} // namespace

bool XtbApi::isAvailable()
{
  QMutexLocker locker(&g_loadMutex);
  if (g_tried) {
    return g_available;
  }
  g_tried = true;

  QStringList names;
#ifdef Q_OS_WIN
  names << QStringLiteral("libxtb-6");
#else
  names << QStringLiteral("libxtb.so.6") << QStringLiteral("libxtb.so");
#endif

  QLibrary* loaded = nullptr;
  for (const QString& name : names) {
    QLibrary* candidate = new QLibrary(name);
    if (candidate->load()) {
      loaded = candidate;
      break;
    }
    delete candidate;
  }

  if (!loaded) {
    g_error = QStringLiteral("xTB library not found (tried: %1)")
                .arg(names.join(QStringLiteral(", ")));
    g_available = false;
    return false;
  }
  g_loadedLib = loaded;

  // Resolve every entry point; any missing core symbol is treated as
  // unavailable.
  fn_xtb_newEnvironment = reinterpret_cast<XtbNewEnvironment>(
    resolve(*loaded, "xtb_newEnvironment"));
  fn_xtb_delEnvironment = reinterpret_cast<XtbDelEnvironment>(
    resolve(*loaded, "xtb_delEnvironment"));
  fn_xtb_checkEnvironment = reinterpret_cast<XtbCheckEnvironment>(
    resolve(*loaded, "xtb_checkEnvironment"));
  fn_xtb_getError =
    reinterpret_cast<XtbGetError>(resolve(*loaded, "xtb_getError"));
  fn_xtb_setVerbosity =
    reinterpret_cast<XtbSetVerbosity>(resolve(*loaded, "xtb_setVerbosity"));
  fn_xtb_newMolecule =
    reinterpret_cast<XtbNewMolecule>(resolve(*loaded, "xtb_newMolecule"));
  fn_xtb_delMolecule =
    reinterpret_cast<XtbDelMolecule>(resolve(*loaded, "xtb_delMolecule"));
  fn_xtb_updateMolecule = reinterpret_cast<XtbUpdateMolecule>(
    resolve(*loaded, "xtb_updateMolecule"));
  fn_xtb_newCalculator = reinterpret_cast<XtbNewCalculator>(
    resolve(*loaded, "xtb_newCalculator"));
  fn_xtb_delCalculator = reinterpret_cast<XtbDelCalculator>(
    resolve(*loaded, "xtb_delCalculator"));
  fn_xtb_loadGFN0xTB = reinterpret_cast<XtbLoadGFN0xTB>(
    resolve(*loaded, "xtb_loadGFN0xTB"));
  fn_xtb_loadGFN1xTB = reinterpret_cast<XtbLoadGFN1xTB>(
    resolve(*loaded, "xtb_loadGFN1xTB"));
  fn_xtb_loadGFN2xTB = reinterpret_cast<XtbLoadGFN2xTB>(
    resolve(*loaded, "xtb_loadGFN2xTB"));
  fn_xtb_loadGFNFF =
    reinterpret_cast<XtbLoadGFNFF>(resolve(*loaded, "xtb_loadGFNFF"));
  fn_xtb_setAccuracy =
    reinterpret_cast<XtbSetAccuracy>(resolve(*loaded, "xtb_setAccuracy"));
  fn_xtb_setMaxIter =
    reinterpret_cast<XtbSetMaxIter>(resolve(*loaded, "xtb_setMaxIter"));
  fn_xtb_setElectronicTemp = reinterpret_cast<XtbSetElectronicTemp>(
    resolve(*loaded, "xtb_setElectronicTemp"));
  fn_xtb_singlepoint =
    reinterpret_cast<XtbSinglepoint>(resolve(*loaded, "xtb_singlepoint"));
  fn_xtb_newResults =
    reinterpret_cast<XtbNewResults>(resolve(*loaded, "xtb_newResults"));
  fn_xtb_delResults =
    reinterpret_cast<XtbDelResults>(resolve(*loaded, "xtb_delResults"));
  fn_xtb_getEnergy =
    reinterpret_cast<XtbGetEnergy>(resolve(*loaded, "xtb_getEnergy"));
  fn_xtb_getGradient =
    reinterpret_cast<XtbGetGradient>(resolve(*loaded, "xtb_getGradient"));

  // Require the core set of symbols the engine needs.
  if (!fn_xtb_newEnvironment || !fn_xtb_delEnvironment || !fn_xtb_newMolecule ||
      !fn_xtb_delMolecule || !fn_xtb_updateMolecule || !fn_xtb_newCalculator ||
      !fn_xtb_delCalculator || !fn_xtb_singlepoint || !fn_xtb_newResults ||
      !fn_xtb_delResults || !fn_xtb_getEnergy || !fn_xtb_getGradient) {
    g_error = QStringLiteral(
      "xTB library found but is missing required symbols");
    g_available = false;
    return false;
  }

  g_available = true;
  g_error.clear();
  return true;
}

QString XtbApi::lastError()
{
  QMutexLocker locker(&g_loadMutex);
  return g_error;
}

#define XTBAPI_ACCESSOR(Name, Type) \
  XtbApi::Type XtbApi::Name() { return fn_##Name; }

XTBAPI_ACCESSOR(xtb_newEnvironment, XtbNewEnvironment)
XTBAPI_ACCESSOR(xtb_delEnvironment, XtbDelEnvironment)
XTBAPI_ACCESSOR(xtb_checkEnvironment, XtbCheckEnvironment)
XTBAPI_ACCESSOR(xtb_getError, XtbGetError)
XTBAPI_ACCESSOR(xtb_setVerbosity, XtbSetVerbosity)
XTBAPI_ACCESSOR(xtb_newMolecule, XtbNewMolecule)
XTBAPI_ACCESSOR(xtb_delMolecule, XtbDelMolecule)
XTBAPI_ACCESSOR(xtb_updateMolecule, XtbUpdateMolecule)
XTBAPI_ACCESSOR(xtb_newCalculator, XtbNewCalculator)
XTBAPI_ACCESSOR(xtb_delCalculator, XtbDelCalculator)
XTBAPI_ACCESSOR(xtb_loadGFN0xTB, XtbLoadGFN0xTB)
XTBAPI_ACCESSOR(xtb_loadGFN1xTB, XtbLoadGFN1xTB)
XTBAPI_ACCESSOR(xtb_loadGFN2xTB, XtbLoadGFN2xTB)
XTBAPI_ACCESSOR(xtb_loadGFNFF, XtbLoadGFNFF)
XTBAPI_ACCESSOR(xtb_setAccuracy, XtbSetAccuracy)
XTBAPI_ACCESSOR(xtb_setMaxIter, XtbSetMaxIter)
XTBAPI_ACCESSOR(xtb_setElectronicTemp, XtbSetElectronicTemp)
XTBAPI_ACCESSOR(xtb_singlepoint, XtbSinglepoint)
XTBAPI_ACCESSOR(xtb_newResults, XtbNewResults)
XTBAPI_ACCESSOR(xtb_delResults, XtbDelResults)
XTBAPI_ACCESSOR(xtb_getEnergy, XtbGetEnergy)
XTBAPI_ACCESSOR(xtb_getGradient, XtbGetGradient)

} // namespace Avogadro
