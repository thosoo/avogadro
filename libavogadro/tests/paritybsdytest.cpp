/**********************************************************************
  paritybsdytest - legacy vs modern visual parity for the BSDY ball-and-stick
  engine (gpu-rendering-pipeline task 3.1). One engine per binary because the
  engines are Qt plugins and their plugin metadata cannot be duplicated.
 *****************************************************************/

#include "parityharness.h"

#include "libavogadro/src/engines/bsdyengine.h"

using namespace Avogadro;
using Avogadro::testing::ParityResult;
using Avogadro::testing::ParityPainterDevice;

class ParityBsdyTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void testBSDYParity();
  void testSelectionHaloEmitted();

private:
  Molecule m_molecule;
  Avogadro::testing::HarnessColor m_colorMap;
  QOpenGLContext *m_ctx = nullptr;
  QOffscreenSurface *m_surface = nullptr;
};

void ParityBsdyTest::initTestCase()
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

void ParityBsdyTest::testBSDYParity()
{
  ParityResult r = Avogadro::testing::runEngineParity<BSDYEngine>(
      m_molecule, &m_colorMap, m_ctx, m_surface);
  QVERIFY2(r.ok, qPrintable(r.message));
  qInfo().noquote() << "BSDY parity images:" << r.imageDir;
}

void ParityBsdyTest::testSelectionHaloEmitted()
{
  // Task 3.1: selection halos must survive the emitter. A selected atom is
  // drawn twice — its element-coloured sphere plus a larger selection-coloured
  // halo — so the store must contain a record carrying the selection colour.
  Molecule mol;
  Atom *a = mol.addAtom(6, Eigen::Vector3d(0, 0, 0));

  Avogadro::rendering::SceneBuilder builder;
  ParityPainterDevice device(&builder, &mol, &m_colorMap);
  BSDYEngine engine;
  engine.setMolecule(&mol);
  engine.setPainterDevice(&device);
  engine.setEnabled(true);

  // Mark the atom selected via a selecting PainterDevice.
  class SelectingDevice : public ParityPainterDevice
  {
  public:
    SelectingDevice(Avogadro::Painter *p, Molecule *m, Color *c)
      : ParityPainterDevice(p, m, c) {}
    bool isSelected(const Avogadro::Primitive *p) const override
    {
      return p == m_selected;
    }
    const Avogadro::Primitive *m_selected = nullptr;
  };

  SelectingDevice sdev(&builder, &mol, &m_colorMap);
  sdev.m_selected = a;
  engine.setPainterDevice(&sdev);

  builder.begin();
  builder.clearStores();
  // BSDY draws selection halos in its transparent pass.
  engine.renderTransparent(&sdev);
  builder.end();

  // At m_alpha == 1 the atom itself is emitted by the opaque pass, so the
  // transparent pass contributes exactly the selection halo.
  QCOMPARE(builder.spheres().size(), 1u);

  // One record must carry the selection colour rather than the element colour.
  Color selectionMap;
  selectionMap.setToSelectionColor();
  const QColor sel = selectionMap.color();
  bool foundHalo = false;
  for (size_t i = 0; i < builder.spheres().size(); ++i) {
    const auto &s = builder.spheres().at(i);
    if (qAbs(int(s.color[0]) - sel.red()) <= 8 &&
        qAbs(int(s.color[1]) - sel.green()) <= 8 &&
        qAbs(int(s.color[2]) - sel.blue()) <= 8) {
      foundHalo = true;
      break;
    }
  }
  QVERIFY2(foundHalo, "no sphere record carries the selection colour");
}

QTEST_MAIN(ParityBsdyTest)
#include "paritybsdytest.moc"
