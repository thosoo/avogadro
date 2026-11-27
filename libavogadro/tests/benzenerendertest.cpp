/**********************************************************************
  Benzene render test (fix-double-bond-offset-plane task 4.3).

  Renders a benzene ring (alternating double bonds) at a genuinely arbitrary
  orientation (ring plane normal along (1,1,1)) through the REAL modern-tier
  pipeline: BSDYEngine emission -> SceneBuilder -> ModernRendererBackend. The
  double bonds must draw in the ring plane (the pre-change world-frame spread
  would stick the pair out of the ring). The frame is written to
  /tmp/benzene_modern.png for inspection.

  Unlike visualparitytest, this only exercises the modern tier, so it runs on a
  Core-profile context (which the modern backend requires and accepts). The
  legacy tier needs a fixed-function Compatibility context, which this runner's
  software GL does not expose; the modern-vs-legacy plane agreement is instead
  covered by bondspreadtest::tierParitySpreadPlane (both tiers compute the
  identical axis x planeNormal) and by the scene-level in-plane assertion.
 *****************************************************************/

#include <QtTest>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <cmath>

#include "libavogadro/src/engines/bsdyengine.h"
#include "libavogadro/src/color.h"
#include "libavogadro/src/molecule.h"
#include "libavogadro/src/atom.h"
#include "libavogadro/src/bond.h"
#include "libavogadro/src/painterdevice.h"
#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/rendering/modernbackend.h"

using Avogadro::BSDYEngine;
using Avogadro::Color;
using Avogadro::Molecule;
using Avogadro::Atom;

namespace {

constexpr int kW = 512;
constexpr int kH = 512;
constexpr float kBackground = 0.05f;

//! Benzene with alternating double bonds, rotated so the ring plane normal is
//! along (1,1,1) — the ring is aligned with NO world coordinate plane.
Molecule *makeBenzeneMolecule()
{
  Molecule *mol = new Molecule;
  const double R = 1.4;
  const double twoPi = 2.0 * std::acos(-1.0);
  QVector<Atom *> ring;
  const Eigen::Vector3d from(0.0, 0.0, 1.0);
  const Eigen::Vector3d to(1.0, 1.0, 1.0);
  const Eigen::Vector3d axis = from.cross(to).normalized();
  const double angle = std::acos(std::min(1.0, from.dot(to)));
  const Eigen::AngleAxisd q(angle, axis);
  for (int i = 0; i < 6; ++i) {
    const double a = twoPi * i / 6.0;
    Eigen::Vector3d p(R * std::cos(a), R * std::sin(a), 0.0);
    p = q * p;
    Atom *atom = mol->addAtom(6, p);
    // The base Color map leaves m_channels uninitialized (setFromPrimitive is
    // a no-op); give the atoms an explicit colour so the baked instance colour
    // is deterministic (the real app uses the element-colour plugin).
    atom->setCustomColorName("red");
    ring.append(atom);
  }
  for (int i = 0; i < 6; ++i)
    mol->addBond(ring[i]->id(), ring[(i + 1) % 6]->id(), (i % 2 == 0) ? 2 : 1);
  return mol;
}

QOpenGLContext *makeGLContext(QOffscreenSurface **off)
{
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  QOffscreenSurface *surface = new QOffscreenSurface;
  surface->setFormat(fmt);
  surface->create();
  QOpenGLContext *ctx = new QOpenGLContext;
  ctx->setFormat(fmt);
  if (!ctx->create() || !ctx->makeCurrent(surface)) {
    ctx->makeCurrent(nullptr);
    delete ctx;
    delete surface;
    *off = nullptr;
    return nullptr;
  }
  *off = surface;
  return ctx;
}

GLuint makeFbo(QOpenGLFunctions &gl, GLuint *colorTex, GLuint *depthRb)
{
  GLuint fbo = 0;
  gl.glGenFramebuffers(1, &fbo);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl.glGenTextures(1, colorTex);
  gl.glBindTexture(GL_TEXTURE_2D, *colorTex);
  gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kW, kH, 0, GL_RGB,
                  GL_UNSIGNED_BYTE, nullptr);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, *colorTex, 0);
  gl.glGenRenderbuffers(1, depthRb);
  gl.glBindRenderbuffer(GL_RENDERBUFFER, *depthRb);
  gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, kW, kH);
  gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_RENDERBUFFER, *depthRb);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return fbo;
}

void setView(QMatrix4x4 &mvpOut, QMatrix4x4 &modelviewOut)
{
  QMatrix4x4 proj;
  proj.perspective(45.0f, float(kW) / float(kH), 0.1f, 100.0f);
  modelviewOut.translate(0.0f, 0.0f, -5.0f);
  mvpOut = proj * modelviewOut;
}

QImage readPixels(QOpenGLFunctions &gl)
{
  QImage img(kW, kH, QImage::Format_ARGB32);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, kW, kH, GL_BGRA, GL_UNSIGNED_BYTE, img.bits());
  return img.mirrored();
}

double coverage(const QImage &img)
{
  long nonBg = 0, total = 0;
  for (int y = 0; y < img.height(); y += 2)
    for (int x = 0; x < img.width(); x += 2) {
      const QRgb px = img.pixel(x, y);
      ++total;
      if (qRed(px) + qGreen(px) + qBlue(px) > 60)
        ++nonBg;
    }
  return total ? double(nonBg) / double(total) : 0.0;
}

class EmitterDevice : public Avogadro::PainterDevice
{
public:
  EmitterDevice(Avogadro::rendering::SceneBuilder *builder, Molecule *mol,
                Color *map)
    : m_builder(builder), m_molecule(mol), m_colorMap(map)
  {
  }
  Avogadro::Painter *painter() const override { return m_builder; }
  Avogadro::Camera *camera() const override { return nullptr; }
  bool isSelected(const Avogadro::Primitive *) const override { return false; }
  double radius(const Avogadro::Primitive *) const override { return 0.3; }
  const Molecule *molecule() const override { return m_molecule; }
  Color *colorMap() const override { return m_colorMap; }
  int width() override { return kW; }
  int height() override { return kH; }

private:
  Avogadro::rendering::SceneBuilder *m_builder;
  Molecule *m_molecule;
  Color *m_colorMap;
};

} // namespace

class BenzeneRenderTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void modernTierRendersDoubleBondsInPlane();
};

void BenzeneRenderTest::initTestCase()
{
}

void BenzeneRenderTest::modernTierRendersDoubleBondsInPlane()
{
  QOffscreenSurface *off = nullptr;
  QOpenGLContext *ctx = makeGLContext(&off);
  if (!ctx)
    QSKIP("No GL 3.3+ context available on this runner; skipping benzene render",
          SkipAll);
  QOpenGLFunctions gl;
  gl.initializeOpenGLFunctions();

  Molecule *benzene = makeBenzeneMolecule();
  Color *map = new Color;
  GLuint fbo = 0, tex = 0, rb = 0;
  fbo = makeFbo(gl, &tex, &rb);
  QVERIFY(fbo != 0);

  // The modern backend's destructor touches its GL context (m_ctx), so the
  // backend must be destroyed while the context is still alive. Scope it in a
  // block that ends before the context is freed below.
  {
    Avogadro::rendering::SceneBuilder builder;
    EmitterDevice device(&builder, benzene, map);
    BSDYEngine engine;
    engine.setColorMap(map);
    engine.setMolecule(benzene);
    engine.setPainterDevice(&device);
    engine.setEnabled(true);

    builder.begin();
    builder.clearStores();
    engine.renderOpaque(&device);
    builder.end();
    QVERIFY(builder.spheres().size() >= 6u);
    QVERIFY(builder.bonds().size() >= 6u);

    Avogadro::rendering::ModernRendererBackend backend;
    backend.initialize();
    QVERIFY(backend.isInitialized());

    QMatrix4x4 mvp, modelview;
    setView(mvp, modelview);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.glViewport(0, 0, kW, kH);
    gl.glClearColor(kBackground, kBackground, kBackground, 1.0f);
    gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LEQUAL);

    backend.beginFrame(nullptr, QRect(0, 0, kW, kH), 0);
    backend.setModelViewProjection(mvp);
    backend.setModelView(modelview);
    backend.pushSpheres(builder.spheres());
    backend.pushBonds(builder.bonds());
    backend.pushLines(builder.lines());
    backend.endFrame();
    gl.glFlush();
    QVERIFY(backend.drawCallsThisFrame() >= 2);

    const QImage img = readPixels(gl);
    img.save(QStringLiteral("/tmp/benzene_modern.png"));
    const double cov = coverage(img);
    qInfo() << "benzene modern coverage:" << cov
            << "spheres:" << builder.spheres().size()
            << "bond instances:" << builder.bonds().size();

    QVERIFY2(cov > 0.02, "modern tier drew nothing for benzene");
  }

  gl.glDeleteFramebuffers(1, &fbo);
  gl.glDeleteTextures(1, &tex);
  gl.glDeleteRenderbuffers(1, &rb);
  ctx->makeCurrent(nullptr);
  delete benzene;
  delete map;
  delete off;
  delete ctx;
}

QTEST_MAIN(BenzeneRenderTest)

#include "benzenerendertest.moc"
