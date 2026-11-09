// Private header for the GLWidget touch input state machine (add-touch-navigation).
// Kept in a paired header rather than an anonymous namespace so the unit test
// (touchnavigationtest) can drive the machine directly with constructed
// QEventPoint lists — see design D6/D7. Not part of the public API.
#pragma once

#include <QEvent>
#include <QEventPoint>
#include <QMouseEvent>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QSet>

namespace Avogadro {
namespace detail {

//! Default jitter threshold (design D2): a single-finger touch must travel at
//! least this many logical pixels before it is treated as a stroke rather than
//! a tap. A named constant so it is tuned in one place (device smoke, task 5).
constexpr double kTouchSlopLogicalPx = 10.0;

//! Stroke-arming grace window (design D2, task 5.5): once a single finger has
//! crossed the slop it enters an ARMED STROKE in which the press is HELD for
//! this many milliseconds before it is emitted to the tool. The hold lets a
//! second finger that arrives during the window form a two-finger GESTURE
//! (pinch) instead of the stroke drawing an atom (the edit tool sees the held
//! press). A genuine one-finger stroke proceeds once the window elapses. Tuned
//! from device evidence (task 5); a named constant so it changes in one place.
constexpr qint64 kTouchStrokeArmMs = 50;

//! Result of feeding one touch event through the touch state machine.
//!
//! The state machine is GL-independent: it emits synthesized mouse events
//! (widget-local logical positions, to be scaled by GLWidget exactly like a
//! real mouse event) plus raw camera deltas that GLWidget maps onto the
//! existing Navigate::zoom / Navigate::translate primitives (design D4).
//! A synthesized mouse event, stored as plain data (Qt's QMouseEvent is
//! non-copyable/non-movable, so it cannot live in a QList). GLWidget
//! reconstructs the QMouseEvent when it dispatches (dispatchMouse), so the
//! state machine stays GL- and Qt-event-implementation independent and the
//! unit test can assert on the logical position without the (0,0) position()
//! quirk of constructed QEventPoints.
struct SyntheticMouseEvent {
  QEvent::Type type = QEvent::None;
  QPointF position;
  Qt::MouseButton button = Qt::NoButton;
  Qt::MouseButtons buttons = Qt::NoButton;
  Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};

struct TouchResult {
  //! Synthesized mouse events to dispatch to the active tool.
  QList<SyntheticMouseEvent> mouseEvents;
  //! Signed change in the distance between the two tracked gesture points
  //! (logical pixels; positive = fingers spread apart). 0.0 outside a gesture.
  double zoomDistanceDelta = 0.0;
  //! Logical-pixel centroid of the gesture pair at the previous and current
  //! event. Valid only while a gesture is in flight.
  QPoint centroidFrom;
  QPoint centroidTo;
  //! True when this event should drive the camera (a two-finger gesture).
  bool cameraActive = false;

  //! Append a synthesized press+release pair (a click) at `pos`.
  void emitClick(const QPointF &pos)
  {
    mouseEvents << SyntheticMouseEvent{ QEvent::MouseButtonPress, pos,
                                        Qt::LeftButton, Qt::LeftButton,
                                        Qt::NoModifier }
                << SyntheticMouseEvent{ QEvent::MouseButtonRelease, pos,
                                        Qt::LeftButton, Qt::NoButton,
                                        Qt::NoModifier };
  }

  //! Append a synthesized press at `pos`.
  void emitPress(const QPointF &pos)
  {
    mouseEvents << SyntheticMouseEvent{ QEvent::MouseButtonPress, pos,
                                        Qt::LeftButton, Qt::LeftButton,
                                        Qt::NoModifier };
  }

  //! Append a synthesized move at `pos`.
  void emitMove(const QPointF &pos)
  {
    mouseEvents << SyntheticMouseEvent{ QEvent::MouseMove, pos, Qt::NoButton,
                                        Qt::LeftButton, Qt::NoModifier };
  }

  //! Append a synthesized release at `pos`.
  void emitRelease(const QPointF &pos)
  {
    mouseEvents << SyntheticMouseEvent{ QEvent::MouseButtonRelease, pos,
                                        Qt::LeftButton, Qt::NoButton,
                                        Qt::NoModifier };
  }
};

//! Per-widget touch input state machine (add-touch-navigation, design D2/D6).
//!
//! Owns the state, the touch-point identity tracking, and the second-class
//! blacklist; consumes a list of QEventPoints and emits synthesized mouse
//! events plus camera deltas. There is NO global or static state, so two
//! GLWidget instances track their touch interactions independently and the
//! machine is unit-testable without a GL context or a real touch stack.
//!
//! State diagram (keyed by touch-point id):
//!   touch (1 pt) ──► PENDING (nothing sent to tools)
//!                    ├─ move >= slop ─────► STROKE (ARMED: press held)
//!                    ├─ quick release < slop ─► synthesized click
//!                    └─ 2nd point observed ─► GESTURE
//!   STROKE (armed) ─► GESTURE if a 2nd finger arrives during the arming
//!   window (a pinch, no atom drawn); otherwise the held press is emitted and
//!   the stroke proceeds.
//!   GESTURE (pinch zoom / centroid pan), tracked by the two original ids;
//!   ends when either original lifts, leaving the camera where it was.
//!   Any point that arrives while an interaction is in flight is second-class:
//!   ignored until it lifts, never promoted (also palm rejection). The
//!   blacklist survives stroke end, so a resting finger never wakes as a new
//!   touch, and a third finger never substitutes into a broken pinch pair.
class A_EXPORT TouchState {
public:
  enum State { Idle, Pending, Stroke, Gesture };

  //! Which touch phase one input event represents. Kept independent of Qt's
  //! event-type representation (this Qt build's QTouchEvent has no
  //! EventType enum and its isBeginEvent()/isEndEvent() helpers are stubs that
  //! always return true), so GLWidget::touchEvent() derives the phase from
  //! event->type() and the unit test drives the machine directly here.
  enum Phase { Begin, Update, End, Cancel };

  //! @param slopLogicalPx jitter threshold in logical pixels (design D2: ~10).
  explicit TouchState(double slopLogicalPx) : m_slop(slopLogicalPx) {}

  //! Advance the state machine by one touch event and return what to apply.
  //! @param points active touch points (QTouchEvent::points())
  //! @param phase  the touch phase of this event (Begin/Update/End)
  //! @param timestamp event timestamp in milliseconds
  TouchResult process(const QList<QEventPoint> &points,
                      Phase phase, qint64 timestamp);

  //! Test hook: the current state.
  State state() const { return m_state; }

private:
  const QEventPoint *find(const QList<QEventPoint> &points, int id) const;
  //! Widget-local logical position of a point. We read scenePosition(): in Qt
  //! 6 widget delivery scenePosition() equals globalPosition() (global logical
  //! screen coords — qguiapplication.cpp sets setScenePosition(ep,
  //! tempPt.globalPosition())), while position() = mapFromGlobal(globalPosition())
  //! (qapplication.cpp updateTouchPointsForWidget rewrites only position()). On
  //! the embedded GLWidget the canvas sits below toolbar/dock chrome and the
  //! window sits somewhere on screen, so the GLOBAL offset is corrected at the
  //! GLWidget::touchEvent boundary (subtracting mapToGlobal(0,0), design D9)
  //! rather than here, keeping the machine unit-testable with constructed
  //! events whose scenePosition is set. The machine's internal math (slop, zoom
  //! delta, centroid delta) is translation-invariant, so global is fine
  //! internally; only the absolute synthesized position needs the widget-local
  //! correction.
  QPointF posOf(const QEventPoint &p) const { return p.scenePosition(); }
  bool isTracked(int id) const;
  bool isLive(const QEventPoint &p) const
  {
    return !m_blacklist.contains(p.id()) && p.state() != QEventPoint::Released;
  }
  static double distance(const QPointF &a, const QPointF &b)
  {
    return (b - a).manhattanLength();
  }
  static QPointF centroid(const QPointF &a, const QPointF &b)
  {
    return QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
  }
  void onBegin(const QList<QEventPoint> &points, TouchResult &result);
  void onUpdate(const QList<QEventPoint> &points, qint64 timestamp,
                TouchResult &result);
  void onEnd(const QList<QEventPoint> &points, qint64 timestamp,
             TouchResult &result);
  //! QEvent::TouchCancel: end the session per design D9 — PENDING → Idle with
  //! no events (never a phantom click); STROKE → emit the synthesized release
  //! so the tool's stroke ends cleanly, then Idle; GESTURE → Idle holding the
  //! camera; the blacklist is cleared.
  void onCancel(TouchResult &result);
  //! Blacklist every new live point that is not one of `keep` (the tracked
  //! ids for the current state). Implements the second-class rule.
  void blacklistNew(const QList<QEventPoint> &points,
                    const QSet<int> &keep);

  double m_slop;
  State m_state = Idle;

  // PENDING / STROKE: the single tracked point and where the stroke began.
  int m_primaryId = -1;
  QPointF m_primaryStart;
  qint64 m_primaryTimestamp = 0;
  QPointF m_primaryLastPos;

  // STROKE arming (design D2, task 5.5): while arming the press is held so a
  // second finger arriving in the window can still form a pinch. m_strokePressPos
  // is the slop-crossing position the held press is emitted at; m_armStart
  // is the timestamp the arming window started.
  bool m_strokeArming = false;
  QPointF m_strokePressPos;
  qint64 m_armStart = 0;

  // GESTURE: the two original point ids and the last distance/centroid.
  int m_gestureId1 = -1;
  int m_gestureId2 = -1;
  double m_lastDistance = 0.0;
  QPointF m_lastCentroid;
  bool m_centroidValid = false;

  // Second-class blacklist (palm rejection): ids ignored until they lift.
  QSet<int> m_blacklist;
};

}  // namespace detail
}  // namespace Avogadro
