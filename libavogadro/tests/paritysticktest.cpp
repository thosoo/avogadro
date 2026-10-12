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
  QVERIFY(m_ctx->create());
  m_surface = new QOffscreenSurface;
  m_surface->setFormat(fmt);
  m_surface->create();
  QVERIFY(m_ctx->isValid());
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
