/**********************************************************************
  paritysticktest - legacy vs modern visual parity for the stick engine
  (gpu-rendering-pipeline task 3.2).
 *****************************************************************/

#include "parityharness.h"

#include "libavogadro/src/engines/stickengine.h"

using namespace Avogadro;
using Avogadro::testing::ParityResult;

class ParityStickTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void testStickParity();

private:
  Molecule m_molecule;
  Avogadro::testing::HarnessColor m_colorMap;
  QOpenGLContext *m_ctx = nullptr;
  QOffscreenSurface *m_surface = nullptr;
};

void ParityStickTest::initTestCase()
{
  Avogadro::testing::buildMixedMolecule(m_molecule);

  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  m_ctx = new QOpenGLContext;
  m_ctx->setFormat(fmt);
  // A GL 3.3 compatibility context is required to drive the fixed-function
  // legacy tier and compare it against the instanced modern tier. When the
  // runner cannot provide one (e.g. a headless CI box with no usable GL),
  // skip the pixel-parity checks rather than failing: the emission-level
  // parity is still covered by threadedparitytest (byte-identical
  // engine-vs-tessellator output) headlessly.
  if (!m_ctx->create())
    QSKIP("No GL 3.3 compatibility context available on this runner; "
          "skipping parity test", SkipAll);
  m_surface = new QOffscreenSurface;
  m_surface->setFormat(fmt);
  m_surface->create();
  if (!m_ctx->isValid())
    QSKIP("Requested GL 3.3 compatibility context became invalid; "
          "skipping parity test", SkipAll);
}

void ParityStickTest::testStickParity()
{
  ParityResult r = Avogadro::testing::runEngineParity<StickEngine>(
      m_molecule, &m_colorMap, m_ctx, m_surface);
  QVERIFY2(r.ok, qPrintable(r.message));
  qInfo().noquote() << "stick parity images:" << r.imageDir;
}

QTEST_MAIN(ParityStickTest)
#include "paritysticktest.moc"
