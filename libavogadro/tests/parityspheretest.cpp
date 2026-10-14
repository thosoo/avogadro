/**********************************************************************
  parityspheretest - legacy vs modern visual parity for the VdW sphere engine,
  plus the design A9 check that its settings widget still affects rendering
  (opacity -> transparent pass with per-instance alpha).
 *****************************************************************/

#include "parityharness.h"

#include "libavogadro/src/engines/sphereengine.h"

#include <QSlider>
#include <QWidget>

using namespace Avogadro;
using Avogadro::testing::ParityResult;
using Avogadro::testing::ParityPainterDevice;

class ParitySphereTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void testSphereParity();
  void testOpacitySettingsWidgetAffectsRendering();

private:
  Molecule m_molecule;
  Avogadro::testing::HarnessColor m_colorMap;
  QOpenGLContext *m_ctx = nullptr;
  QOffscreenSurface *m_surface = nullptr;
};

void ParitySphereTest::initTestCase()
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

void ParitySphereTest::testSphereParity()
{
  ParityResult r = Avogadro::testing::runEngineParity<SphereEngine>(
      m_molecule, &m_colorMap, m_ctx, m_surface);
  QVERIFY2(r.ok, qPrintable(r.message));
  qInfo().noquote() << "sphere parity images:" << r.imageDir;
}

void ParitySphereTest::testOpacitySettingsWidgetAffectsRendering()
{
  // Task 3.2 + design A9: the engine's settings widget must still affect
  // rendering. Lowering the opacity slider has to move the geometry into the
  // transparent pass with per-instance alpha baked into the instance record.
  SphereEngine engine;
  engine.setMolecule(&m_molecule);

  Avogadro::rendering::SceneBuilder builder;
  ParityPainterDevice device(&builder, &m_molecule, &m_colorMap);
  engine.setPainterDevice(&device);
  engine.setEnabled(true);

  // Drive the real settings widget rather than private engine state, so the
  // verification covers the widget -> engine -> instance path.
  QWidget *settings = engine.settingsWidget();
  QVERIFY2(settings != nullptr, "sphere engine returned no settings widget");
  QSlider *opacity = settings->findChild<QSlider *>("opacitySlider");
  QVERIFY2(opacity != nullptr, "opacitySlider not found in sphere settings");

  // At full opacity the transparent pass must not emit the spheres.
  opacity->setValue(20);
  builder.begin();
  builder.clearStores();
  engine.renderTransparent(&device);
  builder.end();
  QCOMPARE(builder.spheres().size(), 0u);

  // Lower the opacity: the transparent pass must now emit spheres carrying the
  // baked per-instance alpha (8/20 == 0.4).
  opacity->setValue(8);
  builder.begin();
  builder.clearStores();
  engine.renderTransparent(&device);
  builder.end();

  QVERIFY2(builder.spheres().size() > 0,
           "transparent pass produced no spheres after lowering opacity");

  // The engine's first pass is the legacy colour-mask depth trick (alpha 1);
  // the real transparent spheres follow it carrying the baked per-instance
  // alpha, so assert that at least one record carries it.
  const int expectedAlpha = 8 * 255 / 20;
  bool foundTransparent = false;
  for (size_t i = 0; i < builder.spheres().size(); ++i) {
    if (int(builder.spheres().at(i).color[3]) == expectedAlpha) {
      foundTransparent = true;
      break;
    }
  }
  QVERIFY2(foundTransparent,
           qPrintable(QString("no sphere record carries the baked per-instance "
                              "alpha %1 (transparent pass)")
                          .arg(expectedAlpha)));
}

QTEST_MAIN(ParitySphereTest)
#include "parityspheretest.moc"
