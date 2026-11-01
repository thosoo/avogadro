/**********************************************************************
  Renderer tier selection unit test (gpu-rendering-pipeline task 7.1).

  Covers the GL-free parts of rendering/renderertier.h: the AVOGADRO_RENDERER
  override parsing, tier naming round-trip (logs must use the same spellings
  the override accepts), compiled-in gating, and the resolution rules that do
  not need a live context. Capability probing itself needs a current GL
  context and is exercised by the app-level runs (design A8 verification).
 *****************************************************************/

#include <QtTest>

#include "libavogadro/src/rendering/renderertier.h"

using namespace Avogadro;
using rendering::RendererTier;
using rendering::requestedRendererTier;
using rendering::resolveForcedTier;
using rendering::resolveRendererTier;
using rendering::tierCompiledIn;
using rendering::tierGlFloor;
using rendering::tierName;
using rendering::tierSupportedAt;

namespace {

//! RAII env guard: sets AVOGADRO_RENDERER for the scope, restores after.
class EnvOverride
{
public:
  explicit EnvOverride(const QByteArray &value)
    : m_had(qEnvironmentVariableIsSet("AVOGADRO_RENDERER")),
      m_old(qgetenv("AVOGADRO_RENDERER"))
  {
    if (value.isEmpty())
      qunsetenv("AVOGADRO_RENDERER");
    else
      qputenv("AVOGADRO_RENDERER", value);
  }
  ~EnvOverride()
  {
    if (m_had)
      qputenv("AVOGADRO_RENDERER", m_old);
    else
      qunsetenv("AVOGADRO_RENDERER");
  }

private:
  bool m_had;
  QByteArray m_old;
};

} // namespace

class RendererTierTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void noOverrideDoesNotForce();
  void overrideSpellingsForce();
  void caseAndWhitespaceTolerant();
  void unknownValueIsIgnored();
  void tierNamesMatchOverrideSpellings();
  void legacyResolvesWithoutContext();
  void floorsMatchDesign();
  void profileAwareTierSupport();
  void forcedTierProfileDiagnostic();

private:
  // The forced-legacy resolution path never probes the context, so these run
  // in a plain QCoreApplication (ctest) with no GL context current.
};

void RendererTierTest::noOverrideDoesNotForce()
{
  EnvOverride guard(QByteArray());
  bool forced = true;
  requestedRendererTier(forced);
  QVERIFY2(!forced, "an unset AVOGADRO_RENDERER must not force a tier");
}

void RendererTierTest::overrideSpellingsForce()
{
  struct Case {
    const char *value;
    RendererTier expected;
  };
  const Case cases[] = {
    {"modern", RendererTier::Modern}, {"tier2", RendererTier::Modern},
    {"compat", RendererTier::Compat}, {"tier1", RendererTier::Compat},
    {"legacy", RendererTier::Legacy}, {"tier0", RendererTier::Legacy},
  };
  for (const Case &c : cases) {
    EnvOverride guard(c.value);
    bool forced = false;
    const RendererTier tier = requestedRendererTier(forced);
    QVERIFY2(forced, qPrintable(QString("'%1' must force").arg(c.value)));
    QVERIFY2(tier == c.expected,
             qPrintable(QString("'%1' must map to %2").arg(c.value).arg(tierName(c.expected))));
  }
}

void RendererTierTest::caseAndWhitespaceTolerant()
{
  // Users set env vars by hand; "MODERN", " Legacy " must still work.
  const char *values[] = {"MODERN", " Legacy ", "Compat"};
  for (const char *value : values) {
    EnvOverride guard(value);
    bool forced = false;
    requestedRendererTier(forced);
    QVERIFY2(forced, qPrintable(QString("'%1' must force").arg(value)));
  }
}

void RendererTierTest::unknownValueIsIgnored()
{
  // A typo must not brick the app: it falls back to the capability probe.
  EnvOverride guard("modren");
  bool forced = true;
  requestedRendererTier(forced);
  QVERIFY2(!forced, "an unknown AVOGADRO_RENDERER value must not force a tier");
}

void RendererTierTest::tierNamesMatchOverrideSpellings()
{
  // Round-trip contract: tierName() produces strings the override accepts,
  // so the startup log always names a settable value.
  const RendererTier tiers[] = {RendererTier::Modern, RendererTier::Compat,
                                RendererTier::Legacy};
  for (RendererTier tier : tiers) {
    EnvOverride guard(tierName(tier).toUtf8());
    bool forced = false;
    const RendererTier parsed = requestedRendererTier(forced);
    QVERIFY2(forced && parsed == tier,
             qPrintable(QString("tierName(%1) must be accepted by the override")
                          .arg(tierName(tier))));
  }
}

void RendererTierTest::legacyResolvesWithoutContext()
{
  if (!tierCompiledIn(RendererTier::Legacy))
    QSKIP("legacy tier is compiled out of this build");

  // Forced legacy has no GL floor, so resolution succeeds without probing —
  // this is the escape hatch when the context cannot be probed.
  QVERIFY(resolveRendererTier(true, RendererTier::Legacy) == RendererTier::Legacy);

  // A tier compiled out + explicitly requested must fail loudly (design D7),
  // never silently fall back.
  if (!tierCompiledIn(RendererTier::Compat)) {
    QVERIFY(resolveRendererTier(true, RendererTier::Compat) ==
            RendererTier::Unavailable);
  }
  if (!tierCompiledIn(RendererTier::Modern)) {
    QVERIFY(resolveRendererTier(true, RendererTier::Modern) ==
            RendererTier::Unavailable);
  }
}

void RendererTierTest::floorsMatchDesign()
{
  // Design D2/D9/spec: modern needs GL 3.3, compat GL 2.0, legacy no floor.
  int major = 0;
  int minor = 0;
  tierGlFloor(RendererTier::Modern, major, minor);
  QCOMPARE(major, 3);
  QCOMPARE(minor, 3);
  tierGlFloor(RendererTier::Compat, major, minor);
  QCOMPARE(major, 2);
  QCOMPARE(minor, 0);
  tierGlFloor(RendererTier::Legacy, major, minor);
  QVERIFY(major <= 1 && minor == 0);
}

// Design fix-modern-tier-core-profile D2 / task 2.2: the profile-aware
// predicate. A Core context cannot run the fixed-function compat/legacy tiers,
// but the modern (shader) tier is core-safe; and the version floor still
// applies to every tier.
void RendererTierTest::profileAwareTierSupport()
{
  using P = QSurfaceFormat::OpenGLContextProfile;

  // A high-version Core context: modern runs, compat and legacy do not.
  QVERIFY(tierSupportedAt(4, 6, P::CoreProfile, RendererTier::Modern));
  QVERIFY(!tierSupportedAt(4, 6, P::CoreProfile, RendererTier::Compat));
  QVERIFY(!tierSupportedAt(4, 6, P::CoreProfile, RendererTier::Legacy));

  // The same version with a compatibility profile: every tier runs.
  QVERIFY(tierSupportedAt(4, 6, P::CompatibilityProfile, RendererTier::Modern));
  QVERIFY(tierSupportedAt(4, 6, P::CompatibilityProfile, RendererTier::Compat));
  QVERIFY(tierSupportedAt(4, 6, P::CompatibilityProfile, RendererTier::Legacy));

  // The version floor still gates the modern tier on a Core context.
  QVERIFY(!tierSupportedAt(3, 2, P::CompatibilityProfile, RendererTier::Modern));
  QVERIFY(tierSupportedAt(3, 3, P::CompatibilityProfile, RendererTier::Modern));

  // Below the compat floor, nothing runs.
  QVERIFY(!tierSupportedAt(1, 9, P::CompatibilityProfile, RendererTier::Compat));
}

// Design fix-modern-tier-core-profile D2 / task 2.3: forced-tier resolution on
// a Core-resolving context fails loudly with the profile reason instead of
// silently degrading, while a core-safe forced modern tier proceeds.
void RendererTierTest::forcedTierProfileDiagnostic()
{
  using P = QSurfaceFormat::OpenGLContextProfile;

  // Forced legacy on a Core context cannot run (fixed-function path needs a
  // non-Core profile): Unavailable, not a silent fallback.
  QCOMPARE(resolveForcedTier(RendererTier::Legacy, 4, 6, P::CoreProfile, true),
           RendererTier::Unavailable);

  // Forced compat on a Core context also cannot run.
  QCOMPARE(resolveForcedTier(RendererTier::Compat, 4, 6, P::CoreProfile, true),
           RendererTier::Unavailable);

  // Forced modern on the same Core context proceeds (core-safe, version met).
  // Skip on builds where the modern tier is compiled out (macOS), where this
  // assertion would otherwise fail.
  if (tierCompiledIn(RendererTier::Modern)) {
    QCOMPARE(resolveForcedTier(RendererTier::Modern, 4, 6, P::CoreProfile, true),
             RendererTier::Modern);
  }

  // Forced legacy on a compatibility profile runs.
  QCOMPARE(resolveForcedTier(RendererTier::Legacy, 4, 6, P::CompatibilityProfile, true),
           RendererTier::Legacy);

  // Forced legacy with no live context resolves to Legacy (escape hatch).
  QCOMPARE(resolveForcedTier(RendererTier::Legacy, 0, 0, P::NoProfile, false),
           RendererTier::Legacy);

  // Below the GL floor, even a compatibility profile cannot run compat.
  QCOMPARE(resolveForcedTier(RendererTier::Compat, 1, 4, P::CompatibilityProfile, true),
           RendererTier::Unavailable);
}

#include "renderertiertest.moc"
QTEST_MAIN(RendererTierTest)
