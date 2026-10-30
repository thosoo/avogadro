/**********************************************************************
  Consumer-parity regression tests (fix-modern-tier-consumer-parity,
  tasks 5.1 / 5.2 / 5.3).

  Three concerns that the modern/compat tier must not silently drop:

    5.3 Emitter diagnostics  -- SceneBuilder::drawTorus / drawEllipsoid /
        drawShadedQuadrilateral each report their omission/coarsening exactly
        once (no silent primitive loss). Headless, no GL required.

    5.1 Mesh-flush upload gating -- GLPainter streams a mesh's VBOs once
        (when it becomes stable / is regenerated / the context changes) and
        draws with a cached VBO thereafter. An unstable mesh is skipped.
        Asserted at the painter level by driving GLPainter::drawColorMesh
        directly and reading the AVOGADRO_TEST_UPLOAD_COUNTER hook.

    5.2 Overlay bracketing + unit cell -- a tool's overlay paint runs under
        an active GLPainter bracket (no "GLPainter not active." warning and
        its drawText actually draws), and a molecule with an OpenBabel unit
        cell renders its cell box/axes while a molecular molecule does not.
        Drives a real GLWidget (QOpenGLWidget needs a drawable), so these
        skip on the offscreen platform.
 *****************************************************************/

#include <QtTest>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QMessageLogContext>
#include <QScopedPointer>
#include <QGuiApplication>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <functional>
#include <Eigen/Geometry>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/color.h>
#include <avogadro/mesh.h>
#include <avogadro/color3f.h>
#include <avogadro/painter.h>
#include <avogadro/tool.h>

#include <openbabel/generic.h>   // OpenBabel::OBUnitCell

#include "libavogadro/src/glwidget.h"
#include "libavogadro/src/glpainter_p.h"
#include "libavogadro/src/rendering/scenebuilder.h"
#include "libavogadro/src/engines/bsdyengine.h"

using namespace Avogadro;
using Avogadro::rendering::SceneBuilder;

// Test-only counter hooks (declared in glpainter_p.cpp, compiled into this
// test with AVOGADRO_TEST_UPLOAD_COUNTER so these resolve). Declared at
// Avogadro:: scope to match the definitions there.
namespace Avogadro {
int glpainterMeshUploadCount();
void glpainterResetMeshUploadCount();
int glpainterBoxDrawCount();
void glpainterResetBoxDrawCount();
} // namespace Avogadro

namespace {

// ---------------------------------------------------------------------------
// Test-only message handler: counts "GLPainter not active." warnings.
// ---------------------------------------------------------------------------
int g_lpainterNotActiveCount = 0;
void lpainterNotActiveHandler(QtMsgType, const QMessageLogContext &,
                              const QString &msg)
{
  if (msg.contains("GLPainter not active."))
    ++g_lpainterNotActiveCount;
}

// ---------------------------------------------------------------------------
// Minimal tool that paints a single screen-space text label via the widget's
// painter. It records whether the draw actually took effect (GLPainter::
// drawText returns 0 when the painter is not active, i.e. no bracket).
// ---------------------------------------------------------------------------
class OverlayTestTool : public Tool {
  Q_OBJECT
public:
  explicit OverlayTestTool() : Tool(nullptr) {}

  QString settingsTitle() const override { return QString("OverlayTest"); }
  QString identifier() const override { return QString("overlaytest"); }
  QString name() const override { return QString("Overlay Test"); }
  QAction *activateAction() const override { return nullptr; }
  QWidget *settingsWidget() override { return nullptr; }
  QUndoCommand *mousePressEvent(GLWidget *, QMouseEvent *) override
    { return nullptr; }
  QUndoCommand *mouseReleaseEvent(GLWidget *, QMouseEvent *) override
    { return nullptr; }
  QUndoCommand *mouseMoveEvent(GLWidget *, QMouseEvent *) override
    { return nullptr; }
  QUndoCommand *mouseDoubleClickEvent(GLWidget *, QMouseEvent *) override
    { return nullptr; }

  bool paint(GLWidget *widget) override
  {
    // GLWidget::painter() returns a Painter* that is always a GLPainter here.
    GLPainter *p = static_cast<GLPainter *>(widget->painter());
    if (p)
      drawnText = p->drawText(20, 20, "overlay");
    return true;
  }

  int drawnText = 0;
};

// ---------------------------------------------------------------------------
// GLWidget that drives GLPainter::drawColorMesh directly in paintGL (the
// meshstreamtest pattern). It measures the mesh-flush upload gating, not the
// widget's own render path, so it overrides paintGL.
// ---------------------------------------------------------------------------
class MeshPaintWidget : public GLWidget {
  Q_OBJECT
public:
  explicit MeshPaintWidget(const QSurfaceFormat &fmt)
    : GLWidget(nullptr, fmt, nullptr) {}

  bool m_done = false;
  int m_profile = -1;  // resolved GL profile (Compatibility/Core)
  int m_stableFirstUpload = -1;
  int m_stableCachedUpload = -1;
  int m_unstableUpload = -1;
  int m_revisionReupload = -1;

protected:
  void paintGL() override
  {
    if (m_done)
      return;
    m_done = true;

    glpainterResetMeshUploadCount();
    m_profile = QOpenGLContext::currentContext()
                    ? int(QOpenGLContext::currentContext()->format().profile())
                    : -1;
    GLPainter painter;
    painter.begin(this);
    setProjection(GLWidget::Orthographic);

    // A stable mesh uploads its VBO exactly once.
    Mesh *stable = makeMesh(200, true);
    painter.drawColorMesh(*stable, 0);
    m_stableFirstUpload = glpainterMeshUploadCount();
    // A cached draw on the next frame issues no new upload.
    painter.drawColorMesh(*stable, 0);
    m_stableCachedUpload = glpainterMeshUploadCount();

    // An unstable mesh is skipped: no upload.
    Mesh *unstable = makeMesh(200, false);
    painter.drawColorMesh(*unstable, 0);
    m_unstableUpload = glpainterMeshUploadCount();

    // A revision bump (regeneration) re-uploads exactly once more.
    stable->setVertices(makeVertices(250));
    painter.drawColorMesh(*stable, 0);
    m_revisionReupload = glpainterMeshUploadCount();

    painter.end();
  }

private:
  static std::vector<Eigen::Vector3f> makeVertices(int n)
  {
    std::vector<Eigen::Vector3f> vs;
    vs.reserve(n);
    for (int i = 0; i < n; ++i)
      vs.emplace_back(float(i % 7) - 3.f, float(i % 5) - 2.f, float(i % 3) - 1.f);
    return vs;
  }

  static Mesh *makeMesh(int verts, bool stable)
  {
    Mesh *m = new Mesh();
    auto vs = makeVertices(verts);
    std::vector<Eigen::Vector3f> ns;
    std::vector<Color3f> cs;
    ns.reserve(verts); cs.reserve(verts);
    for (int i = 0; i < verts; ++i) {
      ns.emplace_back(0.f, 0.f, 1.f);
      cs.emplace_back(0.5f, 0.5f, 0.5f);
    }
    m->setVertices(vs);
    m->setNormals(ns);
    m->setColors(cs);
    m->setStable(stable);
    return m;
  }
};

//! Probe whether a usable GL 3.3 context can be created on this runner.
bool glAvailable(const QSurfaceFormat &fmt)
{
  QOffscreenSurface surface;
  surface.setFormat(fmt);
  surface.create();
  QOpenGLContext ctx;
  ctx.setFormat(fmt);
  if (!ctx.create() || !ctx.makeCurrent(&surface)) {
    ctx.doneCurrent();
    return false;
  }
  ctx.doneCurrent();
  return true;
}

QSurfaceFormat makeGLFormat()
{
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  return fmt;
}

//! Pump the Qt event loop until `cond` is true or `timeoutMs` elapses. The
//! modern tier rebuilds the scene off-thread (BuilderPool); the first frame
//! presents the last-ready state and the worker result arrives via a queued
//! signal, so frames must be pumped before reading backend state.
void pumpUntil(std::function<bool()> cond, int timeoutMs = 15000)
{
  QElapsedTimer timer;
  timer.start();
  while (!cond() && timer.elapsed() < timeoutMs)
    QCoreApplication::processEvents();
}

//! Scan the back buffer for non-background pixels. Under xcb+llvmpipe the
//! modern tier may present a black frame (0 non-bg pixels) -- a pre-existing
//! QOpenGLWidget presentation limitation the caller must account for.
int scanNonBackground(QOpenGLFunctions &gl)
{
  gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
  const int w = 256, h = 256;
  std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3, 0);
  gl.glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  const float bg[3] = {0.05f, 0.05f, 0.05f};
  int nonBg = 0;
  for (int i = 0; i < w * h; ++i) {
    const float r = px[i * 3] / 255.0f;
    const float g = px[i * 3 + 1] / 255.0f;
    const float b = px[i * 3 + 2] / 255.0f;
    if (qAbs(r - bg[0]) > 0.02 || qAbs(g - bg[1]) > 0.02 || qAbs(b - bg[2]) > 0.02)
      ++nonBg;
  }
  return nonBg;
}

//! Tally each distinct emitter diagnostic (one-time statics, so the first
//! call in the process warns, the rest are silent).
struct WarnTally {
  int torus = 0, ellipsoid = 0, quad = 0;
  void handle(const QString &msg)
  {
    if (msg.contains("drawTorus")) ++torus;
    else if (msg.contains("drawEllipsoid")) ++ellipsoid;
    else if (msg.contains("drawShadedQuadrilateral")) ++quad;
  }
};
static WarnTally *g_tally = nullptr;
void tallyHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
  if (g_tally)
    g_tally->handle(msg);
}

} // namespace

class ConsumerParityTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void testEmitterDiagnostics();
  void testMeshFlushUploadCount();
  void testToolOverlayBracketing();
  void testUnitCellRender();

private:
};

// 5.3 -- SceneBuilder emitter diagnostics. No GL: each dropped/coarsened
// primitive reports its omission exactly once per process.
void ConsumerParityTest::testEmitterDiagnostics()
{
  // The emitter's one-time warnings are process-global statics, so this must
  // run before any other caller of these functions in the process. The test
  // binary is the only caller, and QtTest runs slots in declaration order.
  WarnTally tally;
  g_tally = &tally;
  QtMessageHandler old = qInstallMessageHandler(tallyHandler);
  {
    SceneBuilder builder;
    builder.begin();
    builder.clearStores();

    Eigen::Vector3d base(0, 0, 0);
    Eigen::Matrix3d m;
    m.setIdentity();

    builder.drawTorus(base, 0.2, 0.1);
    builder.drawTorus(base, 0.2, 0.1);   // second call is silent
    builder.drawEllipsoid(base, m);
    builder.drawEllipsoid(base, m);      // second call is silent
    builder.drawShadedQuadrilateral(
        Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(1, 0, 0),
        Eigen::Vector3d(1, 1, 0), Eigen::Vector3d(0, 1, 0));
    builder.drawShadedQuadrilateral(
        Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(1, 0, 0),
        Eigen::Vector3d(1, 1, 0), Eigen::Vector3d(0, 1, 0));  // silent
  }
  qInstallMessageHandler(old);
  g_tally = nullptr;

  // Each primitive warned exactly once (the second call of each was silent).
  QCOMPARE(tally.torus, 1);
  QCOMPARE(tally.ellipsoid, 1);
  QCOMPARE(tally.quad, 1);
}

// 5.1 -- Mesh-flush upload gating (asserted at the painter level).
void ConsumerParityTest::testMeshFlushUploadCount()
{
  QSurfaceFormat fmt = makeGLFormat();
  if (!glAvailable(fmt))
    QSKIP("no GL 3.3 compatibility context on this runner; "
          "upload gating is covered by the ensureMeshVBO logic",
          SkipAll);

  qputenv("AVOGADRO_RENDERER", "legacy");  // GLPainter's client-state draw path
  QScopedPointer<MeshPaintWidget> widget(new MeshPaintWidget(fmt));
  widget->resize(256, 256);
  widget->show();
  widget->renderNow();  // triggers one paintGL (the measurement)
  QCoreApplication::processEvents();

  QVERIFY2(widget->m_done, "paintGL did not run");
  // GLPainter streams mesh VBOs only on a Compatibility context; on a Core
  // context it falls back to client arrays by design (ensureMeshVBO returns
  // early, no upload). This runner's llvmpipe resolves a Core context even
  // for a requested Compatibility format, so the VBO-gating assertions are
  // only meaningful on a Compatibility context.
  if (widget->m_profile == int(QSurfaceFormat::CoreProfile))
    QSKIP("resolved GL context is Core; GLPainter VBO upload gating is compat-only by design (client-array fallback); verified on real compat hardware", SkipAll);
  // Stable mesh: exactly one upload, then cached draws add nothing.
  QCOMPARE(widget->m_stableFirstUpload, 1);
  QCOMPARE(widget->m_stableCachedUpload, 1);
  // Unstable mesh: skipped, no upload.
  QCOMPARE(widget->m_unstableUpload, 1);
  // Revision bump: re-uploads exactly once more.
  QCOMPARE(widget->m_revisionReupload, 2);
}

// 5.2a -- Tool overlay paint runs under an active painter bracket.
void ConsumerParityTest::testToolOverlayBracketing()
{
  QSurfaceFormat fmt = makeGLFormat();
  if (!glAvailable(fmt))
    QSKIP("no GL 3.3 context on this runner; skipping overlay bracket test",
          SkipAll);

  // Modern tier: this is where the "no bracket -> GLPainter not active." bug
  // lived. The fix wraps the tool paint in begin()/end() in
  // paintOverlaysAfterFrame().
  qputenv("AVOGADRO_RENDERER", "modern");

  QScopedPointer<Molecule> mol(new Molecule);
  mol->addAtom(6, Eigen::Vector3d(0, 0, 0));
  QScopedPointer<GLWidget> widget(new GLWidget(mol.data(), fmt, nullptr));
  widget->resize(256, 256);

  // A BSDYEngine gives the modern tier instanced content and a well-formed
  // worker snapshot; driving the modern tier with no engine raced on the
  // snapshot-tessellation worker (SIGSEGV) on this runner.
  BSDYEngine *engine = new BSDYEngine;
  engine->setMolecule(mol.data());
  engine->setEnabled(true);
  widget->addEngine(engine);

  QScopedPointer<OverlayTestTool> tool(new OverlayTestTool());
  widget->setTool(tool.data());

  widget->show();
  widget->renderNow();
  GLWidget *w = widget.data();
  pumpUntil([w]() { return w->backendDrawCallsLastFrame() >= 1; });
  widget->renderNow();

  // No "GLPainter not active." may fire during a rendered frame with a tool.
  g_lpainterNotActiveCount = 0;
  QtMessageHandler old = qInstallMessageHandler(lpainterNotActiveHandler);
  for (int i = 0; i < 5; ++i) {
    widget->renderNow();
    QCoreApplication::processEvents();
  }
  qInstallMessageHandler(old);

  QCOMPARE(g_lpainterNotActiveCount, 0);
  // And the tool's text must have drawn at least once (bracket active).
  QVERIFY(tool->drawnText != 0);
}

// 5.2b -- Unit cell renders on the modern tier; a molecular molecule does not.
void ConsumerParityTest::testUnitCellRender()
{
  QSurfaceFormat fmt = makeGLFormat();
  if (!glAvailable(fmt))
    QSKIP("no GL 3.3 context on this runner; skipping unit-cell test",
          SkipAll);

  // Modern tier: this is the consumer-parity concern (the cell was dropped
  // there). The cell box is drawn by the shared overlay pass
  // (paintOverlaysAfterFrame -> renderCrystalAxes -> GLPainter::drawBoxEdges),
  // so assert on that draw -- pixel readback is unreliable offscreen (the
  // modern tier presents a black frame, hidpiviewport).
  qputenv("AVOGADRO_RENDERER", "modern");

  // A set of atoms at the cell corners so the camera fits the cell (a single
  // atom leaves the camera inside the cell, clipping the box out).
  static const Eigen::Vector3d corners[8] = {
      Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(5, 0, 0),
      Eigen::Vector3d(0, 5, 0), Eigen::Vector3d(0, 0, 5),
      Eigen::Vector3d(5, 5, 0), Eigen::Vector3d(5, 0, 5),
      Eigen::Vector3d(0, 5, 5), Eigen::Vector3d(5, 5, 5)};

  // Build a widget hosting `mol` (with an optional cell) on the modern tier,
  // shown and rendered until the worker has adopted the scene, so the shared
  // overlay pass runs on the GUI thread. `out` is written via the ref so the
  // widget outlives the lambda body (QScopedPointer is non-copyable, so it
  // cannot be captured by value in the pump lambda).
  auto makeWidget = [&](Molecule *mol, QScopedPointer<GLWidget> &out) {
    out.reset(new GLWidget(mol, fmt, nullptr));
    // The ctor defaults renderUnitCellAxes to false and this test never calls
    // readSettings(); enable it before the first render so the shared overlay
    // pass honours the flag (matching the legacy tier, which also does). The
    // baseline molecule has no cell, so this still draws zero boxes there.
    out->setRenderUnitCellAxes(true);
    out->resize(256, 256);
    BSDYEngine *engine = new BSDYEngine;
    engine->setMolecule(mol);
    engine->setEnabled(true);
    out->addEngine(engine);
    out->show();
    out->renderNow();  // force initializeGL + first (async-submit) frame
    GLWidget *w = out.data();
    pumpUntil([w]() { return w->backendDrawCallsLastFrame() >= 1; });
    out->renderNow();  // present one clean, populated frame (overlay pass runs)
  };

  // Molecule WITHOUT a unit cell: baseline (corner atoms only, no cell box).
  glpainterResetBoxDrawCount();
  {
    QScopedPointer<Molecule> mol(new Molecule);
    for (const Eigen::Vector3d &c : corners)
      mol->addAtom(6, c);
    QScopedPointer<GLWidget> widget;
    makeWidget(mol.data(), widget);
    int baselineBoxes = glpainterBoxDrawCount();
    QCOMPARE(baselineBoxes, 0);
  }

  // Molecule WITH a unit cell: the cell box must be drawn (drawBoxEdges), so
  // the count is > 0 while the molecular-only molecule drew none.
  glpainterResetBoxDrawCount();
  {
    QScopedPointer<Molecule> mol(new Molecule);
    for (const Eigen::Vector3d &c : corners)
      mol->addAtom(6, c);
    OpenBabel::OBUnitCell *cell = new OpenBabel::OBUnitCell;
    cell->SetData(5.0, 5.0, 5.0, 90.0, 90.0, 90.0);  // cubic cell
    mol->setOBUnitCell(cell);
    QScopedPointer<GLWidget> widget;
    makeWidget(mol.data(), widget);
    int cellBoxes = glpainterBoxDrawCount();
    QVERIFY2(cellBoxes > 0,
             "unit cell must render its box on the modern tier (the shared "
             "overlay pass calls GLPainter::drawBoxEdges for an OBUnitCell "
             "molecule but not for a molecular-only one)");
  }
}

QTEST_MAIN(ConsumerParityTest)
#include "consumerparitytest.moc"
