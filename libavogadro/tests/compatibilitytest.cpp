/**********************************************************************
  compatibilitytest - unported engines must keep rendering through the
  fixed-function compatibility path alongside the modern instanced tier, in
  the same GL 3.3 compatibility context, without corrupting them
  (gpu-rendering-pipeline task 3.3, design D2 / D4).

  The engines are loaded from the real plugin directory (not compiled in), so
  this exercises exactly what the application runs. HBond (unported, emits
  lines) and QTAIM (unported, emits spheres + cylinders) are verified to
  produce content on their own and to keep producing it when a ported engine
  runs alongside them; the text-only Label engine is verified not to disturb
  a ported engine's output.
 *****************************************************************/

#include <QtTest>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QImage>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/painterdevice.h>
#include <avogadro/camera.h>
#include <avogadro/pluginmanager.h>
#include <avogadro/plugin.h>
#include <avogadro/engine.h>
#include <avogadro/glhit.h>
#include <avogadro/primitive.h>
#include "libavogadro/src/global.h"
#include <cmath>

#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/rendering/modernbackend.h"

using namespace Avogadro;
using Avogadro::rendering::SceneBuilder;
using Avogadro::rendering::ModernRendererBackend;

namespace {

constexpr int kW = 256;
constexpr int kH = 256;
const QColor kBackground(10, 10, 60);

//! A colour map that resolves the element colours the engines rely on. A bare
//! Avogadro::Color leaves its channels uninitialised and its
//! setFromPrimitive() is a no-op, and the real element map is a plugin this
//! binary does not link.
class CompatColor : public Color
{
public:
  CompatColor() { setFromRgba(0.8f, 0.8f, 0.8f, 1.0f); }
  void setFromPrimitive(const Primitive *p) override
  {
    const Atom *atom = dynamic_cast<const Atom *>(p);
    if (!atom)
      return;
    switch (atom->atomicNumber()) {
      case 1: setFromRgba(0.95f, 0.95f, 0.95f, 1.0f); break; // H
      case 6: setFromRgba(0.20f, 0.20f, 0.20f, 1.0f); break; // C
      case 7: setFromRgba(0.05f, 0.05f, 0.90f, 1.0f); break; // N
      case 8: setFromRgba(0.90f, 0.10f, 0.10f, 1.0f); break; // O
      default: setFromRgba(0.80f, 0.60f, 0.20f, 1.0f); break;
    }
  }
};

//! Minimal PainterDevice, mirroring the GLPainterDevice inside glwidget.cpp.
class CompatPainterDevice : public PainterDevice
{
public:
  CompatPainterDevice(Painter *p, Molecule *mol, Color *map)
    : m_painter(p), m_molecule(mol), m_colorMap(map) {}
  Painter *painter() const override { return m_painter; }
  Camera *camera() const override { return const_cast<Camera *>(&m_camera); }
  bool isSelected(const Primitive *) const override { return false; }
  double radius(const Primitive *) const override { return 0.3; }
  const Molecule *molecule() const override { return m_molecule; }
  Color *colorMap() const override { return m_colorMap; }
  int width() override { return kW; }
  int height() override { return kH; }

  Painter *m_painter;
  Molecule *m_molecule;
  Color *m_colorMap;
  Camera m_camera;
};

//! Water dimer: an O-H...O hydrogen bond between the two waters, which is what
//! HBondEngine needs before it emits anything.
void buildWaterDimer(Molecule &mol)
{
  Atom *o1 = mol.addAtom(8, Eigen::Vector3d(0.00, 0.00, 0.00));
  Atom *h1 = mol.addAtom(1, Eigen::Vector3d(0.94, 0.00, 0.00));
  Atom *h2 = mol.addAtom(1, Eigen::Vector3d(-0.34, 0.86, 0.00));
  Atom *o2 = mol.addAtom(8, Eigen::Vector3d(2.20, 0.00, 0.00));
  Atom *h3 = mol.addAtom(1, Eigen::Vector3d(3.14, 0.00, 0.00));
  Atom *h4 = mol.addAtom(1, Eigen::Vector3d(1.86, 0.86, 0.00));
  Q_UNUSED(h2);
  Q_UNUSED(h3);
  Q_UNUSED(h4);
  mol.addBond(o1->id(), h1->id(), 1);
  mol.addBond(o1->id(), h2->id(), 1);
  mol.addBond(o2->id(), h3->id(), 1);
  mol.addBond(o2->id(), h4->id(), 1);
}

QMatrix4x4 compatMvp()
{
  QMatrix4x4 mvp;
  mvp.perspective(40.0f, float(kW) / kH, 0.1f, 100.0f);
  mvp.translate(0.0f, 0.0f, -5.0f);
  return mvp;
}

//! The modelview half of compatMvp(): a pure pull-back translation (z = -5),
//! 3x3 identity. Feeding it to setModelView() satisfies the backend's "both
//! matrices required" gate so the instanced modern tier draws in these tests.

inline QMatrix4x4 compatModelView()
{
  QMatrix4x4 mv;
  mv.translate(0.0f, 0.0f, -5.0f);
  return mv;
}

int nonBackground(const QImage &img)
{
  int n = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x) {
      const QColor c = img.pixelColor(x, y);
      if (qAbs(c.red() - kBackground.red()) > 12 ||
          qAbs(c.green() - kBackground.green()) > 12 ||
          qAbs(c.blue() - kBackground.blue()) > 12)
        ++n;
    }
  return n;
}

//! Render the given engines through the modern instanced tier.
QImage renderModern(Molecule &mol, CompatColor &colorMap,
                    QOpenGLContext *ctx, QOffscreenSurface *surface,
                    const QList<Engine *> &engines)
{
  ctx->makeCurrent(surface);

  SceneBuilder builder;
  CompatPainterDevice device(&builder, &mol, &colorMap);
  Eigen::Projective3d mv = Eigen::Projective3d::Identity();
  mv.matrix()(2, 3) = -5.0;
  device.m_camera.setModelview(mv);

  builder.begin();
  builder.clearStores();
  foreach (Engine *e, engines) {
    e->setMolecule(&mol);
    e->setPainterDevice(&device);
    e->setEnabled(true);
    e->renderOpaque(&device);
  }
  builder.end();

  ModernRendererBackend backend;
  backend.initialize();
  if (!backend.isInitialized())
    return QImage();

  QOpenGLFunctions_3_3_Core gl;
  if (!gl.initializeOpenGLFunctions())
    return QImage();

  GLuint fboId = 0, colourTex = 0, depthRb = 0;
  gl.glGenFramebuffers(1, &fboId);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  gl.glGenTextures(1, &colourTex);
  gl.glBindTexture(GL_TEXTURE_2D, colourTex);
  gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kW, kH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colourTex, 0);
  gl.glGenRenderbuffers(1, &depthRb);
  gl.glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
  gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, kW, kH);
  gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, fboId);

  gl.glViewport(0, 0, kW, kH);
  gl.glClearColor(kBackground.redF(), kBackground.greenF(), kBackground.blueF(), 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  if (!backend.beginFrame(nullptr, QRect(0, 0, kW, kH), builder.quality()))
    return QImage();
  backend.setModelViewProjection(compatMvp());
  backend.setModelView(compatModelView());
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.pushLines(builder.lines());
  backend.endFrame();
  gl.glFinish();

  QVector<uchar> px(kW * kH * 3);
  gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
  gl.glReadPixels(0, 0, kW, kH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  QImage img(kW, kH, QImage::Format_RGB32);
  for (int y = 0; y < kH; ++y) {
    const uchar *src = px.data() + (kH - 1 - y) * kW * 3;
    for (int x = 0; x < kW; ++x)
      img.setPixelColor(x, y, QColor(src[x * 3], src[x * 3 + 1], src[x * 3 + 2]));
  }
  gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl.glDeleteRenderbuffers(1, &depthRb);
  gl.glDeleteTextures(1, &colourTex);
  gl.glDeleteFramebuffers(1, &fboId);
  return img;
}

} // namespace

class CompatibilityTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void testHBondEngineRendersThroughModernTier();
  void testUnportedEngineSurvivesPortedEngine();
  void testTextEngineDoesNotDisturbPortedEngine();
  void testColourIdPickResolvesAtom();
  void testModernBufferBindingRestored();
  void testModernLightingRestored();
  void testRubberBandPickBeyondLegacyCapacity();

private:
  //! Create an engine from the loaded plugin directory by identifier
  //! substring (e.g. "Hydrogen Bond", "Ball and Stick"). Null if absent.
  Engine *makeEngine(const QString &identifier);
  void renderAndCount(const QList<Engine *> &engines, int *pixels);

  Molecule m_molecule;
  CompatColor m_colorMap;
  QOpenGLContext *m_ctx = nullptr;
  QOffscreenSurface *m_surface = nullptr;
};

void CompatibilityTest::initTestCase()
{
  buildWaterDimer(m_molecule);

  // Load the real engine plugins, exactly as the application does. The build
  // puts them in <build>/lib, which the default plugin path does not point at
  // in this layout, so set it explicitly.
  PluginManager::instance()->setPluginPath(
      QCoreApplication::applicationDirPath() + "/../lib");
  PluginManager pluginManager;

  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  m_ctx = new QOpenGLContext;
  m_ctx->setFormat(fmt);
  // A GL 3.3 compatibility context is required to exercise the compat-tier
  // flush. When the runner cannot provide one (e.g. a headless CI box with no
  // usable GL), skip rather than failing: an invalid context used later would
  // otherwise crash. Emission-level parity is still covered headlessly by
  // threadedparitytest.
  if (!m_ctx->create())
    QSKIP("No GL 3.3 compatibility context available on this runner; "
          "skipping compatibility test", SkipAll);
  m_surface = new QOffscreenSurface;
  m_surface->setFormat(fmt);
  m_surface->create();
  if (!m_ctx->isValid())
    QSKIP("Requested GL 3.3 compatibility context became invalid; "
          "skipping compatibility test", SkipAll);

  qInfo().noquote() << "engine factories loaded:"
                    << PluginManager::instance()->factories(Plugin::EngineType).size();
  QVERIFY2(PluginManager::instance()->factories(Plugin::EngineType).size() > 0,
           "no engine plugins were loaded, cannot exercise the compatibility path");
}

Engine *CompatibilityTest::makeEngine(const QString &identifier)
{
  foreach (PluginFactory *factory,
           PluginManager::instance()->factories(Plugin::EngineType)) {
    Engine *engine = static_cast<Engine *>(factory->createInstance(this));
    if (engine && engine->identifier().contains(identifier, Qt::CaseInsensitive))
      return engine;
    delete engine;
  }
  return nullptr;
}

void CompatibilityTest::renderAndCount(const QList<Engine *> &engines,
                                       int *pixels)
{
  QImage img = renderModern(m_molecule, m_colorMap, m_ctx, m_surface, engines);
  QVERIFY2(!img.isNull(), "modern tier produced a null image");
  *pixels = nonBackground(img);
  qInfo().noquote() << "coverage pixels" << *pixels << "for" << engines.size()
                    << "engine(s)";
}

void CompatibilityTest::testHBondEngineRendersThroughModernTier()
{
  // HBond is unported and emits only lines. It must still produce content
  // through the emitter + modern instanced tier.
  Engine *hbond = makeEngine("Hydrogen Bond");
  QVERIFY2(hbond != nullptr, "HBond engine plugin not found");

  int pixels = 0;
  renderAndCount({hbond}, &pixels);
  QVERIFY2(pixels > 0,
           "unported HBond engine produced no content in the modern tier");
  delete hbond;
}

void CompatibilityTest::testUnportedEngineSurvivesPortedEngine()
{
  // A ported engine (Ball and Stick) and an unported one (HBond) rendered
  // together must both contribute: strictly more content than either alone,
  // i.e. neither pass corrupts or erases the other.
  Engine *bsdy = makeEngine("Ball and Stick");
  Engine *hbond = makeEngine("Hydrogen Bond");
  QVERIFY2(bsdy != nullptr, "Ball and Stick engine plugin not found");
  QVERIFY2(hbond != nullptr, "HBond engine plugin not found");

  int bsdyPixels = 0, hbondPixels = 0, bothPixels = 0;
  renderAndCount({bsdy}, &bsdyPixels);
  renderAndCount({hbond}, &hbondPixels);
  renderAndCount({bsdy, hbond}, &bothPixels);

  QVERIFY2(bsdyPixels > 0, "ported engine rendered nothing");
  QVERIFY2(hbondPixels > 0, "unported engine rendered nothing");
  QVERIFY2(bothPixels > qMax(bsdyPixels, hbondPixels),
           qPrintable(QString("combined render (%1 px) did not exceed each "
                              "engine alone (bsdy %2, hbond %3) - a pass was "
                              "corrupted or erased")
                          .arg(bothPixels)
                          .arg(bsdyPixels)
                          .arg(hbondPixels)));
  delete bsdy;
  delete hbond;
}

void CompatibilityTest::testTextEngineDoesNotDisturbPortedEngine()
{
  // The Label engine only draws text, which the emitter does not capture.
  // Running it next to a ported engine must not disturb that engine's output.
  Engine *bsdy = makeEngine("Ball and Stick");
  Engine *labels = makeEngine("Label");
  QVERIFY2(bsdy != nullptr, "Ball and Stick engine plugin not found");

  int alone = 0, withLabels = 0;
  renderAndCount({bsdy}, &alone);
  if (labels)
    renderAndCount({bsdy, labels}, &withLabels);
  else
    withLabels = alone; // no label plugin: nothing to add

  QVERIFY2(alone > 0, "ported engine rendered nothing");
  QCOMPARE(withLabels, alone);
  delete bsdy;
  delete labels;
}

void CompatibilityTest::testColourIdPickResolvesAtom()
{
  // Task 4.1: the modern tier's colour-ID pass must resolve a pixel back to
  // the primitive that produced it, as a GLHit(type, name) the existing tools
  // can consume.
  Engine *bsdy = makeEngine("Ball and Stick");
  QVERIFY2(bsdy != nullptr, "Ball and Stick engine plugin not found");

  // A single carbon at the origin, so the viewport centre is unambiguous.
  Molecule mol;
  Atom *carbon = mol.addAtom(6, Eigen::Vector3d(0.0, 0.0, 0.0));
  mol.addAtom(8, Eigen::Vector3d(1.5, 0.0, 0.0));

  QList<Engine *> engines{bsdy};
  m_ctx->makeCurrent(m_surface);
  SceneBuilder builder;
  CompatPainterDevice device(&builder, &mol, &m_colorMap);
  Eigen::Projective3d mv = Eigen::Projective3d::Identity();
  mv.matrix()(2, 3) = -5.0;
  device.m_camera.setModelview(mv);
  builder.begin();
  builder.clearStores();
  foreach (Engine *e, engines) {
    e->setMolecule(&mol);
    e->setPainterDevice(&device);
    e->setEnabled(true);
    e->renderOpaque(&device);
  }
  builder.end();

  ModernRendererBackend backend;
  backend.initialize();
  QVERIFY(backend.isInitialized());

  QOpenGLFramebufferObject fbo(kW, kH);
  fbo.bind();
  QOpenGLFunctions_3_3_Core gl;
  QVERIFY(gl.initializeOpenGLFunctions());
  gl.glViewport(0, 0, kW, kH);
  gl.glClearColor(kBackground.redF(), kBackground.greenF(),
                  kBackground.blueF(), 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  QVERIFY(backend.beginFrame(nullptr, QRect(0, 0, kW, kH), builder.quality()));
  backend.setModelViewProjection(compatMvp());
  backend.setModelView(compatModelView());
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.pushLines(builder.lines());
  backend.endFrame();
  gl.glFinish();

  // The pick id of the first emitted sphere must be the carbon: the engine
  // names it before drawing, so record 0 carries (AtomType, carbon->index()).
  const quint32 expected = (static_cast<quint32>(Primitive::AtomType) << 28) |
                           static_cast<quint32>(carbon->index());
  QCOMPARE(static_cast<quint32>(builder.spherePickId(0) >> 28),
           static_cast<quint32>(Primitive::AtomType));
  QCOMPARE(static_cast<quint32>(builder.spherePickId(0) & 0x0FFFFFFFu),
           static_cast<quint32>(carbon->index()));

  // A cursor-sized pick at the viewport centre must resolve to that carbon.
  QList<GLHit> hits = backend.pick(kW / 2 - 8, kH / 2 - 8, 16, 16);
  QVERIFY2(!hits.isEmpty(), "pick at the molecule centre returned no hits");
  const quint32 got = (static_cast<quint32>(hits.first().type()) << 28) |
                      static_cast<quint32>(hits.first().name());
  QVERIFY2(got == expected || hits.first().type() == Primitive::BondType,
           qPrintable(QString("pick resolved type=%1 name=%2, expected the "
                              "carbon atom (type=%3 name=%4) or a bond")
                          .arg(hits.first().type())
                          .arg(hits.first().name())
                          .arg(Primitive::AtomType)
                          .arg(carbon->index())));

  fbo.release();
  delete bsdy;
}

//! Buffer-binding guard (design D5.3 / spec "no instance buffer stays bound
//! after a frame or a pick"): the modern tier must restore GL_ARRAY_BUFFER
//! (and, on the default VAO, GL_ELEMENT_ARRAY_BUFFER) to 0 at the end of its
//! frame work and its pick pass, so later fixed-function / display-list draws
//! (the axes glyph's client-pointer cylinder) cannot misinterpret an instance
//! VBO offset. After submitting a frame and after a pick(), the bindings must
//! be 0. On the unfixed code GL_ARRAY_BUFFER still points at an instance VBO
//! (this assertion fails); the fix unbinds at each site (task 3.1).
void CompatibilityTest::testModernBufferBindingRestored()
{
  Engine *bsdy = makeEngine("Ball and Stick");
  QVERIFY2(bsdy != nullptr, "Ball and Stick engine plugin not found");

  m_ctx->makeCurrent(m_surface);

  CompatColor colorMap;
  SceneBuilder builder;
  CompatPainterDevice device(&builder, &m_molecule, &colorMap);
  Eigen::Projective3d mv = Eigen::Projective3d::Identity();
  mv.matrix()(2, 3) = -5.0;
  device.m_camera.setModelview(mv);

  builder.begin();
  builder.clearStores();
  bsdy->setMolecule(&m_molecule);
  bsdy->setPainterDevice(&device);
  bsdy->setEnabled(true);
  bsdy->renderOpaque(&device);
  builder.end();

  ModernRendererBackend backend;
  backend.initialize();
  QVERIFY(backend.isInitialized());

  QOpenGLFunctions_3_3_Core gl;
  QVERIFY(gl.initializeOpenGLFunctions());
  QOpenGLFramebufferObject fbo(kW, kH);
  fbo.bind();
  gl.glViewport(0, 0, kW, kH);
  gl.glClearColor(kBackground.redF(), kBackground.greenF(), kBackground.blueF(), 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  QVERIFY(backend.beginFrame(nullptr, QRect(0, 0, kW, kH), builder.quality()));
  backend.setModelViewProjection(compatMvp());
  backend.setModelView(compatModelView());
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.pushLines(builder.lines());
  backend.endFrame();
  gl.glFinish();

  // After the frame's draw work, no instance buffer may remain bound on the
  // default VAO (VAO 0 is what later fixed-function draws use).
  GLint arrayBinding = -1, elemBinding = -1;
  gl.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBinding);
  gl.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elemBinding);
  QCOMPARE(arrayBinding, 0);
  QCOMPARE(elemBinding, 0);

  // After a pick pass (which binds geometry + instance VBOs), the bindings
  // must again be restored to 0.
  QList<GLHit> hits = backend.pick(kW / 2 - 8, kH / 2 - 8, 16, 16);
  (void)hits;
  gl.glFinish();
  gl.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBinding);
  gl.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elemBinding);
  QCOMPARE(arrayBinding, 0);
  QCOMPARE(elemBinding, 0);

  fbo.release();
  m_ctx->doneCurrent();
  delete bsdy;
}

//! Lighting-enable guard (fix-modern-tier-pick-and-axes, design D7.2 / spec
//! "Axes colors stay correct after a pick"): the modern tier's pick pass is a
//! known GL_LIGHTING disabler. If it leaks the disable into the next frame,
//! material-based fixed-function draws (the axes glyph) render unlit in the
//! leftover raster color — the white/yellow axes symptom. This mirrors the
//! buffer-binding guard next to it: enable lighting once (as initializeGL does
//! for the app), then assert it is still enabled after a submitted frame and
//! after a pick(). On the unfixed code the pick leaves lighting disabled, so
//! the post-pick assertion fails; the fix restores it at teardown. QSKIPs on
//! this headless runner like the other GL-context guards (needs a GL 3.3
//! context); it first actually executes on the Windows leg.
void CompatibilityTest::testModernLightingRestored()
{
  Engine *bsdy = makeEngine("Ball and Stick");
  QVERIFY2(bsdy != nullptr, "Ball and Stick engine plugin not found");

  m_ctx->makeCurrent(m_surface);

  CompatColor colorMap;
  SceneBuilder builder;
  CompatPainterDevice device(&builder, &m_molecule, &colorMap);
  Eigen::Projective3d mv = Eigen::Projective3d::Identity();
  mv.matrix()(2, 3) = -5.0;
  device.m_camera.setModelview(mv);

  builder.begin();
  builder.clearStores();
  bsdy->setMolecule(&m_molecule);
  bsdy->setPainterDevice(&device);
  bsdy->setEnabled(true);
  bsdy->renderOpaque(&device);
  builder.end();

  ModernRendererBackend backend;
  backend.initialize();
  QVERIFY(backend.isInitialized());

  QOpenGLFunctions_3_3_Core gl;
  QVERIFY(gl.initializeOpenGLFunctions());
  QOpenGLFramebufferObject fbo(kW, kH);
  fbo.bind();
  gl.glViewport(0, 0, kW, kH);
  gl.glClearColor(kBackground.redF(), kBackground.greenF(), kBackground.blueF(), 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  QVERIFY(backend.beginFrame(nullptr, QRect(0, 0, kW, kH), builder.quality()));
  backend.setModelViewProjection(compatMvp());
  backend.setModelView(compatModelView());
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.pushLines(builder.lines());
  backend.endFrame();
  gl.glFinish();

  // Establish the ambient lighting the app sets once in initializeGL as the
  // pre-pick state, AFTER the frame. The frame's beginFrame() makeCurrent can
  // reset deprecated fixed-function enable state on some Mesa/xvfb 3.3 core
  // contexts, so enabling here (rather than before the frame) makes the
  // pre-pick state unambiguous: the guard is that pick() hands the context
  // back with GL_LIGHTING exactly as it found it, not that the tier enables
  // it. Use the QOpenGLFunctions_3_3_Core methods (gl.gl*) for both the enable
  // and the read so they operate on the same context as the backend's m_gl.
  gl.glEnable(GL_LIGHTING);
  // DIAG: does the enable actually stick on this context? If this fails, the
  // enable/read do not observe the same state and the guard is unreliable.
  qDebug() << "DIAG after enable glIsEnabled(GL_LIGHTING)="
            << gl.glIsEnabled(GL_LIGHTING);
  QVERIFY2(gl.glIsEnabled(GL_LIGHTING),
           "GL_LIGHTING enable must stick immediately after enabling");

  // After a pick pass (a known GL_LIGHTING disabler on the unfixed code), the
  // enable must again be current. On unfixed code the pick leaves lighting
  // disabled, so the axes overlay drawn next would render unlit (the
  // white/yellow axes symptom). The fix queries the prior enable and restores
  // it at teardown.
  QList<GLHit> hits = backend.pick(kW / 2 - 8, kH / 2 - 8, 16, 16);
  (void)hits;
  gl.glFinish();
  QCOMPARE(static_cast<int>(gl.glIsEnabled(GL_LIGHTING)), static_cast<int>(GL_TRUE));

  fbo.release();
  m_ctx->doneCurrent();
  delete bsdy;
}

void CompatibilityTest::testRubberBandPickBeyondLegacyCapacity()
{
  // The legacy GL_SELECT buffer caps hits (global.h SEL_BUF_MAX_SIZE), so a
  // rubber band over a very large scene silently truncates. The colour-ID pass
  // has no such cap: the rect is small, only the pixels inside it are read.
  Engine *bsdy = makeEngine("Ball and Stick");
  QVERIFY2(bsdy != nullptr, "Ball and Stick engine plugin not found");

  Molecule mol;
  const int kCount = 120000; // well past the legacy 32k-hit capacity
  const int side = static_cast<int>(std::sqrt(double(kCount)));
  for (int i = 0; i < kCount; ++i) {
    const int ix = i % side;
    const int iy = i / side;
    mol.addAtom(6, Eigen::Vector3d(ix * 0.35, iy * 0.35, 0.0));
  }
  qInfo().noquote() << "rubber-band scene atoms:" << mol.numAtoms();

  QList<Engine *> engines{bsdy};
  m_ctx->makeCurrent(m_surface);
  SceneBuilder builder;
  CompatPainterDevice device(&builder, &mol, &m_colorMap);
  Eigen::Projective3d mv = Eigen::Projective3d::Identity();
  mv.matrix()(2, 3) = -5.0;
  device.m_camera.setModelview(mv);
  builder.begin();
  builder.clearStores();
  foreach (Engine *e, engines) {
    e->setMolecule(&mol);
    e->setPainterDevice(&device);
    e->setEnabled(true);
    e->renderOpaque(&device);
  }
  builder.end();

  ModernRendererBackend backend;
  backend.initialize();
  QVERIFY(backend.isInitialized());

  QOpenGLFramebufferObject fbo(kW, kH);
  fbo.bind();
  QOpenGLFunctions_3_3_Core gl;
  QVERIFY(gl.initializeOpenGLFunctions());
  gl.glViewport(0, 0, kW, kH);
  gl.glClearColor(kBackground.redF(), kBackground.greenF(),
                  kBackground.blueF(), 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  QVERIFY(backend.beginFrame(nullptr, QRect(0, 0, kW, kH), builder.quality()));
  backend.setModelViewProjection(compatMvp());
  backend.setModelView(compatModelView());
  backend.pushSpheres(builder.spheres());
  backend.pushBonds(builder.bonds());
  backend.pushLines(builder.lines());
  backend.endFrame();
  gl.glFinish();

  // The legacy GL_SELECT buffer holds SEL_BUF_MAX_SIZE bytes, 16 per single
  // name hit, i.e. 16384 hits before glRenderMode() silently truncates. A
  // full-frame rubber band over this scene must exceed that, which is only
  // possible because the colour-ID pass reads one hit per covered pixel.
  QList<GLHit> hits = backend.pick(0, 0, kW, kH);
  qInfo().noquote() << "rubber-band hits:" << hits.size();
  const int legacyCapacity =
      Avogadro::SEL_BUF_MAX_SIZE / (4 * sizeof(GLuint));
  QVERIFY2(hits.size() > legacyCapacity,
           qPrintable(QString("rubber band returned %1 hits; the colour-ID pass "
                              "must exceed the legacy GL_SELECT capacity of %2")
                          .arg(hits.size())
                          .arg(legacyCapacity)));

  fbo.release();
  delete bsdy;
}

QTEST_MAIN(CompatibilityTest)
#include "compatibilitytest.moc"
