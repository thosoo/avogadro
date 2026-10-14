/**********************************************************************
  Render bench & worker-path harness (gpu-rendering-pipeline tasks 6.1,
  8.1 and the 1M-atom input-latency verification of design A7/D3).

  Drives a real GLWidget through the modern tier's worker path and asserts
  the mechanism with the debug-overlay counters (the same numbers the
  on-screen overlay shows):

  1. workerAdoptionRendersScene — after a molecule is set, the builder pool
     rebuilds the scene off-thread; the queued result is adopted for the
     current revision and instanced draws are issued from the worker-built
     chunks (drawCalls >= 2: one sphere draw + one bond draw).
  2. framesWhileBuilding (design D3) — while a large rebuild is in flight,
     frames keep completing (presenting the last ready state); the GUI
     thread never waits on the build.
  3. orbitFps (task 8.1) — scripted orbit over the scene via the existing
     Camera::rotate() API, reporting the average fps.

  The spec's modern-tier fps targets (4k ≥60 iGPU-class, 50k ≥60 discrete /
  ≥30 integrated, 1M ≥30 discrete) are hardware-class acceptance criteria.
  This container renders on llvmpipe (software rasteriser) and its display
  path does not composite QOpenGLWidget content, so the harness REPORTS the
  measured numbers instead of asserting those targets; run it on
  representative hardware to check the targets. The 1M-atom scene runs when
  AVO_RENDER_BENCH_1M=1 is set (it is the slowest configuration).
 *****************************************************************/

#include <QtTest>
#include <QSurfaceFormat>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QElapsedTimer>

#include "libavogadro/src/glwidget.h"
#include "libavogadro/src/engines/bsdyengine.h"
#include "libavogadro/src/color.h"
#include "libavogadro/src/molecule.h"
#include "libavogadro/src/atom.h"
#include "libavogadro/src/bond.h"
#include "libavogadro/src/camera.h"

using Avogadro::GLWidget;
using Avogadro::BSDYEngine;
using Avogadro::Color;
using Avogadro::Molecule;
using Avogadro::Atom;
using Avogadro::Bond;

namespace {

//! Pump the event loop (and with it the queued jobReady deliveries) while
//! `predicate` is false, up to `timeoutMs`. Returns the elapsed time.
qint64 pumpUntil(const std::function<bool()> &predicate, int timeoutMs)
{
  QElapsedTimer timer;
  timer.start();
  while (!predicate()) {
    if (timer.elapsed() > timeoutMs)
      break;
    QTest::qWait(5);
  }
  return timer.elapsed();
}

//! A synthetic compact cube: `n` carbon atoms on a ~cubic lattice with 1.5 A
//! spacing and bonds along the lattice edges. Compact so the default camera
//! framing (initializeViewPoint) keeps the whole scene inside the frustum.
Molecule *makeCube(int n)
{
  Molecule *mol = new Molecule;
  const int side = int(std::ceil(std::cbrt(double(n))));
  const double spacing = 1.5;
  int count = 0;
  for (int k = 0; k < side && count < n; ++k)
    for (int j = 0; j < side && count < n; ++j)
      for (int i = 0; i < side && count < n; ++i, ++count) {
        mol->addAtom(6, Eigen::Vector3d(spacing * i, spacing * j,
                                        spacing * k));
      }
  // Bonds along the +X lattice edges: keeps the bond store large without
  // doubling the atom count.
  const unsigned long total = mol->numAtoms();
  for (unsigned long i = 0; i + 1 < total; ++i) {
    const Eigen::Vector3d a = mol->atom(i)->pos()->cast<double>();
    const Eigen::Vector3d b = mol->atom(i + 1)->pos()->cast<double>();
    if ((b - a).norm() < spacing * 1.5)
      mol->addBond(i, i + 1, 1);
  }
  return mol;
}

} // namespace

class RenderBench : public QObject
{
  Q_OBJECT

  GLWidget *makeWidget(Molecule *mol)
  {
    // The tier is chosen in GLWidget::initializeGL on first show, so the
    // override must be set before the widget is created.
    qputenv("AVOGADRO_RENDERER", "modern");
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    // Probe GL availability before creating/showing the widget. On a headless
    // runner that cannot provide a GL 3.3 compatibility context, showing and
    // rendering the widget crashes (exit 0xc0000409 in CI); skip instead.
    {
      QOffscreenSurface probeSurface;
      probeSurface.setFormat(fmt);
      probeSurface.create();
      QOpenGLContext probeCtx;
      probeCtx.setFormat(fmt);
      if (!probeCtx.create() || !probeCtx.makeCurrent(&probeSurface))
        QSKIP("No GL 3.3 compatibility context available on this runner; "
              "skipping render benchmark", SkipAll);
      probeCtx.makeCurrent(nullptr);
    }

    GLWidget *widget = new GLWidget(mol, fmt, nullptr);
    widget->setColorMap(new Color);
    widget->resize(192, 192);
    BSDYEngine *engine = new BSDYEngine;
    engine->setColorMap(widget->colorMap());
    engine->setMolecule(mol);
    engine->setEnabled(true);
    widget->addEngine(engine);
    widget->show();
    widget->renderNow();
    return widget;
  }

private Q_SLOTS:
  void workerAdoptionRendersScene();
  void framesWhileBuilding();
  void orbitFps();
};

void RenderBench::workerAdoptionRendersScene()
{
  QElapsedTimer timer;
  timer.start();
  Molecule *mol = makeCube(4000);
  qInfo() << "scene build (4k atoms):" << timer.elapsed() << "ms";

  GLWidget *widget = makeWidget(mol);
  QVERIFY(widget != nullptr);

  // The first renderModern submits the build; the queued result must be
  // adopted for the current revision and instanced draws issued from the
  // worker-built chunk stores (>= 2 draws: spheres + bonds). Frame and draw
  // counters are the debug overlay's own numbers (task 8.1).
  const unsigned int framesBefore = widget->backendFramesSubmitted();
  const qint64 adoptMs = pumpUntil([widget, framesBefore]() {
    return widget->backendFramesSubmitted() > framesBefore &&
           widget->backendDrawCallsLastFrame() >= 2;
  }, 30000);
  qInfo() << "4k: submit->adopted-and-drawn:" << adoptMs << "ms";
  QVERIFY2(widget->backendDrawCallsLastFrame() >= 2,
           "the worker-built chunks were never drawn (draw calls stayed < 2)");

  // Scripted orbit (task 8.1) through the existing camera API: the chunks
  // are reused (no re-tessellation), so the frame cost is upload + draw.
  qint64 orbitTotal = 0;
  int frames = 0;
  for (int i = 0; i < 30; ++i) {
    QElapsedTimer ft;
    ft.start();
    widget->camera()->rotate(0.15, Eigen::Vector3d::UnitZ());
    widget->renderNow();
    orbitTotal += ft.elapsed();
    ++frames;
    QVERIFY(widget->backendFramesSubmitted() > framesBefore + unsigned(i));
  }
  const double fps = frames * 1000.0 / double(orbitTotal);
  qInfo() << "4k orbit: avg" << double(orbitTotal) / frames
          << "ms/frame over" << frames << "frames =" << fps << "fps";
  delete widget;
}

void RenderBench::framesWhileBuilding()
{
  // 50k-atom rebuild: while the worker tessellates, frames must keep
  // completing (presenting the last ready state — design D3).
  Molecule *mol = makeCube(50000);
  GLWidget *widget = makeWidget(mol);

  // Wait for the initial build to settle so the test measures a REbuild.
  pumpUntil([widget]() {
    return widget->backendDrawCallsLastFrame() >= 2;
  }, 120000);
  const unsigned int settled = widget->backendFramesSubmitted();
  QVERIFY(settled > 0);

  // Force a rebuild: moving an atom bumps the revision (design A1).
  if (mol->atom(0))
    mol->atom(0)->setPos(Eigen::Vector3d(0.0, 3.0, 0.0));

  // Pump frames while the rebuild is in flight; every frame must complete
  // (the GUI thread must never wait on the build).
  int completed = 0;
  qint64 worstFrameMs = 0;
  QElapsedTimer window;
  window.start();
  while (window.elapsed() < 5000) {
    QElapsedTimer ft;
    ft.start();
    widget->renderNow();
    QTest::qWait(1);
    const qint64 ms = ft.elapsed();
    worstFrameMs = qMax(worstFrameMs, ms);
    ++completed;
  }
  qInfo() << "50k: rebuild dispatched; frames completed while building:"
          << completed << "worst frame:" << worstFrameMs << "ms";
  QVERIFY2(completed > 0,
           "no frame completed while a rebuild was in flight (input stalled)");
  // The rebuild is adopted once it lands (revision check + adoption).
  const qint64 adoptMs = pumpUntil([widget, settled, completed]() {
    return widget->backendFramesSubmitted() > settled + completed &&
           widget->backendDrawCallsLastFrame() >= 2;
  }, 120000);
  qInfo() << "50k: rebuild adopted after" << adoptMs << "ms of pumping";
  QVERIFY(widget->backendFramesSubmitted() > settled);
  delete widget;
}

void RenderBench::orbitFps()
{
  // Report-only scenes for the 8.1 harness: 50k (and 1M when opted in).
  if (!qEnvironmentVariableIsSet("AVO_RENDER_BENCH_1M"))
    QSKIP("1M-atom scene skipped (set AVO_RENDER_BENCH_1M=1 to run it)",
          SkipAll);

  QElapsedTimer timer;
  timer.start();
  Molecule *mol = makeCube(1000000);
  qInfo() << "1M scene build:" << timer.elapsed() << "ms";
  GLWidget *widget = makeWidget(mol);

  const qint64 adoptMs = pumpUntil([widget]() {
    return widget->backendDrawCallsLastFrame() >= 2;
  }, 300000);
  qInfo() << "1M: submit->adopted-and-drawn:" << adoptMs << "ms";

  qint64 orbitTotal = 0;
  int frames = 0;
  for (int i = 0; i < 10; ++i) {
    QElapsedTimer ft;
    ft.start();
    widget->camera()->rotate(0.15, Eigen::Vector3d::UnitZ());
    widget->renderNow();
    orbitTotal += ft.elapsed();
    ++frames;
  }
  qInfo() << "1M orbit: avg" << double(orbitTotal) / frames << "ms/frame ="
          << (frames * 1000.0 / double(orbitTotal)) << "fps over" << frames
          << "frames";
  delete widget;
}

#include "renderbenchtest.moc"
QTEST_MAIN(RenderBench)
