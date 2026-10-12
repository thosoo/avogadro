/**********************************************************************
  Renderer tier selection unit test (gpu-rendering-pipeline task 7.1).

  Covers the GL-free parts of rendering/renderertier.h: the AVOGADRO_RENDERER
  override parsing, tier naming round-trip (logs must use the same spellings
  the override accepts), compiled-in gating, and the resolution rules that do
  not need a live context. Capability probing itself needs a current GL
  context and is exercised by the app-level runs (design A8 verification).
 *****************************************************************/

#include <QtTest>
#include <QScopedValueRollback>

#include "libavogadro/src/rendering/renderertier.h"

using namespace Avogadro;
using rendering::RendererTier;
using rendering::requestedRendererTier;
using rendering::resolveRendererTier;
using rendering::tierCompiledIn;
using rendering::tierGlFloor;
using rendering::tierName;

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

#include "renderertiertest.moc"
QTEST_MAIN(RendererTierTest)
