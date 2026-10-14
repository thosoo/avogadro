/**********************************************************************
  parityharness - shared legacy-vs-modern visual parity harness for the
  ported engines (gpu-rendering-pipeline tasks 3.1 / 3.2).

  Renders a mixed-element molecule twice:
    * legacy: a real GLWidget forced onto the fixed-function tier via
      AVOGADRO_RENDERER=legacy (GLPainter is hard-coupled to GLWidget),
    * modern: the SceneBuilder emitter feeding ModernRendererBackend.

  The two frames are compared for "same visual class" (design D2) and written
  out side by side as PNGs for human review (AVO_PARITY_OUT overrides the
  output directory).

  The engines are Qt plugins, so each engine needs its own test binary that
  compiles exactly one engine .cpp; this header keeps them from diverging.
 *****************************************************************/

#ifndef AVOGADRO_TESTS_PARITYHARNESS_H
#define AVOGADRO_TESTS_PARITYHARNESS_H

#include <QtTest>
#include <QImage>
#include <QDir>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QVector>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/painterdevice.h>
#include <avogadro/engine.h>
#include <avogadro/camera.h>

#include "libavogadro/src/glwidget.h"
#include "libavogadro/src/glpainter_p.h"
#include "libavogadro/src/global.h"
#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/rendering/modernbackend.h"

namespace Avogadro {
namespace testing {

constexpr int kW = 256;
constexpr int kH = 256;
//! Distinctly non-black so geometry drawn in black is not mistaken for an
//! empty frame, and identical for both tiers so coverage compares fairly.
const QColor kBackground(10, 10, 60);

//! Minimal PainterDevice usable with either painter, mirroring the
//! GLPainterDevice inside glwidget.cpp.
class ParityPainterDevice : public Avogadro::PainterDevice
{
public:
  ParityPainterDevice(Avogadro::Painter *p, Molecule *mol, Color *map)
    : m_painter(p), m_molecule(mol), m_colorMap(map) {}

  Avogadro::Painter *painter() const override { return m_painter; }
  Camera *camera() const override { return const_cast<Camera *>(&m_camera); }
  bool isSelected(const Avogadro::Primitive *) const override { return false; }
  double radius(const Avogadro::Primitive *) const override { return 0.3; }
  const Molecule *molecule() const override { return m_molecule; }
  Color *colorMap() const override { return m_colorMap; }
  int width() override { return kW; }
  int height() override { return kH; }

  Avogadro::Painter *m_painter;
  Molecule *m_molecule;
  Color *m_colorMap;
  Camera m_camera;
};

//! A colour map that resolves element colours (a compact CPK subset) and
//! honours per-atom custom colour names. A bare Avogadro::Color cannot do this:
//! its channels are uninitialised and its setFromPrimitive() is a no-op, and
//! the real element map (elementcolor.so) is a plugin this binary cannot link.
class HarnessColor : public Color
{
public:
  HarnessColor() { setFromRgba(0.8f, 0.8f, 0.8f, 1.0f); }

  void setFromPrimitive(const Primitive *p) override
  {
    const Atom *atom = dynamic_cast<const Atom *>(p);
    if (!atom)
      return;
    if (!atom->customColorName().isEmpty()) {
      const QColor c = QColor(atom->customColorName());
      setFromRgba(static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                  static_cast<float>(c.blueF()), 1.0f);
      return;
    }
    switch (atom->atomicNumber()) {
      case 1: setFromRgba(0.95f, 0.95f, 0.95f, 1.0f); break; // H
      case 6: setFromRgba(0.20f, 0.20f, 0.20f, 1.0f); break; // C
      case 7: setFromRgba(0.05f, 0.05f, 0.90f, 1.0f); break; // N
      case 8: setFromRgba(0.90f, 0.10f, 0.10f, 1.0f); break; // O
      default: setFromRgba(0.80f, 0.60f, 0.20f, 1.0f); break;
    }
  }
};

//! A mixed-element molecule (C, O, N, H) — the mixed-element molecule the
//! parity requirement names. Custom colours make the expected dominant
//! channels explicit and independent of the element colour map.
inline void buildMixedMolecule(Molecule &mol)
{
  Atom *c = mol.addAtom(6, Eigen::Vector3d(0.0, 0.0, 0.0));
  Atom *o = mol.addAtom(8, Eigen::Vector3d(1.5, 0.0, 0.0));
  Atom *n = mol.addAtom(7, Eigen::Vector3d(0.0, 1.4, 0.0));
  Atom *h = mol.addAtom(1, Eigen::Vector3d(-0.9, -0.8, 0.0));
  c->setCustomColorName("red");
  o->setCustomColorName("blue");
  Q_UNUSED(n);
  Q_UNUSED(h);
  mol.addBond(c->id(), o->id(), 1);
  mol.addBond(c->id(), n->id(), 1);
  mol.addBond(c->id(), h->id(), 1);
}

//! Fixed-function two-light state exactly as GLWidget::initializeGL sets it
//! (global.h LIGHT_* constants), so the legacy frame is representative.
inline void setupLegacyFixedFunction()
{
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL_EXT, GL_SEPARATE_SPECULAR_COLOR_EXT);
  glLightfv(GL_LIGHT0, GL_AMBIENT, Avogadro::LIGHT_AMBIENT);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, Avogadro::LIGHT0_DIFFUSE);
  glLightfv(GL_LIGHT0, GL_SPECULAR, Avogadro::LIGHT0_SPECULAR);
  glLightfv(GL_LIGHT0, GL_POSITION, Avogadro::LIGHT0_POSITION);
  glLightfv(GL_LIGHT1, GL_AMBIENT, Avogadro::LIGHT_AMBIENT);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, Avogadro::LIGHT1_DIFFUSE);
  glLightfv(GL_LIGHT1, GL_SPECULAR, Avogadro::LIGHT1_SPECULAR);
  glLightfv(GL_LIGHT1, GL_POSITION, Avogadro::LIGHT1_POSITION);
  glEnable(GL_LIGHT1);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

//! The shared camera: a view from -Z, identical for both tiers. Engines
//! frustum-cull against pd->camera()->modelview(), so the device camera must
//! agree with the MVP handed to the backend or engines will cull themselves
//! away in the modern tier.
//! The MVP the working modernrenderertest uses (perspective + pull back 5).
inline QMatrix4x4 parityMvp()
{
  QMatrix4x4 mvp;
  // 40 deg matches Camera's default angleOfViewY so the legacy widget and the
  // modern tier frame the molecule identically.
  mvp.perspective(40.0f, float(kW) / kH, 0.1f, 100.0f);
  mvp.translate(0.0f, 0.0f, -5.0f);
  return mvp;
}

//! The matching modelview, built explicitly as a matrix (translation is the
//! last column in Eigen's column-major layout). Engines frustum-cull against
//! pd->camera()->modelview(), so the device camera must agree with the MVP
//! handed to the backend or engines cull themselves away in the modern tier.
inline Eigen::Projective3d parityModelview()
{
  Eigen::Matrix4d m = Eigen::Matrix4d::Identity();
  m(2, 3) = -5.0;
  return Eigen::Projective3d(m);
}

struct FrameStats
{
  int nonBg = 0;
  int total = 0;
  QColor center;
  bool hasContent() const { return nonBg > 0; }
  double coverageFraction() const
  {
    return total > 0 ? static_cast<double>(nonBg) / total : 0.0;
  }
};

inline FrameStats summarizeFrame(const QImage &img, const QColor &bg)
{
  FrameStats s;
  s.total = img.width() * img.height();
  if (!img.isNull() && img.width() > 1 && img.height() > 1)
    s.center = img.pixelColor(img.width() / 2, img.height() / 2);
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      const QColor c = img.pixelColor(x, y);
      if (qAbs(c.red() - bg.red()) > 12 || qAbs(c.green() - bg.green()) > 12 ||
          qAbs(c.blue() - bg.blue()) > 12) {
        ++s.nonBg;
      }
    }
  }
  return s;
}

//! Project an atom's world position through the shared MVP (parityMvp) and
//! return the pixel coordinates. Y is flipped into top-down image order.
inline QPointF projectAtom(const Eigen::Vector3d &world)
{
  const QMatrix4x4 mvp = parityMvp();
  const QVector4D clip = mvp * QVector4D(float(world.x()), float(world.y()),
                                         float(world.z()), 1.0f);
  if (qFuzzyIsNull(clip.w()))
    return QPointF(-1, -1);
  const float ndcX = clip.x() / clip.w();
  const float ndcY = clip.y() / clip.w();
  return QPointF((ndcX * 0.5f + 0.5f) * kW,
                 (1.0f - (ndcY * 0.5f + 0.5f)) * kH);
}

//! True when any pixel within `radius` of `centre` differs from the
//! background: per-atom presence at its projected position. This catches a
//! missing atom even when the whole-frame dominant channel is carried by
//! other atoms, and it includes achromatic geometry (white/black/grey).
inline bool atomPresentAt(const QImage &img, const QPointF &centre,
                          const QColor &bg, int radius = 12)
{
  const int cx = int(centre.x());
  const int cy = int(centre.y());
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx) {
      const int x = cx + dx;
      const int y = cy + dy;
      if (x < 0 || y < 0 || x >= img.width() || y >= img.height())
        continue;
      const QColor c = img.pixelColor(x, y);
      if (qAbs(c.red() - bg.red()) > 12 || qAbs(c.green() - bg.green()) > 12 ||
          qAbs(c.blue() - bg.blue()) > 12)
        return true;
    }
  return false;
}

//! True when the dominant colour channel agrees and is meaningfully present.
//! Channel dominance encodes the element colour and is far more robust to
//! lighting/tessellation differences than exact pixel equality.
//! Dominant colour channel aggregated over all non-background pixels. A single
//! centre pixel is meaningless for sparse geometry (wireframe) or a molecule
//! whose centre is shadowed, so the comparison uses the whole frame.
inline bool dominantChannelMatchesFrame(const QImage &a, const QImage &b,
                                        const QColor &bg)
{
  auto tally = [&](const QImage &img) {
    qint64 ch[3] = {0, 0, 0};
    for (int y = 0; y < img.height(); ++y) {
      for (int x = 0; x < img.width(); ++x) {
        const QColor c = img.pixelColor(x, y);
        if (qAbs(c.red() - bg.red()) <= 12 && qAbs(c.green() - bg.green()) <= 12 &&
            qAbs(c.blue() - bg.blue()) <= 12)
          continue; // background
        ch[0] += c.red();
        ch[1] += c.green();
        ch[2] += c.blue();
      }
    }
    int best = 0;
    if (ch[1] > ch[best]) best = 1;
    if (ch[2] > ch[best]) best = 2;
    return best;
  };
  return tally(a) == tally(b);
}

inline bool dominantChannelMatches(const QColor &a, const QColor &b)
{
  auto dom = [](const QColor &c) {
    const int r = c.red(), g = c.green(), b = c.blue();
    if (r >= g && r >= b) return 0;
    if (g >= r && g >= b) return 1;
    return 2;
  };
  auto present = [](const QColor &c) {
    const int mx = qMax(c.red(), qMax(c.green(), c.blue()));
    const int mn = qMin(c.red(), qMin(c.green(), c.blue()));
    return mx > 60 && (mx - mn) > 25;
  };
  if (!present(a) || !present(b))
    return false;
  return dom(a) == dom(b);
}

//! Write <name>_legacy.png / <name>_modern.png and a combined side-by-side
//! image; returns the directory they were written to.
inline QString dumpParityImages(const QString &name, const QImage &legacy,
                                const QImage &modern)
{
  const QString dir = qEnvironmentVariable("AVO_PARITY_OUT").isEmpty()
                          ? QDir::tempPath()
                          : qEnvironmentVariable("AVO_PARITY_OUT");
  QDir().mkpath(dir);
  legacy.save(QString("%1/%2_legacy.png").arg(dir, name));
  modern.save(QString("%1/%2_modern.png").arg(dir, name));

  const int gap = 8;
  QImage combined(kW * 2 + gap, kH, QImage::Format_RGB32);
  combined.fill(Qt::black);
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) {
      combined.setPixelColor(x, y, legacy.pixelColor(x, y));
      combined.setPixelColor(kW + gap + x, y, modern.pixelColor(x, y));
    }
  }
  combined.save(QString("%1/%2_side_by_side.png").arg(dir, name));
  return dir;
}

struct ParityResult
{
  bool ok = false;
  QString message;
  QImage legacy;
  QImage modern;
  QString legacyCoverage;
  QString modernCoverage;
  QString imageDir;
};

//! Render a freshly created engine of EngineT through the legacy
//! fixed-function tier inside a real GLWidget and return the frame. The widget
//! takes ownership of (and deletes) the engines in its list, so the engine is
//! heap-allocated here and deliberately leaked into the widget's ownership.
template <typename EngineT>
QImage renderLegacyFrame(Molecule &mol, HarnessColor *colorMap, bool *ok,
                         QString *err)
{
  qputenv("AVOGADRO_RENDERER", "legacy");

  GLWidget widget;
  // A distinctly non-black background so geometry drawn in black is not
  // mistaken for an empty frame.
  widget.setBackground(QColor(10, 10, 60));
  widget.setColorMap(colorMap);
  widget.setMolecule(&mol);
  widget.resize(kW, kH);

  // Replace whatever default engine set the widget loaded with just the
  // engine under test so the frame contains exactly its output.
  foreach (Engine *e, widget.engines())
    widget.removeEngine(e);
  // The engine must be told about the molecule explicitly: addEngine() only
  // forwards molecule changes via the moleculeChanged() signal.
  EngineT *engine = new EngineT();
  engine->setMolecule(&mol);
  // Engines default to disabled; renderOpaque is skipped otherwise.
  engine->setEnabled(true);
  widget.addEngine(engine);
  // Pin the legacy camera to the same transform the modern tier uses, so the
  // two frames are rendered from the same viewpoint and the coverage /
  // colour comparison is apples to apples.
  widget.camera()->setModelview(parityModelview());

  widget.show();
  if (!QTest::qWaitForWindowExposed(&widget)) {
    *ok = false;
    *err = "legacy GLWidget window was never exposed";
    return QImage();
  }
  const QImage img = widget.grabFramebuffer();
  if (img.isNull()) {
    *ok = false;
    *err = "legacy grabFramebuffer() returned a null image";
    return QImage();
  }
  *ok = true;
  return img;
}

//! Render a freshly created engine of EngineT through the modern instanced
//! tier and return the frame. This engine is owned by this scope.
template <typename EngineT>
QImage renderModernFrame(Molecule &mol, HarnessColor *colorMap,
                         QOpenGLContext *ctx, QOffscreenSurface *surface,
                         bool *ok, QString *err)
{
  qunsetenv("AVOGADRO_RENDERER");
  ctx->makeCurrent(surface);

  EngineT engine(&mol);
  engine.setEnabled(true);

  rendering::SceneBuilder builder;
  ParityPainterDevice device(&builder, &mol, colorMap);
  // Engines frustum-cull against pd->camera()->modelview(), so the device
  // camera must agree with the MVP handed to the backend.
  device.m_camera.setModelview(parityModelview());
  engine.setPainterDevice(&device);

  builder.begin();
  builder.clearStores();
  engine.renderOpaque(&device);
  builder.end();

  rendering::ModernRendererBackend backend;
  backend.initialize();
  if (!backend.isInitialized()) {
    *ok = false;
    *err = "modern backend failed to initialise";
    return QImage();
  }

  // A raw FBO (matching the proven modernrenderertest setup) rather than
  // QOpenGLFramebufferObject, whose internal state can differ per platform.
  GLuint fboId = 0, colourTex = 0, depthRb = 0;
  QOpenGLFunctions_3_3_Core gl33f;
  if (!gl33f.initializeOpenGLFunctions()) {
    *ok = false;
    *err = "could not resolve GL 3.3 functions";
    return QImage();
  }
  QOpenGLFunctions_3_3_Core &gl = gl33f;
  gl.glGenFramebuffers(1, &fboId);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  gl.glGenTextures(1, &colourTex);
  gl.glBindTexture(GL_TEXTURE_2D, colourTex);
  gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kW, kH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colourTex, 0);
  gl.glGenRenderbuffers(1, &depthRb);
  gl.glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
  gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, kW, kH);
  gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, fboId);

  gl.glViewport(0, 0, kW, kH);
  gl.glClearColor(kBackground.redF(), kBackground.greenF(),
                  kBackground.blueF(), 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  if (!backend.beginFrame(nullptr, QRect(0, 0, kW, kH), builder.quality())) {
    *ok = false;
    *err = "backend.beginFrame() failed";
    return QImage();
  }
  backend.setModelViewProjection(parityMvp());
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
    // glReadPixels is bottom-up; flip into top-down QImage order.
    const uchar *src = px.data() + (kH - 1 - y) * kW * 3;
    for (int x = 0; x < kW; ++x)
      img.setPixelColor(x, y, QColor(src[x * 3], src[x * 3 + 1], src[x * 3 + 2]));
  }

  gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl.glDeleteRenderbuffers(1, &depthRb);
  gl.glDeleteTextures(1, &colourTex);
  gl.glDeleteFramebuffers(1, &fboId);

  if (img.isNull()) {
    *ok = false;
    *err = "modern tier produced a null image";
    return QImage();
  }
  *ok = true;
  return img;
}

//! Full legacy-vs-modern parity run for one engine type.
template <typename EngineT>
ParityResult runEngineParity(Molecule &mol, HarnessColor *colorMap,
                             QOpenGLContext *ctx, QOffscreenSurface *surface,
                             double minCoverageRatio = 0.75,
                             double maxCoverageRatio = 1.3333)
{
  ParityResult r;
  const QColor &background = kBackground;

  bool ok = false;
  QString err;

  // Resolve the colour map for every atom up front: the reference frame must
  // never depend on uninitialised colour state.
  for (unsigned int i = 0; i < mol.numAtoms(); ++i)
    colorMap->setFromPrimitive(mol.atom(i));

  // AVO_SKIP_LEGACY=1 isolates the modern pass from any state the legacy
  // GLWidget render may leave behind.
  if (!qEnvironmentVariableIsSet("AVO_SKIP_LEGACY")) {
    r.legacy = renderLegacyFrame<EngineT>(mol, colorMap, &ok, &err);
    if (!ok) {
      r.message = err;
      return r;
    }
  } else {
    r.legacy = QImage(kW, kH, QImage::Format_RGB32);
    r.legacy.fill(kBackground);
  }

  // The legacy reference must be deterministic: render it twice and require
  // pixel-identical frames (a reference that flips between runs cannot
  // support a parity sign-off).
  {
    const QImage legacyAgain = renderLegacyFrame<EngineT>(mol, colorMap, &ok, &err);
    if (!ok) {
      r.message = err;
      return r;
    }
    if (legacyAgain != r.legacy) {
      r.message = "legacy reference is non-deterministic: two consecutive "
                  "renders of the same scene differ";
      return r;
    }
  }

  r.modern = renderModernFrame<EngineT>(mol, colorMap, ctx, surface, &ok, &err);
  if (!ok) {
    r.message = err;
    return r;
  }

  const FrameStats ls = summarizeFrame(r.legacy, background);
  const FrameStats ms = summarizeFrame(r.modern, background);
  r.legacyCoverage = QString::number(ls.coverageFraction(), 'f', 4);
  r.modernCoverage = QString::number(ms.coverageFraction(), 'f', 4);

  qInfo().noquote() << typeid(EngineT).name() << "parity: legacy coverage"
                    << r.legacyCoverage << "(" << ls.nonBg << "px ) modern"
                    << r.modernCoverage << "(" << ms.nonBg << "px ) legacy centre"
                    << ls.center.name() << "modern centre" << ms.center.name();

  // Always write the pair out so a failure can be inspected visually.
  r.imageDir = dumpParityImages(
      QString::fromLatin1(typeid(EngineT).name()).section(':', -1), r.legacy,
      r.modern);

  if (!ls.hasContent()) {
    r.message = QString("legacy tier drew nothing (image dir %1)").arg(r.imageDir);
    return r;
  }
  if (!ms.hasContent()) {
    r.message = QString("modern tier drew nothing (image dir %1)").arg(r.imageDir);
    return r;
  }

  // Same visual class: coverage within 25 % (same geometry, same camera;
  // tessellation differs by design but not by 4x).
  const double ratio = ls.coverageFraction() / qMax(ms.coverageFraction(), 1e-6);
  if (ratio < minCoverageRatio || ratio > maxCoverageRatio) {
    r.message = QString("coverage differs beyond the allowed band (legacy %1 "
                        "vs modern %2)")
                    .arg(r.legacyCoverage, r.modernCoverage);
    return r;
  }
  if (!dominantChannelMatchesFrame(r.legacy, r.modern, background)) {
    r.message = "dominant colour channel over the frame differs between tiers";
    return r;
  }

  // Per-atom presence at each projected atom position: catches a missing
  // atom even when other atoms carry the whole-frame dominant channel.
  for (unsigned int i = 0; i < mol.numAtoms(); ++i) {
    const QPointF screen = projectAtom(mol.atom(i)->pos()->cast<double>());
    const bool inLegacy = atomPresentAt(r.legacy, screen, background);
    const bool inModern = atomPresentAt(r.modern, screen, background);
    if (!inLegacy || !inModern) {
      r.message = QString("atom %1 missing at projected position (%2,%3): "
                          "legacy=%4 modern=%5")
                      .arg(i)
                      .arg(screen.x(), 0, 'f', 0)
                      .arg(screen.y(), 0, 'f', 0)
                      .arg(inLegacy)
                      .arg(inModern);
      return r;
    }
  }

  r.ok = true;
  return r;
}

} // namespace testing
} // namespace Avogadro

#endif // AVOGADRO_TESTS_PARITYHARNESS_H
