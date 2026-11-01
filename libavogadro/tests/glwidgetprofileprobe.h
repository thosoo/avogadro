#ifndef GLWIDGET_PROFILE_PROBE_H
#define GLWIDGET_PROFILE_PROBE_H

#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QTest>

// fix-modern-tier-core-profile: forced-legacy-tier tests must skip honestly
// when the resolved widget context is Core. A standalone QOpenGLContext probe
// does not predict what a QOpenGLWidget resolves: on some GLX/Mesa stacks the
// probe resolves a Compatibility context while the widget context resolves
// Core (observed on llvmpipe/nouveau). Probing through an actual QOpenGLWidget
// exercises the same widget context machinery as the GLWidget under test, so
// the resolved profile matches what the real widget will get. Without this,
// the forced-tier startup fatal in GLWidget::initializeGL fires before the
// test's own Core checks can run.
//
// Returns true when the widget-path context resolves a Core profile. Returns
// false when the context never becomes valid (e.g. the offscreen platform,
// where the callers' existing "QOpenGLWidget unsupported" skips apply) or
// when a Compatibility (or other) profile resolves.
inline bool glWidgetPathResolvesCoreProfile(const QSurfaceFormat &fmt)
{
  QOpenGLWidget probeWidget;
  probeWidget.setFormat(fmt);
  probeWidget.resize(16, 16);
  probeWidget.show();
  // Wait for the first frame (initializeGL creates the context) or time out;
  // a context that never arrives keeps core=false and callers keep their
  // existing no-context skip paths.
  for (int i = 0; i < 100 && probeWidget.context() == nullptr; ++i)
    QTest::qWait(20);
  const bool core = probeWidget.context() != nullptr
    && probeWidget.context()->format().profile() == QSurfaceFormat::CoreProfile;
  probeWidget.close();
  return core;
}

#endif // GLWIDGET_PROFILE_PROBE_H
