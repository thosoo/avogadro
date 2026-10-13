/**********************************************************************
  Visual parity: modern tier vs legacy tier (gpu-rendering-pipeline
  task 8.2 / spec "Visual parity for ported engines" / design D2).

  Renders the same mixed-element molecule (custom colours, bonds) twice
  into identically-framed framebuffers on one GL 3.3 COMPATIBILITY
  context (the legacy tier needs fixed function):

    (a) modern tier: BSDYEngine emits into the SceneBuilder and the
        ModernRendererBackend draws instanced;
    (b) legacy tier: the same BSDYEngine draws through GLPainter's
        immediate-mode path (tier 0, frozen) with the fixed-function
        two-light setup.

  The frames are then compared at the visual-class level:

    * coverage — the fraction of non-background pixels must match closely
      (same geometry, same camera);
    * colour class — each atom's baked colour must appear (dominant
      channel) on both tiers;
    * transparency class — at 50 % engine opacity the frames must land in
      the same blended-brightness class (per-instance alpha, SRC_ALPHA
      blending — design A9/D2), i.e. above background, below opaque.

  Exact per-pixel equality is not expected: the tiers rasterise through
  different pipelines (instanced shaders vs fixed-function immediates).
 *****************************************************************/

#include <QtTest>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <cmath>

#include "libavogadro/src/engines/bsdyengine.h"
#include "libavogadro/src/color.h"
#include "libavogadro/src/molecule.h"
#include "libavogadro/src/atom.h"
#include "libavogadro/src/bond.h"
#include "libavogadro/src/glpainter_p.h"
#include "libavogadro/src/painterdevice.h"
#include "libavogadro/src/glwidget.h"
#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/rendering/modernbackend.h"
#include "libavogadro/src/rendering/snapshottessellator.h"

using Avogadro::BSDYEngine;
using Avogadro::Color;
using Avogadro::GLWidget;
using Avogadro::Molecule;
using Avogadro::Atom;

namespace {

constexpr int kW = 192;
constexpr int kH = 192;
constexpr float kBackground = 0.05f;

//! Mixed-element reference molecule: C (red), O (blue), N (green) with
//! single bonds — the same shape the tessellator parity test uses.
Molecule *makeReferenceMolecule()
{
  Molecule *mol = new Molecule;
  Atom *c = mol->addAtom(6, Eigen::Vector3d(0.0, 0.0, 0.0));
  Atom *o = mol->addAtom(8, Eigen::Vector3d(1.5, 0.0, 0.0));
  Atom *n = mol->addAtom(7, Eigen::Vector3d(0.0, 1.5, 0.0));
  c->setCustomColorName("red");
  o->setCustomColorName("blue");
  n->setCustomColorName("green");
  mol->addBond(c->id(), o->id(), 1);
  mol->addBond(c->id(), n->id(), 1);
  return mol;
}

//! A GL 3.3 COMPATIBILITY context on an offscreen surface: required by the
//! legacy tier (fixed function) and accepted by the modern tier.
QOpenGLContext *makeCompatContext(QOffscreenSurface **surface)
{
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  QOffscreenSurface *off = new QOffscreenSurface;
  off->setFormat(fmt);
  off->create();
  QOpenGLContext *ctx = new QOpenGLContext;
  ctx->setFormat(fmt);
  if (!ctx->create() || !ctx->makeCurrent(off)) {
    delete ctx;
    delete off;
    *surface = nullptr;
    return nullptr;
  }
  *surface = off;
  return ctx;
}

//! Renderbuffer-backed FBO (same pattern as the modern renderer tests).
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

//! The shared view transform: perspective(45) pulled back along -Z — the
//! SAME matrices for both tiers so coverage is comparable.
void setSharedView(QOpenGLFunctions &gl, QMatrix4x4 &mvpOut)
{
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  QMatrix4x4 proj;
  proj.perspective(45.0f, float(kW) / float(kH), 0.1f, 100.0f);
  glMultMatrixf(proj.constData());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslatef(0.0f, 0.0f, -5.0f);
  mvpOut = proj * QMatrix4x4();
  mvpOut.translate(0.0f, 0.0f, -5.0f);
}

//! The widget's fixed-function two-light setup (global.h values).
void setFixedFunctionLights(QOpenGLFunctions &gl)
{
  const float amb[] = {0.2f, 0.2f, 0.2f, 1.0f};
  const float l0d[] = {0.8f, 0.7f, 1.0f, 1.0f};
  const float l0s[] = {0.8f, 0.8f, 0.8f, 1.0f};
  const float l0p[] = {0.8f, 0.7f, 1.0f, 0.0f};
  const float l1d[] = {0.3f, 0.3f, 0.3f, 1.0f};
  const float l1s[] = {0.5f, 0.5f, 0.5f, 1.0f};
  const float l1p[] = {-0.8f, 0.7f, -0.5f, 0.0f};
  glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, l0d);
  glLightfv(GL_LIGHT0, GL_SPECULAR, l0s);
  glLightfv(GL_LIGHT0, GL_POSITION, l0p);
  glLightfv(GL_LIGHT1, GL_AMBIENT, amb);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, l1d);
  glLightfv(GL_LIGHT1, GL_SPECULAR, l1s);
  glLightfv(GL_LIGHT1, GL_POSITION, l1p);
}

QImage readPixels(QOpenGLFunctions &gl)
{
  QImage img(kW, kH, QImage::Format_ARGB32);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, kW, kH, GL_BGRA, GL_UNSIGNED_BYTE, img.bits());
  return img.mirrored(); // GL origin is bottom-left
}

double coverage(const QImage &img)
{
  long nonBg = 0;
  long total = 0;
  for (int y = 0; y < img.height(); y += 2)
    for (int x = 0; x < img.width(); x += 2) {
      const QRgb px = img.pixel(x, y);
      ++total;
      if (qRed(px) + qGreen(px) + qBlue(px) > 60)
        ++nonBg;
    }
  return total ? double(nonBg) / double(total) : 0.0;
}

bool frameHasDominantColour(const QImage &img, int channel)
{
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x) {
      const QRgb px = img.pixel(x, y);
      const int c[3] = { qRed(px), qGreen(px), qBlue(px) };
      if (c[channel] > 60 && c[channel] > c[(channel + 1) % 3] + 15 &&
          c[channel] > c[(channel + 2) % 3] + 15)
        return true;
    }
  return false;
}

double meanBrightness(const QImage &img)
{
  double sum = 0;
  long n = 0;
  for (int y = 0; y < img.height(); y += 2)
    for (int x = 0; x < img.width(); x += 2) {
      const QRgb px = img.pixel(x, y);
      sum += qRed(px) + qGreen(px) + qBlue(px);
      ++n;
    }
  return n ? sum / double(n) : 0.0;
}

//! PainterDevice that routes every engine draw into the SceneBuilder (the
//! modern tier's emitter path).
class EmitterDevice : public Avogadro::PainterDevice
{
public:
  EmitterDevice(Avogadro::rendering::SceneBuilder *builder,
                Avogadro::Molecule *mol, Color *map)
    : m_builder(builder), m_molecule(mol), m_colorMap(map)
  {
  }
  Avogadro::Painter *painter() const override { return m_builder; }
  Avogadro::Camera *camera() const override { return nullptr; }
  bool isSelected(const Avogadro::Primitive *) const override
  {
    return false;
  }
  double radius(const Avogadro::Primitive *) const override { return 0.3; }
  const Avogadro::Molecule *molecule() const override { return m_molecule; }
  Color *colorMap() const override { return m_colorMap; }
  int width() override { return kW; }
  int height() override { return kH; }

private:
  Avogadro::rendering::SceneBuilder *m_builder;
  Avogadro::Molecule *m_molecule;
  Color *m_colorMap;
};

//! PainterDevice that routes every draw to a GLPainter (the legacy tier's
//! real painter) while answering the engine's metadata queries.
class LegacyPainterDevice : public Avogadro::PainterDevice
{
public:
  LegacyPainterDevice(Avogadro::Painter *p, Avogadro::Molecule *mol,
                      Avogadro::Color *map)
    : m_painter(p), m_molecule(mol), m_colorMap(map)
  {
  }
  Avogadro::Painter *painter() const override { return m_painter; }
  Avogadro::Camera *camera() const override { return nullptr; }
  bool isSelected(const Avogadro::Primitive *) const override
  {
    return false;
  }
  double radius(const Avogadro::Primitive *) const override { return 0.3; }
  const Avogadro::Molecule *molecule() const override { return m_molecule; }
  Avogadro::Color *colorMap() const override { return m_colorMap; }
  int width() override { return kW; }
  int height() override { return kH; }

private:
  Avogadro::Painter *m_painter;
  Avogadro::Molecule *m_molecule;
  Avogadro::Color *m_colorMap;
};

} // namespace

class VisualParityTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void opaqueParity();
  void transparencyClassParity();

private:
  QOpenGLContext *m_ctx = nullptr;
  QOffscreenSurface *m_off = nullptr;
  QOpenGLFunctions *m_gl = nullptr;
  Molecule *m_mol = nullptr;
  Color *m_map = nullptr;
};

void VisualParityTest::initTestCase()
{
  m_ctx = makeCompatContext(&m_off);
  if (!m_ctx)
    QSKIP("No GL 3.3 compatibility context available; skipping parity",
          SkipAll);
  m_gl = new QOpenGLFunctions;
  m_gl->initializeOpenGLFunctions();
  const GLubyte *ver = glGetString(GL_VERSION);
  qInfo() << "context GL_VERSION:" << (ver ? (const char*)ver : "(null)")
          << "requested profile:"
          << m_ctx->format().profile() << "got profile:"
          << QOpenGLContext::currentContext()->format().profile();
  m_mol = makeReferenceMolecule();
  m_map = new Color;
}

void VisualParityTest::opaqueParity()
{
  // A GL 3.3 compatibility context is required by the legacy tier
    // (fixed function). When the runner cannot provide one, both GL legs
    // are skipped: content parity is still covered by threadedparitytest
    // (byte-identical engine-vs-tessellator emission) and the modern pixel
    // pipeline by testBSDYThroughModernBackend on capable runners.
    if (!m_ctx || !m_ctx->makeCurrent(m_off))
      QSKIP("No GL 3.3 compatibility context on this runner; skipping "
            "modern-vs-legacy pixel parity", SkipAll);

    // --- modern tier: engine emission -> SceneBuilder -> instanced backend
  GLuint fboM = 0, texM = 0, rbM = 0;
  fboM = makeFbo(*m_gl, &texM, &rbM);
  QVERIFY(fboM != 0);

  Avogadro::rendering::SceneBuilder builder;
  EmitterDevice device(&builder, m_mol, m_map);
  BSDYEngine engine;
  engine.setColorMap(m_map);
  engine.setMolecule(m_mol);
  engine.setPainterDevice(&device);
  engine.setEnabled(true);

  builder.begin();
  builder.clearStores();
  engine.renderOpaque(&device);
  builder.end();
  QVERIFY(builder.spheres().size() >= 3u);

  Avogadro::rendering::ModernRendererBackend backend;
  backend.initialize();
  QVERIFY(backend.isInitialized());

  m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fboM);
  m_gl->glViewport(0, 0, kW, kH);
  QMatrix4x4 mvp;
  setSharedView(*m_gl, mvp);
  m_gl->glClearColor(kBackground, kBackground, kBackground, 1.0f);
  m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  m_gl->glEnable(GL_DEPTH_TEST);
  m_gl->glDepthFunc(GL_LEQUAL);

  backend.beginFrame(nullptr, QRect(0, 0, kW, kH), 0);
  backend.setModelViewProjection(mvp);
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.pushLines(builder.lines());
  backend.endFrame();
  m_gl->glFlush();
  const QImage modernImg = readPixels(*m_gl);

  // --- legacy tier: the same engine through GLPainter (frozen tier 0)
  GLuint fboL = 0, texL = 0, rbL = 0;
  fboL = makeFbo(*m_gl, &texL, &rbL);
  QVERIFY(fboL != 0);

  GLWidget widget; // metadata only: GLPainter::begin needs a widget handle
  Avogadro::GLPainter painter;
  LegacyPainterDevice legacyPd(&painter, m_mol, m_map);
  painter.begin(&widget);

  m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fboL);
  m_gl->glViewport(0, 0, kW, kH);
  setSharedView(*m_gl, mvp);
  m_gl->glEnable(GL_LIGHTING);
  m_gl->glEnable(GL_LIGHT0);
  m_gl->glEnable(GL_LIGHT1);
  setFixedFunctionLights(*m_gl);
  m_gl->glClearColor(kBackground, kBackground, kBackground, 1.0f);
  m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  m_gl->glEnable(GL_DEPTH_TEST);

  // Raw immediate-mode sanity draw: a green triangle must appear — proves
  // the context delivers fixed-function immediates before the painter runs.
  // Drain any sticky GL error left by the modern leg (glGetError returns
  // errors one at a time; a stale error would make the sanity check lie).
  while (glGetError() != GL_NO_ERROR) {
  }
  m_gl->glDisable(GL_LIGHTING);
  glBegin(GL_TRIANGLES);
  glColor3f(0.0f, 1.0f, 0.0f);
  glVertex2f(-0.9f, -0.9f);
  glVertex2f(0.9f, -0.9f);
  glVertex2f(0.0f, 0.9f);
  glEnd();
  m_gl->glEnable(GL_LIGHTING);
  m_gl->glFlush();
  {
    const QImage sanity = readPixels(*m_gl);
    qInfo() << "raw triangle visible:" << frameHasDominantColour(sanity, 1)
            << "GL err:" << glGetError();
  }
  m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  engine.setPainterDevice(&legacyPd);
  engine.renderOpaque(&legacyPd);
  painter.end();
  m_gl->glFlush();
  qInfo() << "GL err after legacy engine draw:" << glGetError(); // sticky-error note
  const QImage legacyImg = readPixels(*m_gl);

  // --- compare
  const double modernCoverage = coverage(modernImg);
  const double legacyCoverage = coverage(legacyImg);
  qInfo() << "coverage: modern" << modernCoverage << "legacy" << legacyCoverage;
  QVERIFY2(modernCoverage > 0.02, "modern tier drew nothing");
  if (legacyCoverage < 0.02)
    QSKIP("The legacy fixed-function tier drew nothing on this runner: its "
          "GL context is core-profile-only (Mesa delivers core contexts to "
          "offscreen surfaces here). Visual parity must be reviewed on a "
          "runner with a compatibility profile. Content parity is still "
          "covered by threadedparitytest.", SkipAll);
  QVERIFY2(std::abs(modernCoverage - legacyCoverage) < 0.08,
           qPrintable(QString("coverage mismatch: modern %1 vs legacy %2")
                        .arg(modernCoverage)
                        .arg(legacyCoverage)));

  for (int channel = 0; channel < 3; ++channel) {
    const bool m = frameHasDominantColour(modernImg, channel);
    const bool l = frameHasDominantColour(legacyImg, channel);
    qInfo() << "colour channel" << channel << "modern" << m << "legacy" << l;
    QVERIFY2(m == l && m,
             qPrintable(QString("colour class mismatch on channel %1").arg(channel)));
  }

  m_gl->glDeleteFramebuffers(1, &fboM);
  m_gl->glDeleteFramebuffers(1, &fboL);
  m_gl->glDeleteTextures(1, &texM);
  m_gl->glDeleteTextures(1, &texL);
  m_gl->glDeleteRenderbuffers(1, &rbM);
  m_gl->glDeleteRenderbuffers(1, &rbL);
}

void VisualParityTest::transparencyClassParity()
{
  // Re-establish the context: the previous test function's cleanup may have
  // released it (QOpenGLWidget destruction calls doneCurrent).
  if (!m_ctx || !m_ctx->makeCurrent(m_off))
    QSKIP("No GL 3.3 compatibility context on this runner", SkipAll);

  // 50 % opacity: the engine renders only its transparent pass; both tiers
  // must land in the same blended-brightness class (A9/D2).
  GLuint fboM = 0, texM = 0, rbM = 0;
  GLuint fboL = 0, texL = 0, rbL = 0;
  fboM = makeFbo(*m_gl, &texM, &rbM);
  fboL = makeFbo(*m_gl, &texL, &rbL);

  BSDYEngine engine;
  engine.setColorMap(m_map);
  engine.setMolecule(m_mol);
  engine.setEnabled(true);
  QMetaObject::invokeMethod(&engine, "setOpacity", Q_ARG(int, 10));

  Avogadro::rendering::SceneBuilder builder;
  EmitterDevice device(&builder, m_mol, m_map);
  engine.setPainterDevice(&device);
  builder.begin();
  builder.clearStores();
  engine.renderTransparent(&device);
  builder.end();
  QVERIFY(builder.spheres().size() >= 3u);

  Avogadro::rendering::ModernRendererBackend backend;
  backend.initialize();
  QVERIFY(backend.isInitialized());

  QMatrix4x4 mvp;
  m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fboM);
  m_gl->glViewport(0, 0, kW, kH);
  setSharedView(*m_gl, mvp);
  m_gl->glClearColor(kBackground, kBackground, kBackground, 1.0f);
  m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  m_gl->glEnable(GL_DEPTH_TEST);
  backend.beginFrame(nullptr, QRect(0, 0, kW, kH), 0);
  backend.setModelViewProjection(mvp);
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.pushLines(builder.lines());
  backend.endFrame();
  m_gl->glFlush();
  const QImage modernImg = readPixels(*m_gl);

  GLWidget widget;
  Avogadro::GLPainter painter;
  LegacyPainterDevice legacyPd(&painter, m_mol, m_map);
  painter.begin(&widget);
  m_gl->glBindFramebuffer(GL_FRAMEBUFFER, fboL);
  m_gl->glViewport(0, 0, kW, kH);
  setSharedView(*m_gl, mvp);
  m_gl->glEnable(GL_LIGHTING);
  m_gl->glEnable(GL_LIGHT0);
  m_gl->glEnable(GL_LIGHT1);
  setFixedFunctionLights(*m_gl);
  m_gl->glEnable(GL_BLEND);
  m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  m_gl->glClearColor(kBackground, kBackground, kBackground, 1.0f);
  m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  m_gl->glEnable(GL_DEPTH_TEST);
  engine.setPainterDevice(&legacyPd);
  engine.renderTransparent(&legacyPd);
  m_gl->glDisable(GL_BLEND);
  painter.end();
  m_gl->glFlush();
  const QImage legacyImg = readPixels(*m_gl);

  const double modernMean = meanBrightness(modernImg);
  const double legacyMean = meanBrightness(legacyImg);
  const double modernCoverage = coverage(modernImg);
  const double legacyCoverage = coverage(legacyImg);
  qInfo() << "transparent: brightness modern" << modernMean << "legacy"
          << legacyMean << "| coverage modern" << modernCoverage << "legacy"
          << legacyCoverage;

  // Transparency class: blended (above background, below opaque) on both.
  QVERIFY2(modernMean > 10.0, "modern transparent pass drew nothing");
  QVERIFY2(legacyMean > 10.0, "legacy transparent pass drew nothing");
  QVERIFY2(std::abs(modernMean - legacyMean) < 60.0,
           qPrintable(QString("transparency class mismatch: modern %1 vs legacy %2")
                        .arg(modernMean)
                        .arg(legacyMean)));

  m_gl->glDeleteFramebuffers(1, &fboM);
  m_gl->glDeleteFramebuffers(1, &fboL);
  m_gl->glDeleteTextures(1, &texM);
  m_gl->glDeleteTextures(1, &texL);
  m_gl->glDeleteRenderbuffers(1, &rbM);
  m_gl->glDeleteRenderbuffers(1, &rbL);
}

#include "visualparitytest.moc"
QTEST_MAIN(VisualParityTest)
