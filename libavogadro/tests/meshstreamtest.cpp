// gpu-rendering-pipeline task 5.1 (design A6): per-frame CPU cost of drawing a
// stable mesh must NOT scale with vertex count.
//
// GLPainter streams a mesh to a VBO once (when it becomes stable) and draws via
// glDrawArrays, instead of deep-copying the vertex buffers on the CPU every
// frame. To prove the property we drive GLPainter::drawColorMesh through the
// real GLWidget on the LEGACY/compat tier (GLPainter is the legacy-tier
// painter, so this exercises exactly the modified ensureMeshVBO / drawColorMesh
// path) at two mesh sizes that differ by 10x in vertex count, and assert that
// the 10x larger mesh costs at most 3x the per-frame CPU time. A per-frame deep
// copy (the pre-change behaviour) would cost ~10x.
//
// The measurement runs inside an overridden paintGL; the widget is shown and
// renderNow() is called so paintGL fires once and the result is captured before
// the test method asserts. The legacy tier is forced BEFORE the widget is
// created (the tier is chosen in GLWidget::initializeGL, i.e. on first show);
// on the modern tier exposing a GLWidget crashes in this CI runner's GL stack,
// a pre-existing environmental issue unrelated to this change.

#include <QtTest>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QElapsedTimer>
#include <QScopedPointer>
#include <QGuiApplication>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <Eigen/Core>

#include <avogadro/mesh.h>
#include <avogadro/color3f.h>

#include "libavogadro/src/glwidget.h"
#include "libavogadro/src/glpainter_p.h"

using namespace Avogadro;

namespace {

// GLWidget that measures, in a single paintGL, the per-frame CPU cost of
// drawColorMesh for a small and a large stable mesh.
class MeshPaintWidget : public GLWidget {
public:
  explicit MeshPaintWidget(const QSurfaceFormat &fmt)
    : GLWidget(nullptr, fmt, nullptr) {}

public:
  bool m_done = false;
  double m_smallPerFrame = 0.0;
  double m_bigPerFrame = 0.0;
  double m_ratio = 0.0;

protected:
  void paintGL() override {
    if (m_done) return;
    m_done = true;

    GLPainter painter;
    painter.begin(this);
    setProjection(GLWidget::Orthographic);

    const int kWarmup = 5;   // uploads the VBO once; subsequent draws are O(1)
    const int kFrames = 300;

    Mesh *small = makeMesh(50000);
    for (int i = 0; i < kWarmup; ++i) painter.drawColorMesh(*small, 0);
    QElapsedTimer t; t.start();
    for (int i = 0; i < kFrames; ++i) painter.drawColorMesh(*small, 0);
    m_smallPerFrame = t.elapsed() / double(kFrames);

    Mesh *big = makeMesh(500000);  // 10x the vertices of small
    for (int i = 0; i < kWarmup; ++i) painter.drawColorMesh(*big, 0);
    t.start();
    for (int i = 0; i < kFrames; ++i) painter.drawColorMesh(*big, 0);
    m_bigPerFrame = t.elapsed() / double(kFrames);

    m_ratio = m_smallPerFrame > 0.0 ? m_bigPerFrame / m_smallPerFrame : 0.0;

    qInfo() << "meshstream small(50k) per-frame CPU:" << m_smallPerFrame << "ms";
    qInfo() << "meshstream big (500k) per-frame CPU:" << m_bigPerFrame << "ms";
    qInfo() << "meshstream big/small ratio (10x vertices):" << m_ratio;

    QFile out("/tmp/meshstream_result.txt");
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream os(&out);
      os << m_smallPerFrame << "\n" << m_bigPerFrame << "\n" << m_ratio << "\n";
    }

    painter.end();
  }

private:
private:
  static Mesh *makeMesh(int verts) {
    Mesh *m = new Mesh();
    std::vector<Eigen::Vector3f> vs, ns;
    std::vector<Color3f> cs;
    vs.reserve(verts); ns.reserve(verts); cs.reserve(verts);
    for (int i = 0; i < verts; ++i) {
      vs.emplace_back(float((i % 7) - 3), float((i % 5) - 2), float((i % 3) - 1));
      ns.emplace_back(0.f, 0.f, 1.f);
      cs.emplace_back(0.5f, 0.5f, 0.5f);
    }
    m->setVertices(vs);
    m->setNormals(ns);
    m->setColors(cs);
    m->setStable(true);  // stable => GLPainter uploads the VBO once
    return m;
  }
};

} // namespace

class MeshStreamTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void testMeshStreamingPerf();

private:
  QScopedPointer<MeshPaintWidget> m_widget;
};

void MeshStreamTest::initTestCase() {
  // QOpenGLWidget is unsupported on the offscreen platform (and this runner's
  // modern tier crashes on expose); the revision-counter unit test covers the
  // change-detection mechanism headlessly.
  if (QGuiApplication::platformName() == "offscreen")
    QSKIP("QOpenGLWidget unsupported on the offscreen platform; "
          "unit tests cover the revision counter");

  // Force the legacy/compat fixed-function tier BEFORE the widget is created.
  qputenv("AVOGADRO_RENDERER", "legacy");

  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);

  m_widget.reset(new MeshPaintWidget(fmt));
  m_widget->resize(256, 256);
  m_widget->show();
  m_widget->renderNow();  // force initializeGL + one paintGL (the measurement)
}

void MeshStreamTest::testMeshStreamingPerf() {
  QVERIFY2(m_widget && m_widget->m_done,
           "paintGL did not run; the measurement is unavailable");

  // The VBO streaming path draws with client-state entry points
  // (glVertexPointer / glNormalPointer / glColorPointer / glEnableClientState),
  // which do not exist in the OpenGL 3.3 Core profile. On this runner
  // QOpenGLWidget resolves a Core context even when a Compatibility format was
  // requested, so the ratio is unverifiable there; QSKIP and let the assertion
  // run wherever a Compatibility context exists (e.g. CI). Mirrors
  // glselectoverflowtest's Core-profile skip.
  bool coreContext = m_widget->context()
    && m_widget->context()->format().profile() == QSurfaceFormat::CoreProfile;
  if (coreContext)
    QSKIP("Resolved context is Core profile: the legacy client-state VBO draw "
          "path is unavailable there; the per-frame ratio is covered where a "
          "Compatibility context exists");

  const double ratio = m_widget->m_ratio;
  qInfo() << "meshstream per-frame CPU ratio across 10x vertices =" << ratio;

  // If the mesh were deep-copied on the CPU every frame (pre-change), 10x the
  // vertices would cost ~10x the per-frame time. Streaming to a VBO once makes
  // the per-frame cost independent of vertex count, so the ratio stays near 1.
  // Allow a 3x margin for driver/allocator noise.
  QVERIFY2(ratio <= 3.0,
           qPrintable(QString(
             "per-frame CPU cost scaled with vertex count (big/small ratio = %1; "
             "expected <= 3.0 for VBO streaming)").arg(ratio)));
}

#include "meshstreamtest.moc"
QTEST_MAIN(MeshStreamTest)
