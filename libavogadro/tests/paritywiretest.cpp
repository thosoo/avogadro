/**********************************************************************
  paritywiretest - legacy vs modern visual parity for the wire engine
  (gpu-rendering-pipeline task 3.2).
 *****************************************************************/

#include "parityharness.h"

#include "libavogadro/src/engines/wireengine.h"

using namespace Avogadro;
using Avogadro::testing::ParityResult;

class ParityWireTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void testWireParity();

private:
  Molecule m_molecule;
  Avogadro::testing::HarnessColor m_colorMap;
  QOpenGLContext *m_ctx = nullptr;
  QOffscreenSurface *m_surface = nullptr;
};

void ParityWireTest::initTestCase()
{
  Avogadro::testing::buildMixedMolecule(m_molecule);

  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  m_ctx = new QOpenGLContext;
  m_ctx->setFormat(fmt);
  QVERIFY(m_ctx->create());
  m_surface = new QOffscreenSurface;
  m_surface->setFormat(fmt);
  m_surface->create();
  QVERIFY(m_ctx->isValid());
}

void ParityWireTest::testWireParity()
{
  // The modern tier renders wireframe lines as thin cylinders (design D2:
  // 1-px GL lines are unreliable on modern GL), so its coverage is several
  // times the legacy 1-px line coverage by design. The strict checks that
  // catch real defects — per-atom presence at projected positions, colour
  // classes, and legacy determinism — apply unchanged.
  ParityResult r = Avogadro::testing::runEngineParity<WireEngine>(
      m_molecule, &m_colorMap, m_ctx, m_surface, /*minCoverageRatio=*/0.25,
      /*maxCoverageRatio=*/4.0);
  QVERIFY2(r.ok, qPrintable(r.message));
  qInfo().noquote() << "wire parity images:" << r.imageDir;
}

QTEST_MAIN(ParityWireTest)
#include "paritywiretest.moc"
