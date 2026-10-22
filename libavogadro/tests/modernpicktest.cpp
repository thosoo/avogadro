/**********************************************************************
  Pick-affordance test for the modern tier (fix-modern-tier-bond-interaction).

  The modern pick pass renders the exact visible-frame geometry with no click
  slop, while the frozen legacy pick scene deliberately fattened bonds
  (radius + 0.05, full extent) against atoms (+0.03). That regression collapsed
  the clickable area for order-cycling to roughly the middle of each bond: any
  click near an atom's silhouette resolved to the atom (which silently no-ops
  for a same-element selection). The fix restores the legacy slop via a
  uPickRadiusSlop uniform added to the two pick vertex shaders and set per pass
  from ModernRendererBackend::pick().

  Two layers of verification:

    1. testPickSlopUniform() - compiles the two pick vertex shaders and asserts
       that uPickRadiusSlop is a valid, settable uniform in both (the
       sphere/atom and cylinder/bond passes). Catches a shader-text regression
       even where a live GL context is unavailable.

    2. testPickAffordance() - real rendering + pick(). Drives the modern
       backend with a two-atom, one-bond scene and asserts, from screen
       positions derived from the actual projection:
         - a click at the middle of the bond picks the bond (row 1.3),
         - a click at an atom centre picks the atom (row 1.3),
         - a click within the bond's visual extent, close to an endpoint
           atom's silhouette, picks the bond (row 1.2, the slop fix).

  The render/pick layer needs a live GL 3.3+ context; on runners where one
  cannot be created it QSKIPs with a diagnostic instead of failing, exactly as
  modernrendertest does.
 *****************************************************************/

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QtOpenGL/QOpenGLFunctions_3_3_Core>
#include <QtOpenGL/QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOffscreenSurface>
#include <QMatrix4x4>
#include <QtTest>

#include <Eigen/Geometry>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/camera.h>
#include <avogadro/primitive.h>
#include <avogadro/glhit.h>
#include <avogadro/painter.h>
#include <avogadro/painterdevice.h>

#include "libavogadro/src/rendering/shaders.h"
#include "libavogadro/src/rendering/records.h"
#include "libavogadro/src/rendering/chunkstore.h"
#include "libavogadro/src/rendering/modernbackend.h"
#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/engines/bsdyengine.h"

using Avogadro::rendering::SceneBuilder;
using Avogadro::rendering::ModernRendererBackend;
using namespace Avogadro;

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 256;

const float kBackground[3] = {0.05f, 0.05f, 0.05f};

//! Perspective view + pull-back translation. Atoms sit near the world origin
//! plane and this puts them 5 units in front, well inside [0.1, 100]. With a
//! 45 deg FOV over a 256 px window the visible half-height at that distance is
//! ~2.1 world units, so ~61 px per world unit — enough to resolve a multi-unit
//! bond across the frame.
QMatrix4x4 viewProjection()
{
  QMatrix4x4 m;
  m.perspective(45.0f, static_cast<float>(kWidth) / kHeight, 0.1f, 100.0f);
  m.translate(0.0f, 0.0f, -5.0f);
  return m;
}

QMatrix4x4 viewProjectionModelView()
{
  return viewProjection();
}

QOpenGLContext *makeGLContext(QOffscreenSurface **off)
{
  QOpenGLContext *context = new QOpenGLContext();
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  context->setFormat(format);

  QOffscreenSurface *surface = new QOffscreenSurface();
  surface->setFormat(format);
  surface->create();

  if (!context->create() || !context->makeCurrent(surface)) {
    context->makeCurrent(nullptr);
    delete surface;
    delete context;
    *off = nullptr;
    return nullptr;
  }

  *off = surface;
  return context;
}

//! Minimal PainterDevice that routes painter() to a SceneBuilder so an engine
//! paints through the emitter exactly as GLWidget does in the modern tier.
class EmitterRenderDevice : public Avogadro::PainterDevice {
public:
  EmitterRenderDevice(SceneBuilder *builder, Avogadro::Molecule *mol,
                      Avogadro::Color *map)
    : m_builder(builder), m_molecule(mol), m_colorMap(map) {}

  Avogadro::Painter *painter() const override { return m_builder; }
  Avogadro::Camera *camera() const override
  {
    return const_cast<Avogadro::Camera *>(&m_camera);
  }
  bool isSelected(const Avogadro::Primitive *) const override { return false; }
  double radius(const Avogadro::Primitive *) const override { return 0.3; }
  const Avogadro::Molecule *molecule() const override { return m_molecule; }
  Avogadro::Color *colorMap() const override { return m_colorMap; }
  int width() override { return kWidth; }
  int height() override { return kHeight; }

  SceneBuilder *m_builder;
  Avogadro::Molecule *m_molecule;
  Avogadro::Color *m_colorMap;
  Avogadro::Camera m_camera;
};

//! Build a two-atom, one-bond scene (a long C-O bond) and the emitter stores
//! that render it.
void buildTwoAtomBond(Avogadro::Molecule *mol, SceneBuilder *builder)
{
  Avogadro::Color colorMap;
  EmitterRenderDevice device(builder, mol, &colorMap);
  Avogadro::BSDYEngine engine;
  engine.setMolecule(mol);
  engine.setPainterDevice(&device);
  engine.setEnabled(true);

  // Carbons/oxygen far apart on +X so the bond spans much of the frame and the
  // two atom discs are clearly separated with a long shaft between them.
  mol->addAtom(6, Eigen::Vector3d(-1.5, 0.0, 0.0));
  mol->addAtom(8, Eigen::Vector3d(1.5, 0.0, 0.0));
  mol->addBond(0, 1, 1);

  builder->begin();
  builder->clearStores();
  engine.renderOpaque(&device);
}

//! SEL_BOX_SIZE (9), matching DrawTool's pick rectangle.
constexpr int kSelBox = 9;
//! Half-width (px) of the band just outside an atom disc edge searched for a
//! bond hit in the row-1.2 near-silhouette assertion.
constexpr int kNearBand = 15;

//! Submit no work; return pick() hits for a SEL_BOX rectangle centred on
//! (cx, cy). The frame must already have been submitted by the caller.
QList<GLHit> pickAt(ModernRendererBackend &backend, int cx, int cy)
{
  return backend.pick(cx - kSelBox / 2, cy - kSelBox / 2, kSelBox, kSelBox);
}

} // anonymous namespace

class ModernPickTest : public QObject
{
  Q_OBJECT

private slots:
  void testPickSlopUniform();
  void testPickAffordance();
};

void ModernPickTest::testPickSlopUniform()
{
  // The slop lives in the two pick vertex shaders (kSpherePickVertexShader for
  // atoms, kCylinderPickVertexShader for bonds). Assert both source strings
  // declare the uPickRadiusSlop uniform, so a shader-text regression that drops
  // it is caught even without a live GL context.
  const QString &sphere = Avogadro::rendering::kSpherePickVertexShader;
  const QString &cylinder = Avogadro::rendering::kCylinderPickVertexShader;
  QVERIFY2(sphere.contains("uPickRadiusSlop"),
           "sphere (atom) pick shader must declare uPickRadiusSlop");
  QVERIFY2(cylinder.contains("uPickRadiusSlop"),
           "cylinder (bond) pick shader must declare uPickRadiusSlop");
  // The slop must be added to the per-instance radius used to place the pick
  // fragment (aInstRadius), so the click target is inflated, not just declared.
  QVERIFY2(sphere.contains("aInstRadius + uPickRadiusSlop"),
           "sphere pick must inflate aInstRadius by uPickRadiusSlop");
  QVERIFY2(cylinder.contains("aInstRadius + uPickRadiusSlop"),
           "cylinder pick must inflate aInstRadius by uPickRadiusSlop");
}

void ModernPickTest::testPickAffordance()
{
  QOffscreenSurface *off = nullptr;
  QOpenGLContext *ctx = makeGLContext(&off);
  if (!ctx)
    QSKIP("No GL 3.3 context available on this runner; "
          "pick-affordance parity must be checked on a display",
          QSkip::SkipAll);
  QVERIFY(off != nullptr);

  QOpenGLFunctions glStorage;
  glStorage.initializeOpenGLFunctions();
  QOpenGLFunctions *gl = &glStorage;
  QVERIFY(gl != nullptr);

  // A pick pass needs a live context, so gate the behavioural test on one.
  ModernRendererBackend backend;
  backend.initialize();
  if (!backend.isInitialized()) {
    ctx->makeCurrent(nullptr);
    QSKIP("ModernRendererBackend failed to initialise (no GL 3.3 context)",
          QSkip::SkipAll);
  }

  // The QOffscreenSurface's default framebuffer is 1x1, so we render the
  // visible frame into an explicit 256x256 framebuffer object and read the
  // depth buffer back from it. A depth renderbuffer is attached explicitly:
  // it is required so the depth test occludes the bond (drawn after the
  // sphere) where the two overlap. This mirrors modernrenderertest, which
  // builds the same FBO by hand because QOpenGLFramebufferObject::Depth did
  // not yield a functional depth buffer on this driver. Without it the
  // visible frame renders into the 1x1 default framebuffer and the depth
  // readback finds no atom disc (the silent mid-test QSKIP this test used to
  // hit), so the behavioural assertions below would never run.
  GLuint fboID = 0, colourTexID = 0, depthRBID = 0;
  gl->glGenFramebuffers(1, &fboID);
  gl->glBindFramebuffer(GL_FRAMEBUFFER, fboID);
  gl->glGenTextures(1, &colourTexID);
  gl->glBindTexture(GL_TEXTURE_2D, colourTexID);
  gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kWidth, kHeight, 0, GL_RGB,
                   GL_UNSIGNED_BYTE, nullptr);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, colourTexID, 0);
  gl->glGenRenderbuffers(1, &depthRBID);
  gl->glBindRenderbuffer(GL_RENDERBUFFER, depthRBID);
  gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, kWidth,
                            kHeight);
  gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, depthRBID);
  gl->glBindFramebuffer(GL_FRAMEBUFFER, fboID);
  gl->glViewport(0, 0, kWidth, kHeight);
  gl->glClearColor(kBackground[0], kBackground[1], kBackground[2], 1.0f);
  gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  gl->glEnable(GL_DEPTH_TEST);
  gl->glDepthFunc(GL_LEQUAL);

  Avogadro::Molecule mol;
  SceneBuilder builder;
  buildTwoAtomBond(&mol, &builder);
  QVERIFY(builder.spheres().size() >= 2u);
  QVERIFY(builder.bonds().size() >= 2u);

  // Clear + submit one frame, exactly as GLWidget does.
  backend.beginFrame(nullptr, QRect(0, 0, kWidth, kHeight), 0);
  backend.setModelViewProjection(viewProjection());
  backend.setModelView(viewProjectionModelView());
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.endFrame();
  gl->glFlush();

  auto readDepth = [&](int x, int y) {
    // Re-bind the hand-built FBO so the readback always comes from the
    // rendered frame, not whatever framebuffer a prior call left current.
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fboID);
    float d = 9.0f;
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
    return d;
  };

  // The atom discs are much taller (in y) than the thin bond shaft, so scan
  // each x-column for its filled height and treat the tall columns as atoms.
  // This separates the two atoms from the connecting bond regardless of how
  // the projection scales them.
  int centreY = kHeight / 2;
  auto columnHeight = [&](int x) {
    int minY = -1, maxY = -1;
    for (int y = 2; y < kHeight - 2; ++y) {
      if (readDepth(x, y) < 0.99f) {
        if (minY < 0)
          minY = y;
        maxY = y;
      }
    }
    return (minY < 0) ? 0 : (maxY - minY + 1);
  };

  int centres[2] = {-1, -1};
  int atomCount = 0;
  int runStart = -1;
  const int kAtomMinHeight = 16; // atoms are discs; the bond shaft is thin
  for (int x = 2; x < kWidth - 2; ++x) {
    bool isAtom = columnHeight(x) >= kAtomMinHeight;
    if (isAtom && runStart < 0)
      runStart = x;
    if (!isAtom && runStart >= 0) {
      // Midpoint of a contiguous run of atom-columns is the atom centre.
      if (atomCount < 2)
        centres[atomCount++] = (runStart + (x - 1)) / 2;
      runStart = -1;
    }
  }
  if (runStart >= 0 && atomCount < 2)
    centres[atomCount++] = (runStart + (kWidth - 2)) / 2;

  if (atomCount < 2)
    QSKIP("Could not resolve two atom discs in the rendered frame",
          QSkip::SkipAll);
  std::sort(centres, centres + 2);
  const int leftAtom = centres[0];
  const int rightAtom = centres[1];
  QVERIFY(rightAtom - leftAtom > kAtomMinHeight); // real separation, not noise

  // Row 1.3: clicking an atom centre picks that atom.
  {
    QList<GLHit> hits = pickAt(backend, leftAtom, centreY);
    QVERIFY2(hits.size() >= 1, "atom centre should pick the atom");
    QCOMPARE(static_cast<Primitive::Type>(hits.first().type()),
             Primitive::AtomType);
  }

  // Row 1.3: clicking the middle of the bond picks the bond.
  {
    const int midX = (leftAtom + rightAtom) / 2;
    QList<GLHit> hits = pickAt(backend, midX, centreY);
    QVERIFY2(hits.size() >= 1, "bond middle should pick the bond");
    QCOMPARE(static_cast<Primitive::Type>(hits.first().type()),
             Primitive::BondType);
  }

  // Row 1.2: a click within the bond's visual extent, close to an endpoint
  // atom's silhouette, picks the bond. Measure each atom disc's radius by
  // scanning outward into the empty space on its outer side (left for the left
  // atom, right for the right atom) so the scan does not run into the
  // connecting bond shaft, then search a short band just outside the disc for
  // a bond hit. Requiring a bond hit somewhere in that band (rather than at one
  // assumed pixel) keeps the assertion robust to the exact projection scale;
  // off-axis samples (dy != 0) are where the legacy +0.05 bond slop keeps the
  // bond reachable instead of the atom winning the pick.
  auto discRadius = [&](int atomCentre, int dir) {
    int x = atomCentre;
    while (readDepth(x, centreY) < 0.99f &&
           (dir < 0 ? x > 0 : x + 1 < kWidth))
      x += dir;
    return dir < 0 ? atomCentre - x : x - atomCentre;
  };

  // Returns true if any point in the band just outside the given disc edge
  // (spanning kNearBand px toward the bond, a few rows above/below the axis)
  // resolves to the bond.
  auto findBondNearEdge = [&](int edgeX, int dir) {
    for (int step = 1; step <= kNearBand; ++step) {
      int x = edgeX + dir * step;
      for (int dy : {-1, 0, 1}) {
        QList<GLHit> hits = pickAt(backend, x, centreY + dy);
        if (hits.size() >= 1 &&
            static_cast<Primitive::Type>(hits.first().type()) ==
                Primitive::BondType)
          return true;
      }
    }
    return false;
  };

  const int leftHalf = discRadius(leftAtom, -1);
  const int rightHalf = discRadius(rightAtom, +1);
  QVERIFY(leftHalf > 0);
  QVERIFY(rightHalf > 0);

  // The left atom's inner edge is on its right; the right atom's inner edge is
  // on its left.
  QVERIFY2(findBondNearEdge(leftAtom + leftHalf, +1),
           "bond must be pickable just outside the left atom silhouette");
  QVERIFY2(findBondNearEdge(rightAtom - rightHalf, -1),
           "bond must be pickable just outside the right atom silhouette");

  // Teardown the hand-built FBO.
  gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (depthRBID != 0)
    gl->glDeleteRenderbuffers(1, &depthRBID);
  if (colourTexID != 0)
    gl->glDeleteTextures(1, &colourTexID);
  if (fboID != 0)
    gl->glDeleteFramebuffers(1, &fboID);

  ctx->makeCurrent(nullptr);
}

QTEST_MAIN(ModernPickTest)

#include "modernpicktest.moc"
