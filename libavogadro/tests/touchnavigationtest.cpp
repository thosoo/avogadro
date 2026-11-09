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
  void testBothFingersBeginGoesStraightToGesture();
  void testPinchSpreadZoomsInAndSqueezeZoomsOut();
  void testTwoFingerPanMovesCentroid();
  void testGestureEndHoldsCamera();
  void testPalmMidStrokeIsBlacklisted();
  void testRestingFingerNeverWakes();
  void testThirdFingerDoesNotSubstituteIntoPinch();
  void testSecondPointDuringPendingBecomesGesture();

  // --- Widget integration (touchEvent -> real tool) ------------------------
  void testTapErasesAtom();
  void testLassoThroughSynthesis();
  void testMouseRegressionGuard();
  void testSynthesizedPositionScalesWithDpr();
  void testOneFingerRotateWithNavigateTool();

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
  // stays PENDING; once it crosses the slop it becomes a STROKE (press first).
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

  // A 20-px move crosses slop: a press at the traveled-to position, STROKE.
  QList<QEventPoint> stroke;
  stroke << makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0));
  r = ts.process(stroke, detail::TouchState::Update, 40);
  QCOMPARE(static_cast<int>(r.mouseEvents.size()), 1);
  QCOMPARE(r.mouseEvents.first().type, QEvent::MouseButtonPress);
  QCOMPARE(r.mouseEvents.first().position, QPointF(120.0, 100.0));
  QCOMPARE(ts.state(), detail::TouchState::Stroke);
}

void TouchNavigationTest::testStrokeSynthesizesPressMoveRelease()
{
  // A stroke produces a press at the traveled-to position, move events, then a
  // release, in order (spec: one-finger stroke draws a bond).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  const QList<TouchStep> seq = {
    {{ makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0)) },
     detail::TouchState::Begin, 0},
    {{ makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0)) },
     detail::TouchState::Update, 20}, // >= slop -> press at (120,100)
    {{ makePoint(0, QEventPoint::Updated, QPointF(130.0, 110.0)) },
     detail::TouchState::Update, 40}, // move
    {{ makePoint(0, QEventPoint::Released, QPointF(130.0, 110.0)) },
     detail::TouchState::End, 60}, // release at last pos
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
  // (spec: palm lands mid-stroke).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  const QList<TouchStep> seq = {
    {{ makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0)) },
     detail::TouchState::Begin, 0},
    {{ makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0)) },
     detail::TouchState::Update, 20}, // press (stroke begins)
    {{ makePoint(0, QEventPoint::Updated, QPointF(130.0, 110.0)),
      makePoint(1, QEventPoint::Updated, QPointF(400.0, 400.0)) },
     detail::TouchState::Update, 40}, // palm lands; move still from finger 0
    {{ makePoint(0, QEventPoint::Updated, QPointF(140.0, 120.0)),
      makePoint(1, QEventPoint::Updated, QPointF(400.0, 400.0)) },
     detail::TouchState::Update, 60}, // another move from finger 0
    {{ makePoint(0, QEventPoint::Released, QPointF(140.0, 120.0)),
      makePoint(1, QEventPoint::Updated, QPointF(400.0, 400.0)) },
     detail::TouchState::End, 80}, // release finger 0
  };
  const auto out = driveMachine(ts, seq);

  QCOMPARE(ts.state(), detail::TouchState::Idle);
  // press, move, move, release -- the palm (finger 1) contributed nothing.
  QCOMPARE(static_cast<int>(out.size()), 4);
  QCOMPARE(out[0].type, QEvent::MouseButtonPress);
  QCOMPARE(out[0].position, QPointF(120.0, 100.0));
  QCOMPARE(out[1].type, QEvent::MouseMove);
  QCOMPARE(out[1].position, QPointF(130.0, 110.0));
  QCOMPARE(out[2].type, QEvent::MouseMove);
  QCOMPARE(out[2].position, QPointF(140.0, 120.0));
  QCOMPARE(out[3].type, QEvent::MouseButtonRelease);
  QCOMPARE(out[3].position, QPointF(140.0, 120.0));
}

void TouchNavigationTest::testRestingFingerNeverWakes()
{
  // A finger that rests while a stroke finishes and then lifts never wakes up
  // as a new touch: its movement and its lift produce zero events
  // (spec: resting finger never wakes).
  detail::TouchState ts(detail::kTouchSlopLogicalPx);
  QList<TouchStep> seq = {
    {{ makePoint(0, QEventPoint::Pressed, QPointF(100.0, 100.0)) },
     detail::TouchState::Begin, 0},
    {{ makePoint(0, QEventPoint::Updated, QPointF(120.0, 100.0)) },
     detail::TouchState::Update, 20}, // press
  };
  std::vector<detail::SyntheticMouseEvent> out = driveMachine(ts, seq);
  const size_t afterStroke = out.size();

  // A second finger lands and rests.
  QList<QEventPoint> palm;
  palm << makePoint(0, QEventPoint::Updated, QPointF(130.0, 110.0));
  palm << makePoint(1, QEventPoint::Updated, QPointF(400.0, 400.0));
  ts.process(palm, detail::TouchState::Update, 40);

  // Finger 0 lifts (stroke release); finger 1 still rests (still Pressed).
  QList<QEventPoint> release0;
  release0 << makePoint(0, QEventPoint::Released, QPointF(130.0, 110.0));
  release0 << makePoint(1, QEventPoint::Pressed, QPointF(400.0, 400.0));
  ts.process(release0, detail::TouchState::End, 60);

  // The resting finger moves: ignored (blacklist survives stroke end).
  QList<QEventPoint> restMove;
  restMove << makePoint(1, QEventPoint::Updated, QPointF(405.0, 405.0));
  const detail::TouchResult r1 =
    ts.process(restMove, detail::TouchState::Update, 80);
  QVERIFY(r1.mouseEvents.isEmpty());
  QCOMPARE(ts.state(), detail::TouchState::Idle);

  // The resting finger lifts: still nothing (spec).
  QList<QEventPoint> restLift;
  restLift << makePoint(1, QEventPoint::Released, QPointF(405.0, 405.0));
  const detail::TouchResult r2 =
    ts.process(restLift, detail::TouchState::End, 100);
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
  // navigate tool and rotate the view.
  const QPointF p0(300.0, 300.0);
  const qreal dpr = widget->devicePixelRatioF();
  QList<QEventPoint> begin;
  begin << makePoint(0, QEventPoint::Pressed, p0 / dpr);
  QTouchEvent beginEvent(QEvent::TouchBegin, nullptr, Qt::NoModifier, begin);
  widget->touchEvent(&beginEvent);

  for (int i = 1; i <= 3; ++i) {
    QList<QEventPoint> m;
    m << makePoint(0, QEventPoint::Updated, QPointF(300.0 + 20.0 * i, 300.0 + 10.0 * i) / dpr);
    QTouchEvent move(QEvent::TouchUpdate, nullptr, Qt::NoModifier, m);
    widget->touchEvent(&move);
  }

  QList<QEventPoint> end;
  end << makePoint(0, QEventPoint::Released, QPointF(360.0, 330.0) / dpr);
  QTouchEvent endEvent(QEvent::TouchEnd, nullptr, Qt::NoModifier, end);
  widget->touchEvent(&endEvent);

  // Eigen::Projective3d's != is ambiguous against Qt's operators, so compare
  // with the member isApprox instead.
  QVERIFY(!before.isApprox(widget->camera()->modelview())); // the camera orbited

  delete widget;
  delete mol;
}

void TouchNavigationTest::driveTouchLasso(GLWidget *widget, EraseTool &tool,
                                          const QVector<QPointF> &loopLogical)
{
  (void)tool; // the erase tool is already installed on the widget
  QList<QEventPoint> pts;
  pts << makePoint(0, QEventPoint::Pressed, loopLogical.first());
  QTouchEvent begin(QEvent::TouchBegin, nullptr, Qt::NoModifier, pts);
  widget->touchEvent(&begin);

  for (int i = 1; i < loopLogical.size(); ++i) {
    QList<QEventPoint> mpts;
    mpts << makePoint(0, QEventPoint::Updated, loopLogical[i]);
    QTouchEvent move(QEvent::TouchUpdate, nullptr, Qt::NoModifier, mpts);
    widget->touchEvent(&move);
  }

  QList<QEventPoint> rpts;
  rpts << makePoint(0, QEventPoint::Released, loopLogical.last());
  QTouchEvent end(QEvent::TouchEnd, nullptr, Qt::NoModifier, rpts);
  widget->touchEvent(&end);
}

QTEST_MAIN(TouchNavigationTest)
#include "touchnavigationtest.moc"
