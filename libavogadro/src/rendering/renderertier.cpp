/**********************************************************************
  Renderer tier selection implementation (see renderertier.h).
 *****************************************************************/

#include "renderertier.h"

#include <QDebug>
#include <QtGlobal>
// qopenglfunctions.h pulls in qopengl.h, which includes the system GL headers
// and exposes the legacy entry points (glGetString) in the global namespace.
#include <QOpenGLFunctions>
// QOpenGLContext::currentContext() (used to read the resolved profile for the
// profile-aware tier check, design fix-modern-tier-core-profile D2).
#include <QOpenGLContext>

namespace Avogadro {
namespace rendering {

namespace {
constexpr const char *kRendererEnv = "AVOGADRO_RENDERER";

// Capability probe cache. QOpenGLWidget contexts within one process share
// capabilities, but a widget whose context is not current at probe time (e.g.
// a widget sharing another's context) reads a null GL_VERSION. Caching the
// first successful read keeps those widgets from silently degrading to a
// lower tier than the one the process is actually running (design D7).
bool s_probeValid = false;
int s_major = 0;
int s_minor = 0;

//! Parse the leading integer of a string. GL_VERSION suffixes like
//! "3.3 (Compatibility Profile) Mesa ..." carry text after the minor number,
//! so QString::toInt()'s whole-string conversion cannot be used here.
int leadingInt(const QString &s)
{
  int n = 0;
  for (int i = 0; i < s.size() && s.at(i).isDigit(); ++i)
    n = n * 10 + s.at(i).digitValue();
  return n; // 0 when there are no leading digits
}

//! Parse "<major>.<minor>..." out of the live context's GL_VERSION string.
//! Returns false when no context is current or the string is unparsable.
bool probeContextVersion(int &major, int &minor)
{
  // glGetString is declared by the Qt OpenGL headers; the returned GLubyte*
  // is just a NUL-terminated ASCII string, so take it as const char*.
  const char *versionStr =
    reinterpret_cast<const char *>(glGetString(GL_VERSION));
  if (!versionStr || !*versionStr)
    return false;

  const QStringList parts = QString::fromLatin1(versionStr).split('.');
  if (parts.size() < 2)
    return false;

  major = leadingInt(parts.at(0));
  minor = leadingInt(parts.at(1));
  return major > 0;
}
} // namespace

bool tierCompiledIn(RendererTier tier)
{
  switch (tier) {
    case RendererTier::Modern:
#ifdef AVO_RENDERER_TIER2
      return true;
#else
      return false;
#endif
    case RendererTier::Compat:
#ifdef AVO_RENDERER_TIER1
      return true;
#else
      return false;
#endif
    case RendererTier::Legacy:
#ifdef AVO_RENDERER_LEGACY
      return true;
#else
      return false;
#endif
    default:
      return false;
  }
}

void tierGlFloor(RendererTier tier, int &major, int &minor)
{
  switch (tier) {
    case RendererTier::Modern:
      major = 3;
      minor = 3;
      break;
    case RendererTier::Compat:
      major = 2;
      minor = 0;
      break;
    default: // Legacy: no floor.
      major = 1;
      minor = 0;
      break;
  }
}

QString tierName(RendererTier tier)
{
  switch (tier) {
    case RendererTier::Modern:
      return QStringLiteral("modern");
    case RendererTier::Compat:
      return QStringLiteral("compat");
    case RendererTier::Legacy:
      return QStringLiteral("legacy");
    default:
      return QStringLiteral("unavailable");
  }
}

RendererTier requestedRendererTier(bool &forced)
{
  forced = false;
  const QString choice =
    QString::fromUtf8(qgetenv(kRendererEnv)).toLower().trimmed();
  if (choice.isEmpty())
    return RendererTier::Modern; // unused when !forced

  if (choice == QLatin1String("modern") || choice == QLatin1String("tier2")) {
    forced = true;
    return RendererTier::Modern;
  }
  if (choice == QLatin1String("compat") || choice == QLatin1String("tier1")) {
    forced = true;
    return RendererTier::Compat;
  }
  if (choice == QLatin1String("legacy") || choice == QLatin1String("tier0")) {
    forced = true;
    return RendererTier::Legacy;
  }

  qWarning() << "Ignoring unknown" << kRendererEnv << "value" << choice
             << "(expected modern|tier2|compat|tier1|legacy|tier0);"
                " using capability probe.";
  return RendererTier::Modern;
}

bool tierSupportedAt(int major, int minor,
                     QSurfaceFormat::OpenGLContextProfile profile,
                     RendererTier tier)
{
  int floorMajor = 0;
  int floorMinor = 0;
  tierGlFloor(tier, floorMajor, floorMinor);

  const bool versionOk = (major > floorMajor) ||
                         (major == floorMajor && minor >= floorMinor);
  if (!versionOk)
    return false;

  // The modern tier draws with shaders and is core-safe, so it only needs its
  // GL version floor. compat/legacy use the fixed-function pipeline
  // (glMaterial/glLight/glMatrixMode), which is removed in the Core profile,
  // so they are unavailable on a Core-profile context (design D2).
  if (tier == RendererTier::Modern)
    return true;
  return profile != QSurfaceFormat::CoreProfile;
}

bool contextSupportsTier(RendererTier tier,
                         QSurfaceFormat::OpenGLContextProfile profile)
{
  if (!s_probeValid) {
    if (!probeContextVersion(s_major, s_minor))
      return false; // no current context yet; nothing to cache
    s_probeValid = true;
  }

  return tierSupportedAt(s_major, s_minor, profile, tier);
}

//! Profile label for diagnostics: "Core" vs "Compatibility"/"NoProfile".
const char *profileLabel(QSurfaceFormat::OpenGLContextProfile profile)
{
  switch (profile) {
    case QSurfaceFormat::CoreProfile:
      return "Core";
    case QSurfaceFormat::CompatibilityProfile:
      return "Compatibility";
    default:
      return "NoProfile";
  }
}

//! GL floor check helper, shared by tierSupportedAt() and resolveForcedTier().
bool versionMeetsFloor(int major, int minor, int floorMajor, int floorMinor)
{
  return (major > floorMajor) || (major == floorMajor && minor >= floorMinor);
}

RendererTier resolveForcedTier(RendererTier requested, int major, int minor,
                               QSurfaceFormat::OpenGLContextProfile profile,
                               bool haveContext)
{
  if (!tierCompiledIn(requested)) {
    qWarning().noquote() << kRendererEnv << "requested the" << tierName(requested)
                         << "tier but it was compiled out of this build"
                         << " (AVO_RENDERER_* CMake options).";
    // Caller turns Unavailable into a clear startup failure (design D7).
    return RendererTier::Unavailable;
  }

  // A forced legacy request with no live context resolves to Legacy: it is the
  // escape hatch that needs no probe (no GL floor, and the unit test runs
  // without a context). When a context IS current, legacy is gated by profile
  // like compat (design D2).
  if (requested == RendererTier::Legacy && !haveContext)
    return requested;

  if (tierSupportedAt(major, minor, profile, requested))
    return requested;

  // Failed to honour the forced tier: name the failing constraint so the field
  // report is attributable (design D2). Either the context is below the tier's
  // GL floor, or the resolved profile cannot serve the fixed-function path that
  // compat/legacy require on a Core context.
  int floorMajor = 0;
  int floorMinor = 0;
  tierGlFloor(requested, floorMajor, floorMinor);
  if (!versionMeetsFloor(major, minor, floorMajor, floorMinor)) {
    qWarning().noquote() << kRendererEnv << "requested the" << tierName(requested)
                         << "tier but this context is below its GL floor (needs GL"
                         << QStringLiteral("%1.%2).").arg(floorMajor).arg(floorMinor);
  } else {
    qWarning().noquote() << kRendererEnv << "requested the" << tierName(requested)
                         << "tier but the resolved context profile ("
                         << profileLabel(profile) << ") does not support the "
                         << "fixed-function path this tier requires.";
  }
  return RendererTier::Unavailable;
}

RendererTier resolveRendererTier(bool forced, RendererTier requested)
{
  // Preference order, highest tier first (design D7).
  const RendererTier order[] = { RendererTier::Modern, RendererTier::Compat,
                                 RendererTier::Legacy };

  if (forced) {
    // Read the resolved context version + profile and delegate to the pure
    // forced resolver (design D2, task 2.3).
    const bool haveContext =
      QOpenGLContext::currentContext() &&
      QOpenGLContext::currentContext()->isValid();
    const QSurfaceFormat::OpenGLContextProfile ctxProfile =
      haveContext
        ? QOpenGLContext::currentContext()->format().profile()
        : QSurfaceFormat::NoProfile;
    int major = s_major,
        minor = s_minor;
    if (haveContext && !s_probeValid) {
      // Probe once so the forced diagnostic names the real version.
      if (probeContextVersion(s_major, s_minor))
        s_probeValid = true;
    }
    return resolveForcedTier(requested, major, minor, ctxProfile, haveContext);
  }

#ifdef Q_OS_MAC
  // Spec "macOS uses legacy": the modern tier is compiled out on Apple and the
  // compat tier is not validated there, so the frozen legacy tier is default.
  return RendererTier::Legacy;
#endif

  // Resolve the live context's profile so the chosen tier's profile constraint
  // is logged explicitly (design D2, spec "default tier choice on a Core-only
  // context is explicit"): a Core context cannot run the fixed-function
  // compat/legacy tiers, so the modern tier is what runs and the log should
  // say so.
  const QSurfaceFormat::OpenGLContextProfile ctxProfile =
    (QOpenGLContext::currentContext() &&
     QOpenGLContext::currentContext()->isValid())
      ? QOpenGLContext::currentContext()->format().profile()
      : QSurfaceFormat::NoProfile;

  for (RendererTier tier : order) {
    if (tierCompiledIn(tier) &&
        (tier == RendererTier::Legacy ||
         contextSupportsTier(tier, ctxProfile)))
      return tier;
  }
  // LEGACY compiled out too: nothing renderable remains.
  return RendererTier::Unavailable;
}

} // namespace rendering
} // namespace Avogadro
