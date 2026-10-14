/**********************************************************************
  GL_SELECT overflow-detection test (gpu-rendering-pipeline task 4.2,
  design A4).

  The legacy/compat picking path uses OpenGL's GL_SELECT name stack, whose
  buffer is capped at SEL_BUF_MAX_SIZE (global.h:116). glRenderMode(GL_RENDER)
  returns -1 when the buffer overflowed before every name was recorded. The
  old code stored that return value in an unsigned counter, so -1 became a
  huge count and the parse silently truncated the hits.

  Task 4.2's fix isolates the overflow *decision* from the OpenGL calls into
  Avogadro::decideSelectRefine and makes GLWidget::hits() keep the return value
  as a signed int so -1 is detectable. Two layers of verification:

    1. Unit tests for decideSelectRefine (no GL context required): a clean
       result stops, an overflow (-1) shrinks the box about a fixed centre and
       retries until the budget is spent, then stops so the caller reports
       rather than truncates. These always run.

    2. An end-to-end check through the real GLWidget that the legacy
       GL_SELECT path still selects correctly and that a dense scene trips the
       -1 overflow path (reported, not silently truncated). GL_SELECT only
       exists in a Compatibility context; QOpenGLWidget is unsupported on the
       offscreen platform and the name stack is a no-op on a Core context, so
       these skip rather than fail (the overflow decision is still covered by
       the unit tests).
 *****************************************************************/

#include <QtTest>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QMessageLogContext>
#include <QScopedPointer>
#include <QGuiApplication>
#include <cmath>

#include <Eigen/Geometry>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/camera.h>
#include <avogadro/glhit.h>
#include <avogadro/primitive.h>

#include "libavogadro/src/glwidget.h"
#include "libavogadro/src/global.h"
#include "libavogadro/src/engines/bsdyengine.h"
#include "libavogadro/src/rendering/selectrefine.h"

using namespace Avogadro;
using Avogadro::SelectRefine;

namespace {

// GL_CONTEXT_PROFILE_MASK values (OpenGL 3.0+).
constexpr int GL_CONTEXT_PROFILE_CORE_MASK         = 0x0001;
constexpr int GL_CONTEXT_PROFILE_COMPATIBILITY_MASK = 0x0002;

//! Global capture state for the GL_SELECT overflow diagnostic. Qt6 dropped
//! QMessageHandler, so the test drives qInstallMessageHandler with a free
//! function that mutates this counter to prove the new detection code ran.
int g_overflowLogCount = 0;
void overflowLogHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
  if (msg.contains("overflow detected"))
    ++g_overflowLogCount;
}

//! Query the profile mask of the widget's current GL context. 0 means the
//! context predates the profile concept (pre-3.0). GL_CONTEXT_PROFILE_MASK is
//! 0x9126; use the literal so the test needs no extra GL header.
int glProfileMask(GLWidget *widget)
{
  widget->makeCurrent();
  GLint mask = 0;
  glGetIntegerv(0x9126, &mask);
  int result = mask;
  widget->doneCurrent();
  return result;
}

//! A plain colour map; picking emits names independent of colour, so element
//! accuracy is not in question here.
class PlainColor : public Color
{
public:
  PlainColor() { setFromRgba(1.0f, 1.0f, 1.0f, 1.0f); }
  void setFromPrimitive(const Primitive *) override {}
};

//! A small, non-collinear molecule (a central atom with neighbours along the
//! axes) so the camera's fitting plane is well defined and the atom at the
//! geometric centre projects to the screen centre.
Molecule *buildPickedMolecule()
{
  Molecule *mol = new Molecule;
  mol->addAtom(6, Eigen::Vector3d(0.0, 0.0, 0.0));
  mol->addAtom(6, Eigen::Vector3d(1.0, 0.0, 0.0));
  mol->addAtom(6, Eigen::Vector3d(-1.0, 0.0, 0.0));
  mol->addAtom(6, Eigen::Vector3d(0.0, 1.0, 0.0));
  mol->addAtom(6, Eigen::Vector3d(0.0, -1.0, 0.0));
  mol->addAtom(6, Eigen::Vector3d(0.0, 0.0, 1.0));
  mol->addAtom(6, Eigen::Vector3d(0.0, 0.0, -1.0));
  return mol;
}

//! Builds a very dense molecule: many atoms clustered in a small volume so a
//! full-frame rubber band projects all of them into the capped selection
//! buffer and overflows it.
Molecule *buildDenseMolecule(int n)
{
  Molecule *mol = new Molecule;
  for (int i = 0; i < n; ++i) {
    // Deterministic value in [-1, 1), scaled to a ~1 Angstrom cluster.
    const float u = (std::fmod(i * 12.9898f, 1.0f) * 2.0f) - 1.0f;
    const float v = (std::fmod(i * 7.832f, 1.0f) * 2.0f) - 1.0f;
    const float w = (std::fmod(i * 3.777f, 1.0f) * 2.0f) - 1.0f;
    mol->addAtom(6, Eigen::Vector3d(u, v, w) * 0.5);
  }
  return mol;
}

} // namespace

class GLSelectOverflowTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  // --- Unit tests for the isolated overflow decision (no GL needed) --------
  void testDecideSelectRefineCleanStops();
  void testDecideSelectRefineOverflowRetries();
  void testDecideSelectRefineExhaustsAndStops();
  void testDecideSelectRefinePreservesCentre();
  void testDecideSelectRefineClampsToOne();

  // --- End-to-end through GLWidget (requires a Compatibility context) -------
  void testLegacyPickingStillWorks();
  void testOverflowDetectedAndReported();

private:
  QScopedPointer<GLWidget> makeLegacyWidget(Molecule *mol, Color *color);
};

QScopedPointer<GLWidget> GLSelectOverflowTest::makeLegacyWidget(Molecule *mol, Color *color)
{
  // Force the legacy/compat fixed-function tier BEFORE the widget is created:
  // the tier is chosen in GLWidget::initializeGL, i.e. on first show.
  qputenv("AVOGADRO_RENDERER", "legacy");

  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);

  // Probe GL availability before creating/showing the widget. On a headless
  // runner that cannot provide a GL 3.3 compatibility context, showing and
  // rendering the widget produces no valid picking results (or crashes); skip
  // instead. The overflow decision is still covered by the unit tests above.
  {
    QOffscreenSurface probeSurface;
    probeSurface.setFormat(fmt);
    probeSurface.create();
    QOpenGLContext probeCtx;
    probeCtx.setFormat(fmt);
    if (!probeCtx.create() || !probeCtx.makeCurrent(&probeSurface)) {
      // QSKIP is not recognised as [[noreturn]] by MSVC's flow analysis here,
      // so provide a (runtime-unreachable) return path to satisfy the compiler.
      QSKIP("No GL 3.3 compatibility context available on this runner; "
            "skipping GL_SELECT end-to-end test; unit tests cover the overflow logic", SkipAll);
      return QScopedPointer<GLWidget>();
    }
    probeCtx.makeCurrent(nullptr);
  }

  GLWidget *widget = new GLWidget(mol, fmt, nullptr);
  widget->setColorMap(color);
  widget->resize(256, 256);

  BSDYEngine *engine = new BSDYEngine;
  engine->setMolecule(mol);
  engine->setEnabled(true);
  widget->addEngine(engine);

  widget->show();
  widget->renderNow(); // force initializeGL + one frame so the context/backend

  return QScopedPointer<GLWidget>(widget);
}

void GLSelectOverflowTest::testDecideSelectRefineCleanStops()
{
  int attempt = 0, x = 100, y = 100, w = 64, h = 64;
  // A clean hit count (>= 0) never overflows: stop, leave the box untouched.
  QCOMPARE(decideSelectRefine(5, attempt, 6, x, y, w, h), SelectRefine::Clean);
  QCOMPARE(attempt, 0);
  QCOMPARE(x, 100); QCOMPARE(y, 100); QCOMPARE(w, 64); QCOMPARE(h, 64);

  // Zero hits is also clean (no overflow), even when the budget is spent.
  int a = 6, xx = 0, yy = 0, ww = 32, hh = 32;
  QCOMPARE(decideSelectRefine(0, a, 6, xx, yy, ww, hh), SelectRefine::Clean);
  QCOMPARE(a, 6);
}

void GLSelectOverflowTest::testDecideSelectRefineOverflowRetries()
{
  int attempt = 0, x = 100, y = 100, w = 64, h = 64;
  // Overflow (-1) with budget left: retry, incrementing the attempt counter.
  QCOMPARE(decideSelectRefine(-1, attempt, 6, x, y, w, h), SelectRefine::Retry);
  QCOMPARE(attempt, 1);
  // The box halves about its fixed centre (cx = 64/2 + 100 = 132).
  QCOMPARE(w, 32); QCOMPARE(h, 32);
  QCOMPARE(x, 132 - 32 / 2); // 116
  QCOMPARE(y, 132 - 32 / 2); // 116
}

void GLSelectOverflowTest::testDecideSelectRefineExhaustsAndStops()
{
  int attempt = 5, x = 0, y = 0, w = 64, h = 64;
  // Overflow with one attempt left: spend it, then Stop (no further shrink).
  QCOMPARE(decideSelectRefine(-1, attempt, 6, x, y, w, h), SelectRefine::Stop);
  QCOMPARE(attempt, 6);
  // The rectangle is left intact once we stop reporting/refining.
  QCOMPARE(x, 0); QCOMPARE(y, 0); QCOMPARE(w, 64); QCOMPARE(h, 64);

  // Stays Stop once exhausted; the attempt counter still advances.
  QCOMPARE(decideSelectRefine(-1, attempt, 6, x, y, w, h), SelectRefine::Stop);
  QCOMPARE(attempt, 7);
}

void GLSelectOverflowTest::testDecideSelectRefinePreservesCentre()
{
  int attempt = 0, x = 0, y = 0, w = 64, h = 64;
  const int cx = w / 2 + x;
  const int cy = h / 2 + y; // 32, 32
  for (int k = 0; k < 5; ++k) {
    QCOMPARE(decideSelectRefine(-1, attempt, 100, x, y, w, h), SelectRefine::Retry);
    // The selection rectangle stays centred on the original cursor point.
    QCOMPARE(w / 2 + x, cx);
    QCOMPARE(h / 2 + y, cy);
    QVERIFY(w >= 1);
    QVERIFY(h >= 1);
  }
}

void GLSelectOverflowTest::testDecideSelectRefineClampsToOne()
{
  int attempt = 0, x = 0, y = 0, w = 2, h = 2;
  const int cx = w / 2 + x; // 1
  QCOMPARE(decideSelectRefine(-1, attempt, 100, x, y, w, h), SelectRefine::Retry);
  QCOMPARE(w, 1); QCOMPARE(h, 1); // max(1, 2/2) == 1
  // Further shrinking clamps to 1x1 while keeping the centre fixed.
  QCOMPARE(decideSelectRefine(-1, attempt, 100, x, y, w, h), SelectRefine::Retry);
  QCOMPARE(w, 1); QCOMPARE(h, 1);
  QCOMPARE(w / 2 + x, cx);
}

void GLSelectOverflowTest::testLegacyPickingStillWorks()
{
  // QOpenGLWidget is unsupported on the offscreen platform (it segfaults
  // creating GL objects there, like the parity tests), so skip the end-to-end
  // check instead of crashing. The overflow decision is covered by the unit
  // tests above.
  if (QGuiApplication::platformName() == "offscreen")
    QSKIP("QOpenGLWidget unsupported on the offscreen platform; unit tests cover the overflow logic");

  Color color;
  color.setFromRgba(1.0f, 1.0f, 1.0f, 1.0f);
  Molecule *mol = buildPickedMolecule();
  QScopedPointer<GLWidget> widget(makeLegacyWidget(mol, &color));

  QVERIFY(widget->molecule() != nullptr);

  // GL_SELECT only runs in a Compatibility context; on a Core context the name
  // stack is a no-op, so the end-to-end pick check is skipped rather than
  // failed (the overflow decision is covered by the unit tests above).
  if (glProfileMask(widget.data()) & GL_CONTEXT_PROFILE_CORE_MASK)
    QSKIP("GL_SELECT unavailable in a Core GL context; unit tests cover the overflow logic");

  // A full-frame rubber band must return only valid, in-bounds hits: every hit
  // name must index a real atom in the molecule (no buffer overrun/corruption).
  QList<GLHit> probe = widget->hits(0, 0, widget->width(), widget->height());
  QVERIFY2(!probe.isEmpty(), "legacy GL_SELECT picking must return hits in the legacy tier");
  foreach (const GLHit &hit, probe) {
    QVERIFY(hit.type() == Primitive::AtomType);
    QVERIFY(hit.name() < static_cast<GLuint>(mol->numAtoms()));
  }

  // A centre pick resolves to a real atom of the molecule.
  Atom *picked = widget->computeClickedAtom(
      QPoint(widget->width() / 2, widget->height() / 2));
  QVERIFY2(picked != nullptr, "centre pick must return a valid atom");
  QCOMPARE(picked->molecule(), mol);

  delete mol;
}

void GLSelectOverflowTest::testOverflowDetectedAndReported()
{
  // Skip on the offscreen platform for the same QOpenGLWidget reason as the
  // picking test above: it segfaults creating GL objects there.
  if (QGuiApplication::platformName() == "offscreen")
    QSKIP("QOpenGLWidget unsupported on the offscreen platform; unit tests cover the overflow logic");

  g_overflowLogCount = 0;
  QtMessageHandler old = qInstallMessageHandler(overflowLogHandler);

  Color color;
  color.setFromRgba(1.0f, 1.0f, 1.0f, 1.0f);
  Molecule *mol = buildDenseMolecule(80000);
  QScopedPointer<GLWidget> widget(makeLegacyWidget(mol, &color));

  // The -1 overflow path can only be exercised where GL_SELECT exists.
  if (glProfileMask(widget.data()) & GL_CONTEXT_PROFILE_CORE_MASK)
    QSKIP("GL_SELECT overflow cannot be triggered in a Core GL context "
          "(name stack is a no-op); the overflow decision is covered by unit tests");

  // A dense, full-frame rubber band overloads the capped selection buffer,
  // which must trip the -1 overflow path (reported, not silently truncated).
  widget->hits(0, 0, widget->width(), widget->height());

  qInstallMessageHandler(old);

  QVERIFY2(g_overflowLogCount > 0,
           "expected the GL_SELECT overflow-detection path to fire on a dense scene");

  delete mol;
}

QTEST_MAIN(GLSelectOverflowTest)
#include "glselectoverflowtest.moc"
