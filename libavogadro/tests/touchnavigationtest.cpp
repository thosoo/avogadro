/**********************************************************************
  TouchNavigationTest - regression tests for the GLWidget touch-input
  pipeline (add-touch-navigation).

  Two layers of verification, mirroring the erasetooltest / hidpiviewporttest
  fixture pattern:

    1. State-machine tests (no GL, no device-pixel-ratio concerns) drive the
       GL-independent TouchState machine directly with constructed
       QEventPoint lists and assert on the synthesized mouse events (press /
       move / release order, positions, click pairing) and the camera deltas.
       These are the core behavioral guards for the spec's tap / stroke /
       jitter / pinch / pan / palm-rejection / second-class rules.

    2. Widget integration tests drive GLWidget::touchEvent() with constructed
       QTouchEvent sequences through the real EraseTool / NavigateTool and
       assert on the observable effect on the molecule / camera. The erase
       tool picks by camera projection (not GL_SELECT), so these run
       headlessly; the navigate-tool rotate test needs a live context and is
       skipped when none is available.

  The touch pipeline delivers widget-local logical (scene) coordinates to
  GLWidget::dispatchMouse, which scales them by devicePixelRatioF() before the
  tool sees them. A touch over an atom at device position D therefore carries
  the atom's LOGICAL position D / dpr, so the tests simulate a real touch that
  way and stay correct at any device-pixel ratio.
 **********************************************************************/

#include <QtTest>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QOffscreenSurface>
#include <QOpenGLContext>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/camera.h>
#include <avogadro/glwidget.h>
#include <avogadro/molecule.h>
#include <avogadro/undosequence.h>
#include <avogadro/undostack.h>
#include <QUndoStack>

#include "libavogadro/src/glwidget_touch_p.h"
#include "libavogadro/src/tools/erasetool.h"
#include "libavogadro/src/tools/navigatetool.h"
#include "libavogadro/src/tools/drawtool.h"
#include "libavogadro/src/engines/bsdyengine.h"

using namespace Avogadro;
using Eigen::Projective3d;
using Eigen::Vector3d;

namespace detail = Avogadro::detail;

namespace {

//! Portable pi constant (MSVC does not define M_PI unless _USE_MATH_DEFINES).
const double kPi = 3.14159265358979323846;

//! Build a QEventPoint at logical (scene) position `pos` with the given state.
//! posOf() reads scenePosition(), which is the field QEventPoint's constructor
//! populates, so the state machine is unit-testable with constructed events.
QEventPoint makePoint(int id, QEventPoint::State state, const QPointF &pos)
{
  return QEventPoint(id, state, pos, pos);
}

//! Build a GLWidget bound to `mol` with a deterministic camera so atom
//! projection is reproducible. The widget is NOT shown (QOpenGLWidget cannot
//! present on the offscreen platform); the erase tool never uses GL picking, so
//! driving touchEvent against the camera projection is safe.
GLWidget *makeWidget(Molecule *mol)
{
  QSurfaceFormat fmt;
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);
  GLWidget *widget = new GLWidget(mol, fmt, nullptr);
  widget->resize(800, 600);

  Vector3d center = Vector3d::Zero();
  const QList<Atom *> atoms = mol->atoms();
  if (!atoms.isEmpty()) {
    for (Atom *a : atoms)
      center += *a->pos();
    center /= static_cast<double>(atoms.size());
  }
  Projective3d mv = Projective3d::Identity();
  mv.translate(-center);
  mv.translate(Vector3d(0.0, 0.0, -8.0));
  widget->camera()->setModelview(mv);
  widget->camera()->applyProjection();
  return widget;
}

//! Projected device-pixel position of an atom.
QPointF projPoint(GLWidget *widget, Atom *a)
{
  const Vector3d sp = widget->camera()->project(*a->pos());
  return QPointF(sp.x(), sp.y());
}

//! A cluster of four carbons near the origin plus a distant one, so a lasso can
//! enclose some atoms while another stays outside (mirror erasetooltest).
Molecule *makeClusterMolecule()
{
  Molecule *mol = new Molecule;
  mol->addAtom(6, Vector3d(0.0, 0.0, 0.0));
  mol->addAtom(6, Vector3d(0.8, 0.6, 0.0));
  mol->addAtom(6, Vector3d(-0.8, 0.6, 0.0));
  mol->addAtom(6, Vector3d(0.0, -0.9, 0.0));
  mol->addAtom(6, Vector3d(5.0, 0.0, 0.0)); // outside the lasso
  mol->addBond(mol->atom(0), mol->atom(1), 1);
  mol->addBond(mol->atom(1), mol->atom(2), 1);
  mol->addBond(mol->atom(2), mol->atom(3), 1);
  mol->addBond(mol->atom(3), mol->atom(0), 1);
  mol->addBond(mol->atom(0), mol->atom(4), 1);
  return mol;
}

//! Circular device-pixel lasso loop enclosing `targets`, padded by `pad`.
QVector<QPointF> loopAround(GLWidget *widget, const QList<Atom *> &targets,
                            double pad = 12.0)
{
  QList<QPointF> pts;
  QPointF c(0, 0);
  for (Atom *a : targets) {
    const QPointF p = projPoint(widget, a);
    pts << p;
    c += p;
  }
  c /= pts.size();

  double r = 0.0;
  for (const QPointF &p : pts) {
    const double dx = p.x() - c.x();
    const double dy = p.y() - c.y();
    r = std::max(r, std::sqrt(dx * dx + dy * dy));
  }
  r += pad;

  QVector<QPointF> loop;
  const int n = 24;
  for (int i = 0; i < n; ++i) {
    const double ang = 2.0 * kPi * i / n;
    loop << QPointF(c.x() + r * std::cos(ang), c.y() + r * std::sin(ang));
  }
  return loop;
}

//! A single (points, phase, timestamp) input to the state machine.
struct TouchStep {
  QList<QEventPoint> points;
  detail::TouchState::Phase phase;
  qint64 timestamp;
};

//! Feed a whole sequence through the machine and return the concatenation of
//! every synthetic mouse event it emitted (in order).
std::vector<detail::SyntheticMouseEvent> driveMachine(detail::TouchState &ts,
    const QList<TouchStep> &seq)
{
  std::vector<detail::SyntheticMouseEvent> out;
  for (const TouchStep &s : seq) {
    const detail::TouchResult r = ts.process(s.points, s.phase, s.timestamp);
    for (const detail::SyntheticMouseEvent &sm : r.mouseEvents)
      out.push_back(sm);
  }
  return out;
}

//! A Tool that records the (scaled) scene position of every mouse event the
//! touch pipeline dispatches to it, so HiDPI scaling can be asserted
//! headlessly. QMouseEvent is non-copyable, so only the fields of interest are
//! copied out.
class MockTool : public Tool {
  Q_OBJECT
public:
  explicit MockTool(QObject *parent = nullptr) : Tool(parent) {}

  QString settingsTitle() const override { return "Mock"; }
  QUndoCommand *mousePressEvent(GLWidget *, QMouseEvent *e) override
  {
    positions << e->scenePosition();
    return 0;
  }
  QUndoCommand *mouseReleaseEvent(GLWidget *, QMouseEvent *e) override
  {
    positions << e->scenePosition();
    return 0;
  }
  QUndoCommand *mouseMoveEvent(GLWidget *, QMouseEvent *e) override
  {
    positions << e->scenePosition();
    return 0;
  }
  QUndoCommand *mouseDoubleClickEvent(GLWidget *, QMouseEvent *e) override
  {
    positions << e->scenePosition();
    return 0;
  }
  Plugin::Type type() const override { return Plugin::ToolType; }
  QString identifier() const override { return "Mock"; }
  QString name() const override { return "Mock Tool"; }

  QList<QPointF> positions; // scaled (device-pixel) scene positions
};

//! True when a usable GL context can be created on this runner.
bool glAvailable()
{
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  QOffscreenSurface surface;
  surface.setFormat(fmt);
  surface.create();
  QOpenGLContext ctx;
  ctx.setFormat(fmt);
  if (!ctx.create())
    return false;
  if (!ctx.makeCurrent(&surface)) {
    ctx.doneCurrent();
    return false;
  }
  ctx.doneCurrent();
  return true;
}

} // namespace

class TouchNavigationTest : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  // --- State-machine behavioral guards (no GL, dpr-independent) ------------
  void testTapSynthesizesClick();
  void testJitterBelowSlopEmitsNothingThenStroke();
  void testStrokeSynthesizesPressMoveRelease();
  void testStrokeArmingHoldsPressThenEmits();
  void testSecondFingerDuringStrokeArmingBecomesGesture();
  void testBothFingersBeginGoesStraightToGesture();
  void testPinchSpreadZoomsInAndSqueezeZoomsOut();
  void testTwoFingerPanMovesCentroid();
  void testGestureEndHoldsCamera();
  void testPalmMidStrokeIsBlacklisted();
  void testRestingFingerNeverWakes();
  void testThirdFingerDoesNotSubstituteIntoPinch();
  void testSecondPointDuringPendingBecomesGesture();
  void testSecondPointBeginDuringPendingBecomesGesture();
  void testCancelWhilePendingEmitsNothing();
  void testCancelMidStrokeEmitsRelease();
  void testCancelMidGestureHoldsCamera();
  void testCancelDuringStrokeArmingEmitsNothing();
  void testEndDuringStrokeArmingEmitsPressRelease();
  void testBeginDuringStrokeArmingFormsGesture();

  // --- Widget integration (touchEvent -> real tool) ------------------------
  void testTapErasesAtom();
  void testTouchEventsRouteThroughEvent();
  void testLassoThroughSynthesis();
  void testMouseRegressionGuard();
  void testSynthesizedPositionScalesWithDpr();
  void testOneFingerRotateWithNavigateTool();
  void testEmbeddedCanvasUsesWidgetLocalPosition();
  void testPinchDuringStrokeArmingDrawsNoAtom();

  //! Drive a one-finger lasso through GLWidget::touchEvent() over a logical
  //! (scene-space) loop; the widget scales each point by devicePixelRatioF().
  //! A static member (not a free function) so the friend declaration inside
  //! GLWidget grants access to the protected touchEvent().
  static void driveTouchLasso(GLWidget *widget, EraseTool &tool,
                              const QVector<QPointF> &loopLogical);
};

// ---------------------------------------------------------------------------
// State-machine guards
// ---------------------------------------------------------------------------

void TouchNavigationTest::testTapSynthesizesClick()
{
  // A touch that begins and lifts without significant travel is a click: a
  // press+release pair at the touch position, nothing else (spec: tap scenario).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  const QPointF p(100.0, 100.0);
  const QList<TouchStep> seq = {
    {{ makePoint(0, QEventPoint::Pressed, p) }, detail::TouchState::Begin, 0},
    {{ makePoint(0, QEventPoint::Released, p) }, detail::TouchState::End, 50},
  };
  const auto out = driveMachine(ts, seq);

  QCOMPARE(ts.state(), detail::TouchState::Idle);
  QCOMPARE(static_cast<int>(out.size()), 2); // press + release, nothing else
  QCOMPARE(out[0].type, QEvent::MouseButtonPress);
  QCOMPARE(out[0].position, p);
  QCOMPARE(out[1].type, QEvent::MouseButtonRelease);
  QCOMPARE(out[1].position, p);
  // The press and release of the click share the touch position.
  QCOMPARE(out[0].position, out[1].position);
}

void TouchNavigationTest::testJitterBelowSlopEmitsNothingThenStroke()
{
  // A touch that wobbles below the slop emits nothing (jitter swallowed) and
  // stays PENDING; once it crosses the slop it becomes a STROKE. The press is
  // HELD for the arming window (kTouchStrokeArmMs) so a second finger arriving
  // in that window can still form a pinch, then emitted once the window
  // elapses (design D2, task 5.5).
  detail::TouchState ts(detail::kTouchSlopLogicalPx); // slop ~10 logical px
  const QPointF start(100.0, 100.0);

  // Begin -> PENDING, no events.
  QList<TouchStep> seq = {
    {{ makePoint(0, QEventPoint::Pressed, start) },
     detail::TouchState::Begin, 0},
  };
  QVERIFY(driveMachine(ts, seq).empty());
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  // A 5-px (Manhattan) move stays below slop: still nothing, still PENDING.
  QList<QEventPoint> jitter;
  jitter << makePoint(0, QEventPoint::Updated, QPointF(103.0, 102.0));
  detail::TouchResult r = ts.process(jitter, detail::TouchState::Update, 20);
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  // A 20-px move crosses slop: the state is STROKE but the press is held
  // (arming) — nothing emitted yet.
  QList<QEventPoint> stroke;
  stroke << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  r = ts.process(stroke, detail::TouchState::Update, 40);
  QVERIFY(r.mouseEvents.isEmpty()); // press held for the arming window
  QCOMPARE(ts.state(), detail::TouchState::Stroke);

  // After the arming window elapses the held press is emitted at the
  // slop-crossing position.
  QList<QEventPoint> later;
  later << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  r = ts.process(later, detail::TouchState::Update, 40 + detail::kTouchStrokeArmMs);
  QCOMPARE(static_cast<int>(r.mouseEvents.size()), 1);
  QCOMPARE(r.mouseEvents.first().type, QEvent::MouseButtonPress);
  QCOMPARE(r.mouseEvents.first().position, QPointF(120.0, 100.0));
}

void TouchNavigationTest::testStrokeSynthesizesPressMoveRelease()
{
  // A stroke produces a press at the traveled-to position, move events, then a
  // release, in order (spec: one-finger stroke draws a bond). The press is
  // held for the arming window then emitted, so the timestamps here elapse it
  // (design D2, task 5.5).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  const QList<TouchStep> seq = {
    {{ makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0)) },
     detail::TouchState::Begin, 0},
    {{ makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0)) },
     detail::TouchState::Update, 20}, // crosses slop -> arming, press held
    {{ makePoint(0, QEventPoint::Updated, QPointF(130.0, 110.0)) },
     detail::TouchState::Update, 20 + detail::kTouchStrokeArmMs}, // window elapsed
    {{ makePoint(0, QEventPoint::Released, QPointF(130.0, 110.0)) },
     detail::TouchState::End, 20 + detail::kTouchStrokeArmMs + 1}, // monotonic release
  };
  const auto out = driveMachine(ts, seq);

  QCOMPARE(ts.state(), detail::TouchState::Idle);
  QCOMPARE(static_cast<int>(out.size()), 3);
  QCOMPARE(out[0].type, QEvent::MouseButtonPress);
  QCOMPARE(out[0].position, QPointF(120.0, 100.0));
  QCOMPARE(out[1].type, QEvent::MouseMove);
  QCOMPARE(out[1].position, QPointF(130.0, 110.0));
  QCOMPARE(out[2].type, QEvent::MouseButtonRelease);
  QCOMPARE(out[2].position, QPointF(130.0, 110.0));
}

void TouchNavigationTest::testStrokeArmingHoldsPressThenEmits()
{
  // The arming window holds the press: a slop-crossing move emits nothing, and
  // the held press is emitted only once the window elapses (design D2, 5.5).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  // Slop crossing at t=20: arming, press held, nothing emitted.
  QList<QEventPoint> cross;
  cross << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  detail::TouchResult r =
    ts.process(cross, detail::TouchState::Update, 20);
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Stroke);

  // Just before the window elapses: still held.
  QList<QEventPoint> before;
  before << makePoint(0, QEventPoint::Updated, QPointF(125.0, 100.0));
  r = ts.process(before, detail::TouchState::Update,
                 20 + detail::kTouchStrokeArmMs - 1);
  QVERIFY(r.mouseEvents.isEmpty());

  // At/after the window: the held press is emitted at the slop-crossing pos,
  // plus the move to the current position.
  QList<QEventPoint> after;
  after << makePoint(0, QEventPoint::Updated, QPointF(130.0, 100.0));
  r = ts.process(after, detail::TouchState::Update,
                 20 + detail::kTouchStrokeArmMs + 1);
  QCOMPARE(static_cast<int>(r.mouseEvents.size()), 2);
  QCOMPARE(r.mouseEvents[0].type, QEvent::MouseButtonPress);
  QCOMPARE(r.mouseEvents[0].position, QPointF(120.0, 100.0)); // held press pos
  QCOMPARE(r.mouseEvents[1].type, QEvent::MouseMove);
  QCOMPARE(r.mouseEvents[1].position, QPointF(130.0, 100.0));
}

void TouchNavigationTest::testSecondFingerDuringStrokeArmingBecomesGesture()
{
  // The P52 pinch fix (design D2, task 5.5): a second finger that arrives
  // while a single finger is in the stroke-arming window (it just crossed the
  // slop) forms a two-finger GESTURE instead of the stroke drawing an atom.
  // The held press is never emitted, so no tool event is synthesized.
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  // Finger 0 crosses the slop (arming, press held).
  QList<QEventPoint> cross;
  cross << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  detail::TouchResult r =
    ts.process(cross, detail::TouchState::Update, 20);
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Stroke);

  // Finger 1 arrives within the arming window -> GESTURE, no tool events.
  QList<QEventPoint> second;
  second << makePoint(0, QEventPoint::Updated, QPointF(125.0, 100.0));
  second << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0));
  r = ts.process(second, detail::TouchState::Update, 30);
  QCOMPARE(ts.state(), detail::TouchState::Gesture);
  QVERIFY(r.cameraActive);
  QVERIFY(r.mouseEvents.isEmpty()); // no press, no move, no release

  // The gesture is live: spreading the pair zooms.
  QList<QEventPoint> spread;
  spread << makePoint(0, QEventPoint::Updated, QPointF(135.0, 100.0));
  spread << makePoint(1, QEventPoint::Updated, QPointF(240.0, 100.0));
  const detail::TouchResult z =
    ts.process(spread, detail::TouchState::Update, 50);
  QVERIFY(z.cameraActive);
  QVERIFY(z.zoomDistanceDelta > 0.0);
}

void TouchNavigationTest::testBothFingersBeginGoesStraightToGesture()
{
  // A touch that begins with two contact points goes straight to GESTURE with
  // no PENDING state and no synthesized tool events (spec: both-fingers-begin).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  const QList<TouchStep> begin = {
    {{ makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0)),
      makePoint(1, QEventPoint::Pressed, QPointF(200.0, 100.0)) },
     detail::TouchState::Begin, 0},
  };
  QVERIFY(driveMachine(ts, begin).empty());
  QCOMPARE(ts.state(), detail::TouchState::Gesture);

  // The next update is a camera event (pinch spread), not a tool event.
  QList<QEventPoint> spread;
  spread << makePoint(0, QEventPoint::Updated, QPointF(110.0, 100.0));
  spread << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0));
  const detail::TouchResult r =
    ts.process(spread, detail::TouchState::Update, 20);
  QVERIFY(r.cameraActive);
  // Fingers apart (distance grew 100 -> 130) => positive zoom delta (zoom in).
  QVERIFY(r.zoomDistanceDelta > 0.0);
  QVERIFY(r.mouseEvents.isEmpty());
}

void TouchNavigationTest::testPinchSpreadZoomsInAndSqueezeZoomsOut()
{
  // Changing the distance between the two tracked points zooms: spreading them
  // apart zooms in (positive delta, matching wheel-up), squeezing zooms out.
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  begin << makePoint(1, QEventPoint::Pressed, QPointF(200.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0); // gesture, distance 100

  QList<QEventPoint> apart;
  apart << makePoint(0, QEventPoint::Updated, QPointF(110.0, 100.0));
  apart << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0)); // dist 130
  detail::TouchResult zoomIn = ts.process(apart, detail::TouchState::Update, 20);
  QVERIFY(zoomIn.cameraActive);
  QVERIFY(zoomIn.zoomDistanceDelta > 0.0); // fingers apart -> zoom in

  QList<QEventPoint> together;
  together << makePoint(0, QEventPoint::Updated, QPointF(105.0, 100.0));
  together << makePoint(1, QEventPoint::Updated, QPointF(205.0, 100.0)); // dist 100
  detail::TouchResult zoomOut =
    ts.process(together, detail::TouchState::Update, 40);
  QVERIFY(zoomOut.cameraActive);
  QVERIFY(zoomOut.zoomDistanceDelta < 0.0); // fingers together -> zoom out
}

void TouchNavigationTest::testTwoFingerPanMovesCentroid()
{
  // Moving the two points together without changing their separation pans: the
  // centroid shifts while the distance delta is zero (spec: two-finger pan).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  begin << makePoint(1, QEventPoint::Pressed, QPointF(200.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);

  QList<QEventPoint> pan;
  pan << makePoint(0, QEventPoint::Updated, QPointF(110.0, 110.0));
  pan << makePoint(1, QEventPoint::Updated, QPointF(210.0, 110.0)); // +（10,10)
  const detail::TouchResult r = ts.process(pan, detail::TouchState::Update, 20);

  QVERIFY(r.cameraActive);
  QCOMPARE(r.zoomDistanceDelta, 0.0); // separation unchanged
  QCOMPARE(r.centroidFrom, QPoint(150, 100));
  QCOMPARE(r.centroidTo, QPoint(160, 110));
  QVERIFY(r.centroidFrom != r.centroidTo);
  QVERIFY(r.mouseEvents.isEmpty()); // pan reaches no tool
}

void TouchNavigationTest::testGestureEndHoldsCamera()
{
  // Lifting one original finger ends the gesture and the camera holds; the
  // remaining finger's subsequent movement does nothing (spec: gesture end).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  begin << makePoint(1, QEventPoint::Pressed, QPointF(200.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);

  QList<QEventPoint> move;
  move << makePoint(0, QEventPoint::Updated, QPointF(110.0, 100.0));
  move << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0));
  ts.process(move, detail::TouchState::Update, 20);

  // Lift finger 0 (still-present finger 1); the End event names the lifted point
  // as Released.
  QList<QEventPoint> lift0;
  lift0 << makePoint(0, QEventPoint::Released, QPointF(110.0, 100.0));
  lift0 << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0));
  ts.process(lift0, detail::TouchState::End, 40);
  QCOMPARE(ts.state(), detail::TouchState::Idle);

  // The remaining finger keeps moving: nothing happens (camera holds).
  QList<QEventPoint> rest;
  rest << makePoint(1, QEventPoint::Updated, QPointF(240.0, 100.0));
  const detail::TouchResult r = ts.process(rest, detail::TouchState::Update, 60);
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Idle);
}

void TouchNavigationTest::testPalmMidStrokeIsBlacklisted()
{
  // A palm / extra finger that lands mid-stroke is blacklisted: the stroke
  // continues unaffected and the extra contact produces no tool event
  // (spec: palm lands mid-stroke). The stroke is committed (arming window
  // elapsed, press emitted) before the palm lands; a palm landing inside the
  // arming window would instead form a pinch (see
  // testSecondFingerDuringStrokeArmingBecomesGesture).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  const QList<TouchStep> seq = {
    {{ makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0)) },
     detail::TouchState::Begin, 0},
    {{ makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0)) },
     detail::TouchState::Update, 20}, // slop crossing -> arming, press held
    {{ makePoint(0, QEventPoint::Updated, QPointF(130.0, 110.0)) },
     detail::TouchState::Update, 20 + detail::kTouchStrokeArmMs}, // window elapsed
    {{ makePoint(0, QEventPoint::Updated, QPointF(140.0, 120.0)),
      makePoint(1, QEventPoint::Updated, QPointF(400.0, 400.0)) },
     detail::TouchState::Update, 40 + detail::kTouchStrokeArmMs}, // palm lands
    {{ makePoint(0, QEventPoint::Updated, QPointF(150.0, 130.0)),
      makePoint(1, QEventPoint::Updated, QPointF(400.0, 400.0)) },
     detail::TouchState::Update, 60 + detail::kTouchStrokeArmMs},
    {{ makePoint(0, QEventPoint::Released, QPointF(150.0, 130.0)),
      makePoint(1, QEventPoint::Updated, QPointF(400.0, 400.0)) },
     detail::TouchState::End, 80 + detail::kTouchStrokeArmMs}, // release finger 0
  };
  const auto out = driveMachine(ts, seq);

  QCOMPARE(ts.state(), detail::TouchState::Idle);
  // press, move, move, move, release -- the palm (finger 1) contributed nothing.
  QCOMPARE(static_cast<int>(out.size()), 5);
  QCOMPARE(out[0].type, QEvent::MouseButtonPress);
  QCOMPARE(out[0].position, QPointF(120.0, 100.0));
  QCOMPARE(out[1].type, QEvent::MouseMove);
  QCOMPARE(out[1].position, QPointF(130.0, 110.0));
  QCOMPARE(out[2].type, QEvent::MouseMove);
  QCOMPARE(out[2].position, QPointF(140.0, 120.0));
  QCOMPARE(out[3].type, QEvent::MouseMove);
  QCOMPARE(out[3].position, QPointF(150.0, 130.0));
  QCOMPARE(out[4].type, QEvent::MouseButtonRelease);
  QCOMPARE(out[4].position, QPointF(150.0, 130.0));
}

void TouchNavigationTest::testRestingFingerNeverWakes()
{
  // A finger that rests while a stroke finishes and then lifts never wakes up
  // as a new touch: its movement and its lift produce zero events
  // (spec: resting finger never wakes). The stroke is committed (arming window
  // elapsed, press emitted) before the second finger rests.
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<TouchStep> seq = {
    {{ makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0)) },
     detail::TouchState::Begin, 0},
    {{ makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0)) },
     detail::TouchState::Update, 20}, // arming, press held
    {{ makePoint(0, QEventPoint::Updated, QPointF(130.0, 110.0)) },
     detail::TouchState::Update, 20 + detail::kTouchStrokeArmMs}, // press emitted
  };
  std::vector<detail::SyntheticMouseEvent> out = driveMachine(ts, seq);
  const size_t afterStroke = out.size(); // press + move

  // A second finger lands and rests (stroke already committed -> blacklisted).
  QList<QEventPoint> palm;
  palm << makePoint(0, QEventPoint::Updated, QPointF(140.0, 120.0));
  palm << makePoint(1, QEventPoint::Updated, QPointF(400.0, 400.0));
  ts.process(palm, detail::TouchState::Update, 40 + detail::kTouchStrokeArmMs);

  // Finger 0 lifts (stroke release); finger 1 still rests (still Pressed).
  QList<QEventPoint> release0;
  release0 << makePoint(0, QEventPoint::Released, QPointF(140.0, 120.0));
  release0 << makePoint(1, QEventPoint::Pressed, QPointF(400.0, 400.0));
  ts.process(release0, detail::TouchState::End, 60 + detail::kTouchStrokeArmMs);

  // The resting finger moves: ignored (blacklist survives stroke end).
  QList<QEventPoint> restMove;
  restMove << makePoint(1, QEventPoint::Updated, QPointF(405.0, 405.0));
  const detail::TouchResult r1 =
    ts.process(restMove, detail::TouchState::Update, 80 + detail::kTouchStrokeArmMs);
  QVERIFY(r1.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Idle);

  // The resting finger lifts: still nothing (spec).
  QList<QEventPoint> restLift;
  restLift << makePoint(1, QEventPoint::Released, QPointF(405.0, 405.0));
  const detail::TouchResult r2 =
    ts.process(restLift, detail::TouchState::End, 100 + detail::kTouchStrokeArmMs);
  QVERIFY(r2.mouseEvents.isEmpty());

  // No events were produced after the stroke's release.
  QCOMPARE(static_cast<int>(out.size()), static_cast<int>(afterStroke));
}

void TouchNavigationTest::testThirdFingerDoesNotSubstituteIntoPinch()
{
  // When one finger of a pinch lifts while a third is resting, the gesture
  // ends and the third finger never substitutes into (or restarts) the pinch
  // (spec: third finger does not substitute).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  begin << makePoint(1, QEventPoint::Pressed, QPointF(200.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);

  // Third finger lands mid-gesture (second-class, blacklisted).
  QList<QEventPoint> withThird;
  withThird << makePoint(0, QEventPoint::Updated, QPointF(110.0, 100.0));
  withThird << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0));
  withThird << makePoint(2, QEventPoint::Updated, QPointF(300.0, 300.0));
  ts.process(withThird, detail::TouchState::Update, 40);

  // Finger 0 lifts while finger 2 rests: the gesture ends.
  QList<QEventPoint> lift0;
  lift0 << makePoint(0, QEventPoint::Released, QPointF(110.0, 100.0));
  lift0 << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0));
  lift0 << makePoint(2, QEventPoint::Updated, QPointF(300.0, 300.0));
  ts.process(lift0, detail::TouchState::End, 60);
  QCOMPARE(ts.state(), detail::TouchState::Idle);

  // Finger 1 keeps moving (would continue a gesture if it had been
  // replenished); it does not.
  QList<QEventPoint> after;
  after << makePoint(1, QEventPoint::Updated, QPointF(230.0, 110.0));
  after << makePoint(2, QEventPoint::Updated, QPointF(300.0, 300.0));
  const detail::TouchResult r = ts.process(after, detail::TouchState::Update, 80);
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Idle);

  // Both remaining fingers lift: still nothing.
  QList<QEventPoint> endAll;
  endAll << makePoint(1, QEventPoint::Released, QPointF(230.0, 110.0));
  endAll << makePoint(2, QEventPoint::Released, QPointF(300.0, 300.0));
  const detail::TouchResult r2 =
    ts.process(endAll, detail::TouchState::End, 100);
  QVERIFY(r2.mouseEvents.isEmpty());
}

void TouchNavigationTest::testSecondPointDuringPendingBecomesGesture()
{
  // A second point observed while a single touch is PENDING promotes straight
  // to a two-finger gesture (no tap, no stroke).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  QList<QEventPoint> second;
  second << makePoint(0, QEventPoint::Updated, QPointF(102.0, 100.0)); // small
  second << makePoint(1, QEventPoint::Updated, QPointF(200.0, 100.0));
  const detail::TouchResult r =
    ts.process(second, detail::TouchState::Update, 20);
  QCOMPARE(ts.state(), detail::TouchState::Gesture);
  QVERIFY(r.cameraActive);
  QVERIFY(r.mouseEvents.isEmpty());
}

void TouchNavigationTest::testSecondPointBeginDuringPendingBecomesGesture()
{
  // Delivery-independence hardening (design D2, task 5.4c): when the platform
  // delivers the second finger's arrival as its OWN single-point touch-begin
  // while the first is still pending, the machine must pair it with the
  // tracked primary (forming a gesture) instead of orphaning the primary.
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> first;
  first << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(first, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  // Finger 2 arrives as its own TouchBegin carrying only that new point.
  QList<QEventPoint> secondBegin;
  secondBegin << makePoint(1, QEventPoint::Pressed, QPointF(200.0, 100.0));
  const detail::TouchResult r =
    ts.process(secondBegin, detail::TouchState::Begin, 20);
  QCOMPARE(ts.state(), detail::TouchState::Gesture);
  QVERIFY(r.cameraActive);
  QVERIFY(r.mouseEvents.isEmpty()); // no tool event from the begin

  // The gesture is live: spreading the pair produces a zoom delta.
  QList<QEventPoint> spread;
  spread << makePoint(0, QEventPoint::Updated, QPointF(110.0, 100.0));
  spread << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0));
  const detail::TouchResult z =
    ts.process(spread, detail::TouchState::Update, 40);
  QVERIFY(z.cameraActive);
  QVERIFY(z.zoomDistanceDelta > 0.0);
}

void TouchNavigationTest::testCancelWhilePendingEmitsNothing()
{
  // A TouchCancel while a single touch is pending ends the session with no
  // synthesized events (never a phantom click) — spec: cancel while pending.
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  QList<QEventPoint> cancel;
  cancel << makePoint(0, QEventPoint::Stationary, QPointF(101.0, 100.0));
  const detail::TouchResult r =
    ts.process(cancel, detail::TouchState::Cancel, 20);
  QVERIFY(r.mouseEvents.isEmpty()); // no click, no press, no release
  QCOMPARE(ts.state(), detail::TouchState::Idle);
}

void TouchNavigationTest::testCancelMidStrokeEmitsRelease()
{
  // A TouchCancel mid-stroke emits the synthesized release so the tool's stroke
  // ends cleanly, then Idle — design D9. The stroke is committed (arming window
  // elapsed, press emitted) before the cancel, so a real stroke exists to
  // clean up.
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QList<QEventPoint> stroke;
  stroke << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  detail::TouchResult r =
    ts.process(stroke, detail::TouchState::Update, 20); // slop crossing -> arming
  QCOMPARE(ts.state(), detail::TouchState::Stroke);
  QVERIFY(r.mouseEvents.isEmpty()); // press held for the arming window

  // Elapse the arming window: the held press + a move are emitted.
  QList<QEventPoint> committed;
  committed << makePoint(0, QEventPoint::Updated, QPointF(125.0, 100.0));
  r = ts.process(committed, detail::TouchState::Update,
                 20 + detail::kTouchStrokeArmMs);
  QCOMPARE(ts.state(), detail::TouchState::Stroke);
  QCOMPARE(static_cast<int>(r.mouseEvents.size()), 2); // the press + move

  QList<QEventPoint> cancel;
  cancel << makePoint(0, QEventPoint::Stationary, QPointF(125.0, 100.0));
  r = ts.process(cancel, detail::TouchState::Cancel,
                 40 + detail::kTouchStrokeArmMs);
  QCOMPARE(ts.state(), detail::TouchState::Idle);
  // Exactly one event on cancel: the release at the stroke's last position.
  QCOMPARE(static_cast<int>(r.mouseEvents.size()), 1);
  QCOMPARE(r.mouseEvents.first().type, QEvent::MouseButtonRelease);
}

void TouchNavigationTest::testCancelMidGestureHoldsCamera()
{
  // A TouchCancel mid-gesture ends the gesture with no tool events; the camera
  // holds where the gesture left it — spec: cancel mid-gesture.
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  begin << makePoint(1, QEventPoint::Pressed, QPointF(200.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Gesture);

  QList<QEventPoint> cancel;
  cancel << makePoint(0, QEventPoint::Stationary, QPointF(105.0, 100.0));
  cancel << makePoint(1, QEventPoint::Stationary, QPointF(205.0, 100.0));
  const detail::TouchResult r =
    ts.process(cancel, detail::TouchState::Cancel, 20);
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Idle);
}

void TouchNavigationTest::testCancelDuringStrokeArmingEmitsNothing()
{
  // A TouchCancel during the stroke-arming window (the press was held and
  // never emitted) goes Idle with no events: no tool event ever reached the
  // tool, so there is no stroke to clean up (design D2, task 5.5).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  QList<QEventPoint> cross;
  cross << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  detail::TouchResult r =
    ts.process(cross, detail::TouchState::Update, 20); // arming, press held
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Stroke);

  QList<QEventPoint> cancel;
  cancel << makePoint(0, QEventPoint::Stationary, QPointF(125.0, 100.0));
  r = ts.process(cancel, detail::TouchState::Cancel, 30);
  QVERIFY(r.mouseEvents.isEmpty()); // no press, no release
  QCOMPARE(ts.state(), detail::TouchState::Idle);
}

void TouchNavigationTest::testEndDuringStrokeArmingEmitsPressRelease()
{
  // A finger that crosses the slop (arming) and lifts before the window
  // elapses emits exactly a press+release pair at the stroke's positions: the
  // travel was >= slop, so this is a short stroke, not a tap, and the tool
  // sees a complete gesture (design D2, task 5.5).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  QList<QEventPoint> cross;
  cross << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  detail::TouchResult r =
    ts.process(cross, detail::TouchState::Update, 20); // arming, press held
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Stroke);

  QList<QEventPoint> end;
  end << makePoint(0, QEventPoint::Released, QPointF(120.0, 100.0));
  r = ts.process(end, detail::TouchState::End, 25); // lifts within the window
  QCOMPARE(static_cast<int>(r.mouseEvents.size()), 2); // press + release only
  QCOMPARE(r.mouseEvents[0].type, QEvent::MouseButtonPress);
  QCOMPARE(r.mouseEvents[0].position, QPointF(120.0, 100.0)); // slop-crossing
  QCOMPARE(r.mouseEvents[1].type, QEvent::MouseButtonRelease);
  QCOMPARE(r.mouseEvents[1].position, QPointF(120.0, 100.0));
  QCOMPARE(ts.state(), detail::TouchState::Idle);
}

void TouchNavigationTest::testBeginDuringStrokeArmingFormsGesture()
{
  // Delivery-independence (design D2, task 5.5): when the second finger's
  // arrival is delivered as its OWN touch-begin while the primary is in the
  // stroke-arming window, the machine forms a two-finger GESTURE instead of
  // the stroke drawing an atom. The held press is never emitted.
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0));
  ts.process(begin, detail::TouchState::Begin, 0);
  QCOMPARE(ts.state(), detail::TouchState::Pending);

  QList<QEventPoint> cross;
  cross << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  detail::TouchResult r =
    ts.process(cross, detail::TouchState::Update, 20); // arming, press held
  QVERIFY(r.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Stroke);

  // Finger 1 arrives as its own touch-begin during the arming window.
  QList<QEventPoint> secondBegin;
  secondBegin << makePoint(1, QEventPoint::Pressed, QPointF(200.0, 100.0));
  r = ts.process(secondBegin, detail::TouchState::Begin, 30);
  QCOMPARE(ts.state(), detail::TouchState::Gesture);
  QVERIFY(r.cameraActive);
  QVERIFY(r.mouseEvents.isEmpty()); // no tool event from the begin

  // The gesture is live: spreading the pair produces a zoom delta.
  QList<QEventPoint> spread;
  spread << makePoint(0, QEventPoint::Updated, QPointF(130.0, 100.0));
  spread << makePoint(1, QEventPoint::Updated, QPointF(220.0, 100.0));
  const detail::TouchResult z =
    ts.process(spread, detail::TouchState::Update, 50);
  QVERIFY(z.cameraActive);
  QVERIFY(z.zoomDistanceDelta > 0.0);
}

// ---------------------------------------------------------------------------
// Widget integration (touchEvent -> real tool)
// ---------------------------------------------------------------------------

void TouchNavigationTest::testTapErasesAtom()
{
  // A tap (quick press+release below slop) on an atom erases it exactly as a
  // mouse click does, through the full touch -> synthesize -> dispatch path
  // (spec: tap on an atom behaves like a mouse click).
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);
  widget->setMolecule(mol);
  widget->setTool(&tool);
  Avogadro::UndoStack undoStack(mol);
  widget->setUndoStack(&undoStack);

  Atom *target = mol->atom(0);
  Atom *distant = mol->atom(4); // captured before the deletion compacts indices
  const qreal dpr = widget->devicePixelRatioF();
  // A real touch carries the atom's LOGICAL position; dispatchMouse scales it
  // back to device pixels, so simulate the touch at proj / dpr.
  const QPointF logical = projPoint(widget, target) / dpr;

  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, logical);
  QTouchEvent beginEvent(QEvent::TouchBegin, nullptr, Qt::NoModifier, begin);
  widget->touchEvent(&beginEvent);

  QList<QEventPoint> end;
  end << makePoint(0, QEventPoint::Released, logical);
  QTouchEvent endEvent(QEvent::TouchEnd, nullptr, Qt::NoModifier, end);
  widget->touchEvent(&endEvent);

  QVERIFY(mol->atomById(target->id()) == nullptr); // the tapped atom is gone
  QVERIFY(mol->atomById(distant->id()) != nullptr); // the distant one stays

  delete widget;
  delete mol;
}

void TouchNavigationTest::testTouchEventsRouteThroughEvent()
{
  // P0 regression guard: GLWidget::event() MUST route the raw touch stream to
  // touchEvent(). Without that routing the pipeline is production-dead: QWidget
  // (Qt 6) declares no touchEvent virtual, so the touchEvent() override is not
  // part of the virtual dispatch table Qt walks and QWidget::event() never calls
  // it; WA_AcceptTouchEvents then also suppresses Qt's touch-to-mouse synthesis,
  // so no touch reaches any tool or the camera. This drives the tap through
  // event() (the real delivery path) rather than calling touchEvent() directly,
  // and asserts the observable effect (the tapped atom is erased).
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);
  widget->setMolecule(mol);
  widget->setTool(&tool);
  Avogadro::UndoStack undoStack(mol);
  widget->setUndoStack(&undoStack);

  Atom *target = mol->atom(0);
  Atom *distant = mol->atom(4); // captured before the deletion compacts indices
  const qreal dpr = widget->devicePixelRatioF();
  const QPointF logical = projPoint(widget, target) / dpr;

  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, logical);
  QTouchEvent beginEvent(QEvent::TouchBegin, nullptr, Qt::NoModifier, begin);
  widget->event(&beginEvent); // routed through GLWidget::event()

  QList<QEventPoint> end;
  end << makePoint(0, QEventPoint::Released, logical);
  QTouchEvent endEvent(QEvent::TouchEnd, nullptr, Qt::NoModifier, end);
  widget->event(&endEvent);

  QVERIFY(mol->atomById(target->id()) == nullptr); // the tapped atom is gone
  QVERIFY(mol->atomById(distant->id()) != nullptr); // the distant one stays

  delete widget;
  delete mol;
}

void TouchNavigationTest::testLassoThroughSynthesis()
{
  // A freehand loop drawn with one finger erases the enclosed atoms through the
  // touch synthesis path, exactly as a mouse lasso (spec: one-finger lasso).
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);
  widget->setMolecule(mol);
  widget->setTool(&tool);
  Avogadro::UndoStack undoStack(mol);
  widget->setUndoStack(&undoStack);

  const QList<Atom *> cluster = {mol->atom(0), mol->atom(1),
                                 mol->atom(2), mol->atom(3)};
  Atom *distant = mol->atom(4); // captured before the deletion compacts indices
  const qreal dpr = widget->devicePixelRatioF();
  QVector<QPointF> loop = loopAround(widget, cluster, 12.0);
  // Convert to logical (scene) space; the widget scales back to device px.
  for (QPointF &p : loop)
    p /= dpr;

  const int before = mol->numAtoms();
  driveTouchLasso(widget, tool, loop);

  QCOMPARE(mol->numAtoms(), before - 4); // the four cluster atoms deleted
  QVERIFY(mol->atomById(distant->id()) != nullptr); // distant survives

  delete widget;
  delete mol;
}

void TouchNavigationTest::testMouseRegressionGuard()
{
  // The real mouse path is unchanged: a mouse click on an atom still erases it,
  // proving the shared dispatch helper did not regress mouse behavior
  // (spec: mouse regression guard).
  Molecule *mol = makeClusterMolecule();
  GLWidget *widget = makeWidget(mol);
  EraseTool tool;
  tool.setAddHydrogens(Qt::Unchecked);
  widget->setMolecule(mol);
  widget->setTool(&tool);
  Avogadro::UndoStack undoStack(mol);
  widget->setUndoStack(&undoStack);

  Atom *target = mol->atom(0);
  const qreal dpr = widget->devicePixelRatioF();
  const QPointF logical = projPoint(widget, target) / dpr;

  QMouseEvent press(QEvent::MouseButtonPress, logical, logical,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  widget->mousePressEvent(&press);
  QMouseEvent release(QEvent::MouseButtonRelease, logical, logical,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  widget->mouseReleaseEvent(&release);

  QVERIFY(mol->atomById(target->id()) == nullptr);

  delete widget;
  delete mol;
}

void TouchNavigationTest::testSynthesizedPositionScalesWithDpr()
{
  // At a HiDPI scale the synthesized event the tool receives is the logical
  // position scaled by devicePixelRatioF() (add-touch-navigation task 1.3), so
  // picking lands on the rendered geometry the finger is over. Skipped when the
  // runner does not honour QT_SCALE_FACTOR (devicePixelRatioF() == 1).
  Molecule *mol = new Molecule;
  mol->addAtom(6, Vector3d(0.0, 0.0, 0.0));
  GLWidget *widget = makeWidget(mol);
  widget->resize(800, 600);
  widget->setMolecule(mol);

  const qreal dpr = widget->devicePixelRatioF();
  if (qAbs(dpr - 2.0) > 0.1)
    QSKIP("devicePixelRatioF() does not report ~2 on this runner "
          "(QT_SCALE_FACTOR clamped); HiDPI scaling not exercised",
          SkipAll);

  MockTool tool;
  widget->setTool(&tool);

  const QPointF device = projPoint(widget, mol->atom(0));
  const QPointF logical = device / dpr;

  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, logical);
  QTouchEvent beginEvent(QEvent::TouchBegin, nullptr, Qt::NoModifier, begin);
  widget->touchEvent(&beginEvent);
  QList<QEventPoint> end;
  end << makePoint(0, QEventPoint::Released, logical);
  QTouchEvent endEvent(QEvent::TouchEnd, nullptr, Qt::NoModifier, end);
  widget->touchEvent(&endEvent);

  // The tool sees the logical position scaled up to device pixels.
  QVERIFY(!tool.positions.isEmpty());
  QCOMPARE(tool.positions.first(), device);

  delete widget;
  delete mol;
}

void TouchNavigationTest::testEmbeddedCanvasUsesWidgetLocalPosition()
{
  // On the real app layout GLWidget is a child widget offset below the
  // toolbar/dock chrome. In Qt 6 widget delivery scenePosition() equals
  // globalPosition() (global logical screen coords): qguiapplication.cpp sets
  //   setScenePosition(ep, tempPt.globalPosition())
  // and the widget path (qapplication.cpp updateTouchPointsForWidget) rewrites
  // only position() = widget->mapFromGlobal(globalPosition()), never
  // scenePosition(). So the machine's synthesized positions are GLOBAL and must
  // be corrected to widget-local at the touchEvent boundary by subtracting the
  // widget's global origin mapToGlobal(0,0) (design D9).
  //
  // The fixture embeds GLWidget as a child at a chrome offset AND moves the
  // top-level parent to a non-zero global position, so the widget's global
  // origin differs from its window-relative chrome offset: this makes the test
  // discriminate mapToGlobal (correct) from mapTo(window) (the window-relative
  // offset the pre-fix code used). With the parent at (0,0) the two offsets
  // coincide and the test would pass for the wrong code too — the same masking
  // failure mode that hid the original bug.
  Molecule *mol = new Molecule;
  mol->addAtom(6, Vector3d(0.0, 0.0, 0.0));
  GLWidget *widget = makeWidget(mol);
  widget->resize(800, 600);
  widget->setMolecule(mol);

  // Embed as a child at a chrome offset (e.g. toolbar/dock above the canvas),
  // and move the top-level parent so the window is not at global (0,0).
  QWidget parent;
  parent.resize(1000, 800);
  parent.move(300, 200);
  widget->setParent(&parent);
  const QPoint chromeOffset(120, 64);
  widget->setGeometry(QRect(chromeOffset, QSize(800, 600)));

  MockTool tool;
  widget->setTool(&tool);

  // Where the finger must land to touch the atom: the widget-local logical px.
  const qreal dpr = widget->devicePixelRatioF();
  const QPointF device = projPoint(widget, mol->atom(0));
  const QPointF widgetLocal = device / dpr;

  // The widget's global origin (the coordinate a real touch's scenePosition()
  // is offset by). It differs from the window-relative chrome offset here, so
  // the test discriminates the correct mapToGlobal correction.
  const QPointF globalOrigin = QPointF(widget->mapToGlobal(QPoint(0, 0)));
  QVERIFY(globalOrigin != QPointF(chromeOffset));

  // A real touch on the embedded canvas carries the GLOBAL scene position =
  // widget-local + the widget's global origin.
  const QPointF sceneGlobal = widgetLocal + globalOrigin;

  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, sceneGlobal);
  QTouchEvent beginEvent(QEvent::TouchBegin, nullptr, Qt::NoModifier, begin);
  widget->touchEvent(&beginEvent);
  QList<QEventPoint> end;
  end << makePoint(0, QEventPoint::Released, sceneGlobal);
  QTouchEvent endEvent(QEvent::TouchEnd, nullptr, Qt::NoModifier, end);
  widget->touchEvent(&endEvent);

  // The tool sees the widget-local position scaled to device pixels, not the
  // global scene position offset by the widget's global origin.
  QVERIFY(!tool.positions.isEmpty());
  QCOMPARE(tool.positions.first(), device);

  delete widget;
  delete mol;
}

void TouchNavigationTest::testOneFingerRotateWithNavigateTool()
{
  // One-finger drag with the navigate tool active orbits the camera, exactly as
  // a mouse drag (spec: one-finger rotate with the navigate tool active). This
  // needs a live GL context (the navigate tool picks under the press), so it is
  // skipped when none is available on the runner.
  if (!glAvailable())
    QSKIP("QOpenGLWidget has no usable GL context on this platform "
          "(offscreen); real hardware is the acceptance gate",
          SkipAll);

  Molecule *mol = new Molecule;
  mol->addAtom(6, Vector3d(0.0, 0.0, 0.0));
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);
  GLWidget *widget = new GLWidget(mol, fmt, nullptr);
  widget->resize(800, 600);
  widget->show();
  widget->renderNow();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  if (!widget->context() || !widget->context()->isValid()) {
    delete widget;
    delete mol;
    QSKIP("QOpenGLWidget has no usable GL context on this runner", SkipAll);
  }

  NavigateTool navTool;
  widget->setTool(&navTool);
  const Projective3d before = widget->camera()->modelview();

  // One-finger drag well inside the widget: the stroke press + moves reach the
  // navigate tool and rotate the view. Timestamps advance past the arming
  // window so the held press is emitted and the drag reaches the tool.
  const QPointF p0(300.0, 300.0);
  const qreal dpr = widget->devicePixelRatioF();
  qint64 ts = 0;
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, p0 / dpr);
  QTouchEvent beginEvent(QEvent::TouchBegin, nullptr, Qt::NoModifier, begin);
  beginEvent.setTimestamp(ts);
  widget->touchEvent(&beginEvent);

  for (int i = 1; i <= 3; ++i) {
    ts += detail::kTouchStrokeArmMs;
    QList<QEventPoint> m;
    m << makePoint(0, QEventPoint::Updated, QPointF(300.0 + 20.0 * i, 300.0 + 10.0 * i) / dpr);
    QTouchEvent move(QEvent::TouchUpdate, nullptr, Qt::NoModifier, m);
    move.setTimestamp(ts);
    widget->touchEvent(&move);
  }

  QList<QEventPoint> end;
  end << makePoint(0, QEventPoint::Released, QPointF(360.0, 330.0) / dpr);
  QTouchEvent endEvent(QEvent::TouchEnd, nullptr, Qt::NoModifier, end);
  endEvent.setTimestamp(ts + detail::kTouchStrokeArmMs);
  widget->touchEvent(&endEvent);

  // Eigen::Projective3d's != is ambiguous against Qt's operators, so compare
  // with the member isApprox instead.
  QVERIFY(!before.isApprox(widget->camera()->modelview())); // the camera orbited

  delete widget;
  delete mol;
}

void TouchNavigationTest::testPinchDuringStrokeArmingDrawsNoAtom()
{
  // The P52 pinch fix, end to end (spec: pinch with an editing tool active —
  // the camera zooms/pans and the molecule is unchanged). Finger 0 presses on
  // empty space with the draw tool active and crosses the slop (the state
  // machine enters the stroke-arming window, holding the press); finger 1
  // arrives inside that window, so the interaction becomes a two-finger
  // GESTURE and the held press is never emitted. The draw tool therefore sees
  // no press and draws no atom, while the camera zooms.
  Molecule *mol = new Molecule; // empty: any press on empty space would add an atom
  GLWidget *widget = makeWidget(mol);
  widget->resize(800, 600);
  widget->setMolecule(mol);
  DrawTool tool;
  tool.setElement(6); // carbon
  tool.setAddHydrogens(Qt::Unchecked);
  widget->setTool(&tool);
  Avogadro::UndoStack undoStack(mol);
  widget->setUndoStack(&undoStack);

  const int before = mol->numAtoms(); // 0
  const Projective3d camBefore = widget->camera()->modelview();

  // Finger 0 lands and crosses the slop (arming: press held).
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, QPointF(200.0, 300.0));
  QTouchEvent beginEvent(QEvent::TouchBegin, nullptr, Qt::NoModifier, begin);
  beginEvent.setTimestamp(0);
  widget->touchEvent(&beginEvent);

  QList<QEventPoint> cross;
  cross << makePoint(0, QEventPoint::Updated, QPointF(240.0, 300.0)); // > slop
  QTouchEvent crossEvent(QEvent::TouchUpdate, nullptr, Qt::NoModifier, cross);
  crossEvent.setTimestamp(20);
  widget->touchEvent(&crossEvent);

  // Finger 1 arrives inside the arming window -> GESTURE, no tool press.
  QList<QEventPoint> second;
  second << makePoint(0, QEventPoint::Updated, QPointF(245.0, 300.0));
  second << makePoint(1, QEventPoint::Updated, QPointF(360.0, 300.0));
  QTouchEvent secondEvent(QEvent::TouchUpdate, nullptr, Qt::NoModifier, second);
  secondEvent.setTimestamp(30); // 30 - 20 < kTouchStrokeArmMs -> still arming
  widget->touchEvent(&secondEvent);

  // Spread the pair: the camera zooms in, and the molecule is unchanged.
  QList<QEventPoint> spread;
  spread << makePoint(0, QEventPoint::Updated, QPointF(255.0, 300.0));
  spread << makePoint(1, QEventPoint::Updated, QPointF(390.0, 300.0));
  QTouchEvent spreadEvent(QEvent::TouchUpdate, nullptr, Qt::NoModifier, spread);
  spreadEvent.setTimestamp(60);
  widget->touchEvent(&spreadEvent);

  QCOMPARE(mol->numAtoms(), before); // no atom was drawn
  QVERIFY(!camBefore.isApprox(widget->camera()->modelview())); // camera zoomed

  delete widget;
  delete mol;
}

void TouchNavigationTest::driveTouchLasso(GLWidget *widget, EraseTool &tool,
                                          const QVector<QPointF> &loopLogical)
{
  (void)tool; // the erase tool is already installed on the widget
  qint64 ts = 0;
  QList<QEventPoint> pts;
  pts << makePoint(0, QEventPoint::Pressed, loopLogical.first());
  QTouchEvent begin(QEvent::TouchBegin, nullptr, Qt::NoModifier, pts);
  begin.setTimestamp(ts);
  widget->touchEvent(&begin);

  // Timestamps advance past the stroke-arming window (kTouchStrokeArmMs) so the
  // held press is emitted and the lasso draws (a second finger never arrives).
  for (int i = 1; i < loopLogical.size(); ++i) {
    ts += detail::kTouchStrokeArmMs;
    QList<QEventPoint> mpts;
    mpts << makePoint(0, QEventPoint::Updated, loopLogical[i]);
    QTouchEvent move(QEvent::TouchUpdate, nullptr, Qt::NoModifier, mpts);
    move.setTimestamp(ts);
    widget->touchEvent(&move);
  }

  QList<QEventPoint> rpts;
  rpts << makePoint(0, QEventPoint::Released, loopLogical.last());
  QTouchEvent end(QEvent::TouchEnd, nullptr, Qt::NoModifier, rpts);
  end.setTimestamp(ts + detail::kTouchStrokeArmMs);
  widget->touchEvent(&end);
}

QTEST_MAIN(TouchNavigationTest)
#include "touchnavigationtest.moc"
