/**********************************************************************
  GLWidget - general OpenGL display

  Copyright (C) 2006-2009 Geoffrey R. Hutchison
  Copyright (C) 2006,2007 Donald Ephraim Curtis
  Copyright (C) 2007      Benoit Jacob
  Copyright (C) 2007-2009 Marcus D. Hanwell
  Copyright (C) 2011      David C. Lonie

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 **********************************************************************/

// krazy:excludeall=includes
#include "config.h"

#include "camera.h"
#include "glwidget.h"
#include "glwidget_touch_p.h"
#include "undostack.h"
#include "glpainter_p.h"
#include "glhit.h"
#include "navigate.h"

#include <QTouchEvent>

#include "rendering/modernbackend.h"
#include "rendering/compatbackend.h"
#include "rendering/renderertier.h"
#include "rendering/builderpool.h"
#include "rendering/builderadoption.h"
#include "rendering/scenesnapshot.h"
#include "rendering/snapshottessellator.h"
#include "rendering/selectrefine.h"
#include "rendering/records.h"
#include "mesh.h"
#include <QOpenGLFunctions>
#include "rendering/scenebuilder.h"

#include <QMessageBox>
#include <QSet>
#include <QPen>
#include <QPainter>
#include <QPaintEngine>
#include <QOpenGLContext>
#include <QUndoStack>
#include <QLabel>

#ifdef Q_OS_MAC
# include <OpenGL/glu.h>
#else
# include <GL/glu.h>
#endif

#ifdef ENABLE_PYTHON
  #include "pythonthread_p.h"
  #include "pythonextension_p.h"
#endif

#include <avogadro/painterdevice.h>
#include <avogadro/tool.h>
#include <avogadro/toolgroup.h>
#include <avogadro/extension.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/residue.h>
#include <avogadro/molecule.h>
#include <avogadro/color.h>
#include <avogadro/engine.h>

#include "pluginmanager.h"

#include <QDebug>
#include <QDir>
#include <QPluginLoader>
#include <QPointer>
#include <QReadWriteLock>
#include <QElapsedTimer>
#include <QMutex>

#ifdef ENABLE_THREADED_GL
  #include <QWaitCondition>
  #include <QThread>
#endif

#include <Eigen/Geometry>

#ifdef ENABLE_GLSL
  #include <GL/glew.h>
#endif

#include <algorithm>
#include <vector>
#include <cstdlib>

#include <openbabel/mol.h>
#include <openbabel/generic.h>
#include <openbabel/math/vector3.h>

using namespace OpenBabel;
using namespace Eigen;

namespace Avogadro {
void GLWidget::showEvent(QShowEvent *event)
{
  QOpenGLWidget::showEvent(event);
  // Schedule a viewport update once the event loop returns so the
  // correct device pixel ratio is applied on the first paint.
  QMetaObject::invokeMethod(this, "updateViewport",
                           Qt::QueuedConnection);
}

void GLWidget::updateViewport()
{
  resizeGL(width(), height());
  update();
}

  bool engineLessThan( const Engine* lhs, const Engine* rhs )
  {
    Engine::Layers lhsLayers = lhs->layers();
    Engine::Layers rhsLayers = rhs->layers();
    Engine::PrimitiveTypes lhsPrimitives = lhs->primitiveTypes();
    Engine::PrimitiveTypes rhsPrimitives = rhs->primitiveTypes();

    if ( !( lhsLayers & Engine::Overlay ) && rhsLayers & Engine::Overlay ) {
      return true;
    } else if (( lhsLayers & Engine::Overlay ) && ( rhsLayers & Engine::Overlay ) ) {
      return lhs->transparencyDepth() < rhs->transparencyDepth();
    } else if (( lhsLayers & Engine::Overlay ) && !( rhsLayers & Engine::Overlay ) ) {
      return false;

    } else if ( !( lhsPrimitives & Engine::Molecules ) && rhsPrimitives & Engine::Molecules ) {
      return true;
    } else if (( lhsPrimitives & Engine::Molecules ) && ( rhsPrimitives & Engine::Molecules ) ) {
      return lhs->transparencyDepth() < rhs->transparencyDepth();
    } else if (( lhsPrimitives & Engine::Molecules ) && !( rhsPrimitives & Engine::Molecules ) ) {
      return false;

    } else if ( !( lhsPrimitives & Engine::Atoms ) && rhsPrimitives & Engine::Atoms ) {
      return true;
    } else if (( lhsPrimitives & Engine::Atoms ) && ( rhsPrimitives & Engine::Atoms ) ) {
      return lhs->transparencyDepth() < rhs->transparencyDepth();
    } else if (( lhsPrimitives & Engine::Atoms ) && !( rhsPrimitives & Engine::Atoms ) ) {
      return false;

    } else if ( !( lhsPrimitives & Engine::Bonds ) && rhsPrimitives & Engine::Bonds ) {
      return true;
    } else if (( lhsPrimitives & Engine::Bonds ) && ( rhsPrimitives & Engine::Bonds ) ) {
      return lhs->transparencyDepth() < rhs->transparencyDepth();
    } else if (( lhsPrimitives & Engine::Bonds ) && !( rhsPrimitives & Engine::Bonds ) ) {
      return false;
    }
    return false;
  }

  class GLPainterDevice : public PainterDevice
  {
  public:
    GLPainterDevice(GLWidget *gl) : widget( gl ), overridePainter( nullptr ) { }
    ~GLPainterDevice() {}

    //! Redirect every engine draw call to a different Painter (e.g. the
    //! SceneBuilder for the modern tier) instead of the widget's real painter.
    void setOverridePainter(Painter *p) { overridePainter = p; }
    void clearOverridePainter() { overridePainter = nullptr; }

    Painter *painter() const
    { return overridePainter ? overridePainter : widget->painter(); }
    Camera *camera() const { return widget->camera(); }
    bool isSelected( const Primitive *p ) const { return widget->isSelected(p); }
    double radius( const Primitive *p ) const { return widget->radius(p); }
    const Molecule *molecule() const { return widget->molecule(); }
    Color *colorMap() const { return widget->colorMap(); }

    int width() { return widget->width(); }
    int height() { return widget->height(); }

  private:
    GLWidget *widget;
    Painter *overridePainter;
  };

  class GLWidgetPrivate
  {
  public:
    GLWidgetPrivate() : background( 0,0,0,255 ),
                        aCells( 1 ), bCells( 1 ), cCells( 1 ),
                        onlyRenderOriginalUnitCell(false),
                        cellColor( 255,255,255 ),
                        molecule( 0 ),
                        camera( new Camera ),
                        projection( GLWidget::Perspective ),
                        tool( 0 ),
                        toolGroup( 0 ),
                        selectBuf( 0 ),
                        selectBufSize( -1 ),
                        undoStack(0),
#ifdef ENABLE_THREADED_GL
                        thread( 0 ),
#else
                        initialized( false ),
#endif
                        painter( 0 ),
                        colorMap( 0),
                        defaultColorMap( 0),
                        updateCache(true),
                        quickRender(false),
                        allowQuickRender(true),
                        renderUnitCellAxes(false),
                        fogLevel(0),
                        renderAxes(false),
                        renderDebug(false),
                        renderModelViewDebug(false),
                        dlistQuick(0), dlistOpaque(0), dlistTransparent(0),
                        pd(0), backend(0),
                        m_touch( detail::kTouchSlopLogicalPx )
    {
    }

    ~GLWidgetPrivate()
    {
      if ( selectBuf ) delete[] selectBuf;
      delete camera;
      delete backend;

      // free the display lists
      if (dlistQuick)
        glDeleteLists(dlistQuick, 1);
      if (dlistOpaque)
        glDeleteLists(dlistOpaque, 1);
      if (dlistTransparent)
        glDeleteLists(dlistTransparent, 1);
    }

    void updateListQuick();

    QList<Engine *>        engines;

    QColor                 background;

    Vector3d               normalVector;
    Vector3d               center;
    double                 radius;
    const Atom            *farthestAtom;

    //! number of unit cells in a, b, and c crystal directions
    unsigned char          aCells;
    unsigned char          bCells;
    unsigned char          cCells;

    bool onlyRenderOriginalUnitCell;

    GLWidget::ProjectionType projection;

    QColor                 cellColor;

    Molecule              *molecule;

    Camera                *camera;

    Tool                  *tool;
    ToolGroup             *toolGroup;
    QList<Extension*>     extensions;

    GLuint                *selectBuf;
    int                    selectBufSize;

    QList<QPair<QString, QPair<QList<unsigned int>, QList<unsigned int> > > > namedSelections;
    // Task 4.3 (design D6): a QSet gives O(1) contains/remove, so selecting or
    // checking every primitive on a large scene stays O(n) per frame rather than
    // O(n^2) as it was with the QList-backed PrimitiveList. The public API still
    // materializes a PrimitiveList (rare calls) so consumers are untouched.
    QSet<Primitive *>      selectedPrimitives;

    UndoStack             *undoStack;

#ifdef ENABLE_THREADED_GL
    QWaitCondition         paintCondition;
    QMutex                 renderMutex;

    GLThread              *thread;
#else
    bool                   initialized;
#endif

    GLPainter             *painter;
    Color                 *colorMap; // global color map
    Color                 *defaultColorMap;  // default fall-back coloring (i.e., by elements)
    bool                   updateCache; // Update engine caches in quick render?
    bool                   quickRender; // Are we using quick render?
    bool                   allowQuickRender; // Are we allowed to use quick render?
    bool                   renderUnitCellAxes; // Do we render the unit cell axes
    int                    fogLevel;    // The level of fog to use (0=none, 9=max)
    bool                   renderAxes;  // Should the x, y, z axes be rendered?
    bool                   renderDebug; // Should the debug information be shown?
    bool                   renderModelViewDebug; // Should the modelview matrix be shown?

    GLuint                 dlistQuick;
    GLuint                 dlistOpaque;
    GLuint                 dlistTransparent;

    QMutex                 textOverlayMutex; // Protects textOverlayLabels
    QList<QPointer<QLabel> > textOverlayLabels; // List of labels to render
    /**
      * Member GLPainterDevice which is passed to the engines.
      */
    GLPainterDevice *pd;

    //! Modern-tier backend (task 2.4). Owns the instanced geometry VAOs,
    //! shaders and instance VBOs, and issues the bounded instanced draws.
    //! Created on the GL context in initializeGL(); rebuilt on context loss.
    rendering::ModernRendererBackend *backend = nullptr;
    //! SceneBuilder: the Painter engines emit into it (via the pd override),
    //! producing the chunk stores the backend uploads and draws.
    rendering::SceneBuilder builder;
    //! Whether render() uses the modern instanced path (true) or the legacy
    //! display-list/GL painter path (false). Defaulted true for GL 3.3 contexts;
    //! the engines are painter-agnostic so they emit to the SceneBuilder.
    bool useModernTier = true;
    //! Quality level (mapped from the dialog's 0-4 slider) that must reach the
    //! modern backend. Stashed here because readSettings() calls setQuality()
    //! at startup, before the backend exists (created in initializeGL / the
    //! context-loss recovery); applying the level only when the backend is
    //! live would silently lose a persisted Low/High quality after a restart.
    rendering::LodLevel pendingDetailLevel = rendering::LodMedium;
    //! Resolved runtime tier (design D7): modern, compat or legacy. The compat
    //! tier currently renders through the same fixed-function path as legacy
    //! (task 7.2 replaces that with the batched chunk-store flush); the tier
    //! is resolved and logged distinctly from task 7.1 on.
    rendering::RendererTier rendererTier = rendering::RendererTier::Modern;
    //! Compat-tier submit stage (task 7.2): engines emit the same chunk
    //! stores the modern tier consumes, flushed through fixed function. Only
    //! created when the resolved tier is Compat; null otherwise.
    rendering::CompatRendererBackend *compatBackend = nullptr;
    //! Worker pool (gpu-rendering-pipeline task 6.1, design A7/D3): rebuilds
    //! the scene off the GUI thread while paintGL keeps presenting the last
    //! ready chunks. Null until the first modern-tier frame creates it.
    rendering::BuilderPool *builderPool = nullptr;
    //! Revision currently submitted to the pool (0 = nothing submitted).
    unsigned int submittedRevision = 0;
    //! Newest worker result and the revision it was built for. Results are
    //! tagged with the molecule revision; stale ones are discarded.
    rendering::TessellationResult workerResult;
    unsigned int readyRevision = 0;
    bool hasWorkerResult = false;
    //! Generation of the last ADOPTED worker result (design D1 /
    //! fix-modern-tier-revision-churn). Adoption is forward-progress:
    //! builderResultReady adopts any result strictly newer than this and
    //! discards everything older-or-equal, so under sustained revision churn
    //! the display trails by at most one build instead of starving on an
    //! exact molecule->revision() match. Monotonic — never regresses.
    unsigned int presentedRevision = 0;
    //! Revision the backend's chunk stores currently reflect, and whether any
    //! stores exist at all. Rotation frames skip re-emission entirely.
    unsigned long emittedRevision = 0;
    bool storesValid = false;
    //! Set when an engine reports changed() (settings, selection colouring):
    //! forces re-emission even though the molecule revision is unchanged.
    bool enginesDirty = true;
    //! Backend counters exposed to the debug overlay (draw calls / frames of
    //! the most recent modern frame).
    unsigned int backendDrawCalls = 0;
    unsigned int backendFrames = 0;

    //! Per-widget touch input state machine (add-touch-navigation). Owns the
    //! PENDING/STROKE/GESTURE state, touch-point identity tracking and the
    //! second-class blacklist; fed from GLWidget::touchEvent(). No static state
    //! (design D6): two widgets track their touch interactions independently.
    detail::TouchState m_touch;
  };

  void GLWidgetPrivate::updateListQuick()
  {
    // Always regenerate the quick rendering list. In earlier versions
    // this was only done when updateCache was set, which caused outdated
    // geometry to be shown while the view was being manipulated.

    if (dlistQuick == 0)
      dlistQuick = glGenLists(1);

    // Don't use dynamic scaling when rendering quickly
    painter->setDynamicScaling(false);

    glNewList(dlistQuick, GL_COMPILE);
    foreach (Engine *engine, engines) {
      if (engine->isEnabled()) {
        molecule->lock()->lockForRead();
        engine->renderQuick(pd);
        molecule->lock()->unlock();
      }
    }
    glEndList();

    // Keep track that the quick cache is current
    updateCache = false;
    painter->setDynamicScaling(true);
  }


// ---------------------------------------------------------------------------
// TouchState (add-touch-navigation) — see glwidget_touch_p.h for the contract.
// ---------------------------------------------------------------------------

namespace detail {

  const QEventPoint *TouchState::find(const QList<QEventPoint> &points,
                                      int id) const
  {
    for (const QEventPoint &p : points)
      if (p.id() == id)
        return &p;
    return nullptr;
  }

  bool TouchState::isTracked(int id) const
  {
    switch (m_state) {
      case Pending:
      case Stroke:
        return id == m_primaryId;
      case Gesture:
        return id == m_gestureId1 || id == m_gestureId2;
      case Idle:
      default:
        return false;
    }
  }

  // Second-class rule: blacklist every new live point that is not one of the
  // currently tracked ids (a palm / extra finger). Lifted points and points
  // already on the blacklist are left alone.
  void TouchState::blacklistNew(const QList<QEventPoint> &points,
                                const QSet<int> &keep)
  {
    for (const QEventPoint &p : points) {
      if (keep.contains(p.id()) || m_blacklist.contains(p.id()))
        continue;
      if (p.state() == QEventPoint::Released)
        continue;
      m_blacklist.insert(p.id());
    }
  }

  void TouchState::onBegin(const QList<QEventPoint> &points,
                           TouchResult &result)
  {
    QList<int> live;
    for (const QEventPoint &p : points)
      if (isLive(p))
        live.append(p.id());

    if (live.size() >= 2) {
      // Both fingers landed together: straight to GESTURE, no PENDING state and
      // no synthesized tool events (spec: both-fingers-begin).
      m_state = Gesture;
      m_gestureId1 = live.first();
      m_gestureId2 = live.at(1);
      const QPointF a = posOf(*find(points, m_gestureId1));
      const QPointF b = posOf(*find(points, m_gestureId2));
      m_lastDistance = distance(a, b);
      m_lastCentroid = centroid(a, b);
      m_centroidValid = true;
      result.cameraActive = true;
      return;
    }

    if (live.size() == 1) {
      // PENDING: nothing is sent to any tool yet.
      m_state = Pending;
      m_primaryId = live.first();
      const QPointF pos = posOf(*find(points, m_primaryId));
      m_primaryStart = pos;
      m_primaryLastPos = pos;
      m_primaryTimestamp = 0;
      return;
    }

    m_state = Idle;
  }

  void TouchState::onUpdate(const QList<QEventPoint> &points,
                            qint64 /* timestamp */,
                            TouchResult &result)
  {
    switch (m_state) {
      case Pending: {
        // A second live point observed while pending promotes straight to a
        // two-finger gesture (no tap, no stroke).
        int second = -1;
        for (const QEventPoint &p : points) {
          if (p.id() == m_primaryId || m_blacklist.contains(p.id()) ||
              p.state() == QEventPoint::Released)
            continue;
          second = p.id();
          break;
        }
        if (second != -1) {
          m_state = Gesture;
          m_gestureId1 = m_primaryId;
          m_gestureId2 = second;
          const QPointF a = posOf(*find(points, m_gestureId1));
          const QPointF b = posOf(*find(points, m_gestureId2));
          m_lastDistance = distance(a, b);
          m_lastCentroid = centroid(a, b);
          m_centroidValid = true;
          result.cameraActive = true;
          return;
        }

        const QEventPoint *primary = find(points, m_primaryId);
        if (!primary || primary->state() == QEventPoint::Released) {
          // Primary lifted while still pending: a sub-slop release is a tap.
          if (distance(m_primaryStart, m_primaryLastPos) < m_slop)
            result.emitClick(m_primaryStart);
          m_state = Idle;
          m_primaryId = -1;
          return;
        }

        const QPointF pos = posOf(*primary);
        m_primaryLastPos = pos;
        // PENDING -> STROKE: travel at or above the slop synthesizes a press at
        // the traveled-to position, then moves follow.
        if (distance(m_primaryStart, pos) >= m_slop) {
          m_state = Stroke;
          result.emitPress(pos);
        }
        // Below the slop: stay PENDING and emit nothing (jitter swallow).
        break;
      }

      case Stroke: {
        // Second-class rule: any new live point (palm / extra finger) is
        // blacklisted; the stroke itself is unaffected.
        QSet<int> keep;
        keep.insert(m_primaryId);
        blacklistNew(points, keep);

        const QEventPoint *primary = find(points, m_primaryId);
        if (primary && primary->state() != QEventPoint::Released) {
          const QPointF pos = posOf(*primary);
          m_primaryLastPos = pos;
          result.emitMove(pos);
        }
        break;
      }

      case Gesture: {
        QSet<int> keep;
        keep.insert(m_gestureId1);
        keep.insert(m_gestureId2);
        blacklistNew(points, keep);

        const QEventPoint *p1 = find(points, m_gestureId1);
        const QEventPoint *p2 = find(points, m_gestureId2);
        if (!p1 || !p2 || p1->state() == QEventPoint::Released ||
            p2->state() == QEventPoint::Released) {
          // Either original point lifted: the gesture ends and the camera
          // stays where the gesture left it.
          m_state = Idle;
          m_gestureId1 = m_gestureId2 = -1;
          m_centroidValid = false;
          return;
        }
        const QPointF a = posOf(*p1);
        const QPointF b = posOf(*p2);
        const double newDist = distance(a, b);
        result.zoomDistanceDelta = newDist - m_lastDistance;
        result.centroidFrom = m_lastCentroid.toPoint();
        result.centroidTo = centroid(a, b).toPoint();
        result.cameraActive = true;
        m_lastDistance = newDist;
        m_lastCentroid = centroid(a, b);
        break;
      }

      case Idle:
      default:
        break;
    }
  }

  void TouchState::onEnd(const QList<QEventPoint> &points,
                         qint64 /* timestamp */,
                         TouchResult &result)
  {
    switch (m_state) {
      case Pending: {
        // Quick release below the slop radius is a synthesized click (a
        // press+release pair at the touch position).
        if (distance(m_primaryStart, m_primaryLastPos) < m_slop)
          result.emitClick(m_primaryStart);
        m_state = Idle;
        m_primaryId = -1;
        break;
      }

      case Stroke: {
        // Synthesize the release at the primary's last known position. The
        // blacklist survives: a resting finger that lifts after the stroke is
        // still ignored (it is removed from the blacklist only when it lifts).
        const QEventPoint *primary = find(points, m_primaryId);
        const QPointF pos = primary ? posOf(*primary) : m_primaryLastPos;
        result.emitRelease(pos);
        m_state = Idle;
        m_primaryId = -1;
        break;
      }

      case Gesture: {
        // Either (or both) original points lifted: the gesture ends and the
        // camera holds its final pose.
        m_state = Idle;
        m_gestureId1 = m_gestureId2 = -1;
        m_centroidValid = false;
        break;
      }

      case Idle:
      default:
        break;
    }
  }

  TouchResult TouchState::process(const QList<QEventPoint> &points,
                                  Phase phase, qint64 timestamp)
  {
    TouchResult result;

    // A point that lifted in this event leaves the blacklist, so a resting
    // finger that lifts (after a stroke or a gesture) never stays second-class
    // and never wakes up as a fresh touch.
    for (const QEventPoint &p : points)
      if (p.state() == QEventPoint::Released)
        m_blacklist.remove(p.id());

    switch (phase) {
      case Begin:
        onBegin(points, result);
        break;
      case Update:
        onUpdate(points, timestamp, result);
        break;
      case End:
        onEnd(points, timestamp, result);
        break;
      default:
        break;
    }
    return result;
  }

}  // namespace detail


#ifdef ENABLE_THREADED_GL
  class GLThread : public QThread
  {
  public:
    GLThread( GLWidget *widget, QObject *parent );

    void run();
    void resize( int width, int height );
    void stop();

  private:
    GLWidget *m_widget;
    QOpenGLContext *m_context;
    bool m_running;
    bool m_resize;
    bool m_initialized;

    int m_width;
    int m_height;

  };

  GLThread::GLThread( GLWidget *widget, QObject *parent ) : QThread( parent ),
                                                            m_widget( widget ), m_running( true ), m_resize( false ), m_initialized( false )
  {}

  void GLThread::run()
  {
    GLWidgetPrivate *d = m_widget->d;

    while ( true ) {
      // lock the mutex
      d->renderMutex.lock();

      // unlock and wait
      d->paintCondition.wait( &( d->renderMutex ) );
      if ( !m_running ) {
        d->renderMutex.unlock();
        break;
      }
      m_widget->makeCurrent();

      if ( !m_initialized ) {
        m_widget->initializeGL();
        m_initialized = true;
      }

      if ( m_resize ) {
        m_widget->resizeGL( m_width, m_height );
        m_resize=false;
      }

      // Always clear with opaque alpha to ensure solid background on Wayland
      const QColor &c = d->background;
      glClearColor(c.redF(), c.greenF(), c.blueF(), 1.0f);
      m_widget->paintGL();
      m_widget->context()->swapBuffers(m_widget->context()->surface());
      m_widget->doneCurrent();
      d->renderMutex.unlock();
    }
  }

  void GLThread::resize( int width, int height )
  {
    m_resize = true;
    m_width = width;
    m_height = height;
  }

  void GLThread::stop()
  {
    m_running = false;
  }
#endif

GLWidget::GLWidget( QWidget *parent )
  : QOpenGLWidget( parent ), d( new GLWidgetPrivate )
  {
    constructor();
  }

GLWidget::GLWidget( const QSurfaceFormat &format, QWidget *parent,
                    const GLWidget *shareWidget )
  : QOpenGLWidget( parent ), d( new GLWidgetPrivate )
{
  setFormat(format);
  constructor(shareWidget);
}

GLWidget::GLWidget( Molecule *molecule,
                    const QSurfaceFormat &format, QWidget *parent,
                    const GLWidget *shareWidget )
  : QOpenGLWidget( parent ), d( new GLWidgetPrivate )
  {
    setFormat(format);
    constructor(shareWidget);
    setMolecule( molecule );
  }

  GLWidget::~GLWidget()
  {
    // GL objects owned here — the painter's display lists / textures / mesh
    // VBOs, engine textures, and GLWidgetPrivate's display lists — must be
    // deleted with a context current. Without one the calls silently drop or,
    // with the shared painter (MainWindow::newGLWidget), touch another
    // context's names. makeCurrent()/doneCurrent() are public on QOpenGLWidget
    // (Qt 6) and this derived destructor body runs before the base class tears
    // the context down. Headless / never-shown widgets have no context and
    // simply skip the GL calls. (fix-shared-gl-contexts)
    const bool haveContext = context() && context()->isValid();
    if (haveContext)
      makeCurrent();

    if(!d->painter->isShared())
      delete d->painter;
    else
      d->painter->decrementShare();

#ifdef ENABLE_THREADED_GL
    // cleanup our thread
    d->thread->stop();
    d->paintCondition.wakeAll();
    d->thread->wait();
#endif

#ifdef ENABLE_PYTHON
    // Creating the PythonThread object in Engine destructor doesn't seem
    // to work so we do it here
    PythonThread pt;
#endif

    // delete the engines
    foreach(Engine *engine, d->engines)
      delete engine;

    delete( d );

    if (haveContext)
      doneCurrent();
  }

  void GLWidget::constructor(const GLWidget *shareWidget)
  {
    // Make sure we get keyboard events
    setFocusPolicy(Qt::StrongFocus);

    // Accept touch input so Qt delivers QTouchEvents to touchEvent() instead of
    // synthesizing primary-point-only mouse events (add-touch-navigation task
    // 1.1). Without this, the second finger is discarded by Qt's touch-to-mouse
    // synthesis and two-finger gestures are unreachable.
    setAttribute(Qt::WA_AcceptTouchEvents, true);

    // Worker pool (gpu-rendering-pipeline task 6.1, design A7): rebuilds the
    // scene off the GUI thread. CPU-only, so it needs no GL context. Created
    // for every widget (all constructors share this helper).
    d->builderPool = new rendering::BuilderPool(2, this);
    connect(d->builderPool, &rendering::BuilderPool::jobReady,
            this, &GLWidget::builderResultReady, Qt::QueuedConnection);

    // New PainterDevice
    d->pd = new GLPainterDevice(this);
    if (shareWidget) {
      // share painter with the supplied widget
      d->painter = static_cast<GLPainter *>(shareWidget->painter());
    } else {
      d->painter = new GLPainter();
    }
    d->painter->incrementShare();

    setAutoFillBackground( false );
    setSizePolicy( QSizePolicy::MinimumExpanding,QSizePolicy::MinimumExpanding );
    d->camera->setParent( this );
    m_glslEnabled = false;
    m_navigateTool = 0;
    // Single-shot timer that clears the interaction flag and requests a repaint
    // ~250 ms after the last mouse drag. Surfaces render wireframe while the
    // camera drags (legacy renderQuick parity); the flag selects the flush mode,
    // it no longer skips. The timer is the backstop for releases the widget
    // never sees. Parented to this widget.
    m_cameraInteracting = false;
    m_interactionTimer.setSingleShot( true );
    m_interactionTimer.setInterval( 250 );
    m_interactionTimer.setParent( this );
    connect( &m_interactionTimer, &QTimer::timeout, this, [this] {
      m_cameraInteracting = false;
      update();
    } );

#ifdef ENABLE_THREADED_GL
    qDebug() << "Threaded GL enabled.";
    d->thread = new GLThread( this, this );
    //doneCurrent();
    d->thread->start();
#endif
  }

  GLWidget *GLWidget::m_current = 0;

  GLWidget *GLWidget::current()
  {
    return m_current;
  }

  void GLWidget::setCurrent(GLWidget *current)
  {
    m_current = current;
  }

  void GLWidget::renderNow()
  {
    // paintGL issues GL calls, which need the widget's context current AND
    // the widget's own framebuffer bound: QOpenGLWidget::makeCurrent() only
    // makes the context current, and drawing into the default framebuffer
    // would silently produce an empty frame.
    makeCurrent();
    // A non-null but invalid context (offscreen platform cannot create a GL
    // context, or context creation failed) still dereferences inside
    // defaultFramebufferObject() -> isValid() and segfaults. Bail cleanly when
    // the context is not usable so paintGL/picking run against a real one.
    if (QOpenGLContext *ctx = context()) {
      if (!ctx->isValid())
        return;
      ctx->functions()->glBindFramebuffer(GL_FRAMEBUFFER,
                                          defaultFramebufferObject());
    }
    paintGL();
  }

  void GLWidget::initializeGL()
  {
    // Guard (fix-windows-first-run-crash / gpu-rendering-pipeline): never issue a
    // GL call without a current context. resizeEvent/paintEvent call initializeGL
    // from the GUI thread; if no context is current (e.g. a widget created for a
    // not-yet-exposed view on Windows) bail out cleanly. This does not touch
    // d->initialized: the event paths guard on QOpenGLWidget::isValid() before
    // calling initializeGL, so d->initialized is only advanced once a real,
    // current-able context exists. Until then it stays false and the next exposed
    // frame runs full initialization (the modern tier's lazy first-frame
    // recovery also anticipates a delayed init). Defense in depth behind the
    // event-path checks.
    if (!QOpenGLContext::currentContext())
      return;

    qDebug() << "GLWidget initialisation...";
    if(!context()->isValid())
    {
      // this should never happen, as we checked for availability of features that we requested in
      // the default OpenGL format. However it happened to a user who had a very broken setting with
      // a proprietary nvidia driver.
      const QString error_msg = tr("Invalid OpenGL context.\n"
                                   "Either something is completely broken in your OpenGL setup "
                                   "(can you run any OpenGL application?), "
                                   "or you found a bug.");
      qDebug() << error_msg;
      QMessageBox::critical(0, tr("OpenGL error"), error_msg);
      abort();
    }

    // Report the renderer the live context actually resolved (not the requested
    // format), so rendering-behaviour issues can be attributed to the right
    // device. initializeGL runs exactly once per widget, so no once-flag needed.
    qDebug() << "OpenGL renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
             << "vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR))
             << "version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION))
             << "requested samples:" << context()->format().samples();

    // D10 / task 4.4: log the resolved GL version + profile (Compatibility vs.
    // Core) so the fixed-function fallback condition is visible in the field.
    const QSurfaceFormat fmt = context()->format();
    const char *profile = (fmt.profile() == QSurfaceFormat::CompatibilityProfile)
        ? "Compatibility" : (fmt.profile() == QSurfaceFormat::CoreProfile)
        ? "Core" : "NoProfile";
    qInfo() << "OpenGL resolved format:"
            << "version" << fmt.majorVersion() << fmt.minorVersion()
            << "profile" << profile;

    // Try to initialise GLEW if GLSL was enabled, test for OpenGL 2.0 support
    #ifdef ENABLE_GLSL
    GLenum err = glewInit();
    if (err != GLEW_OK) {
      qDebug() << "GLSL support enabled but GLEW could not initialise!";
      m_glslEnabled = false;
    }
    if (GLEW_VERSION_2_0) {
      qDebug() << "GLSL support enabled, OpenGL 2.0 support confirmed.";
      m_glslEnabled = true;
    }
    else if (GLEW_ARB_vertex_shader && GLEW_ARB_fragment_shader) {
      qDebug() << "GLSL support enabled, no OpenGL 2.0 support.";
      m_glslEnabled = true;
    }
    else {
      qDebug() << "GLSL support disabled, OpenGL 2.0 support not present.";
      m_glslEnabled = false;
    }
    #endif

    glClearColor(d->background.redF(), d->background.greenF(),
                 d->background.blueF(), d->background.alphaF());

    glShadeModel( GL_SMOOTH );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glEnable( GL_CULL_FACE );

    // Used to display semi-transparent selection rectangle
    //  glBlendFunc(GL_ONE, GL_ONE);
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    if (fmt.profile() == QSurfaceFormat::CoreProfile) {
      qInfo().noquote() << "Core-profile OpenGL context: fixed-function lighting "
                           "init skipped (GL enums invalid on Core). Degraded "
                           "fixed-function consumers are skipped here (not corrupt): "
                           "axes navigation glyph, text overlays, active-tool "
                           "overlay, legacy mesh flush. The modern shader tier "
                           "runs with the Camera-authored projection and "
                           "modelview matrices, decoupled from the (dead) GL "
                           "matrix stack.";
    } else {
      // Fixed-function-only state (review finding: glShadeModel and
      // GL_NORMALIZE are removed in GL 3.3 Core just like the lighting enums,
      // so they belong inside the non-Core branch). The fixed-function
      // lighting state follows: the legacy GLPainter (and the compat tier)
      // enable lighting themselves but rely on these ambient/diffuse/specular/
      // position values set here; removing them would darken the legacy render
      // (GL's default ambient is black) and break modern-vs-legacy visual
      // parity, so this block is retained (D2 revert-and-keep clause). It is
      // valid on a GL 3.3 CompatibilityProfile context.
      glShadeModel( GL_SMOOTH );
      glEnable( GL_NORMALIZE );
      glEnable( GL_LIGHTING );

      glLightfv( GL_LIGHT0, GL_AMBIENT, LIGHT_AMBIENT );
      glLightfv( GL_LIGHT0, GL_DIFFUSE, LIGHT0_DIFFUSE );
      glLightfv( GL_LIGHT0, GL_SPECULAR, LIGHT0_SPECULAR );
      glLightfv( GL_LIGHT0, GL_POSITION, LIGHT0_POSITION );
      glEnable( GL_LIGHT0 );

      // Create a second light source to illuminate those shadows a little better
      glLightfv( GL_LIGHT1, GL_AMBIENT, LIGHT_AMBIENT );
      glLightfv( GL_LIGHT1, GL_DIFFUSE, LIGHT1_DIFFUSE );
      glLightfv( GL_LIGHT1, GL_SPECULAR, LIGHT1_SPECULAR );
      glLightfv( GL_LIGHT1, GL_POSITION, LIGHT1_POSITION );
      glEnable( GL_LIGHT1 );
    }

    // Modern-tier backend (gpu-rendering-pipeline task 2.4). The instanced
    // geometry VAOs, shaders and instance VBOs are GPU objects bound to this
    // context, so they are created here on the current context. initializeGL
    // runs again when the GL context is rebuilt (e.g. after context loss), so
    // the existing backend is destroyed first and rebuilt: the GPU buffers are
    // recreated rather than leaked or left bound to a dead context.
    // Capability probing itself lives in rendering/renderertier.cpp (task 7.1).
    delete d->backend;
    d->backend = nullptr;
    delete d->compatBackend;
    d->compatBackend = nullptr;
    // Context loss (A3): the GPU buffer objects died with the old context.
    // The CPU-side stores survive, but the new backend starts with empty
    // VBOs, so the scene must be rebuilt/resubmitted after the backend is
    // recreated (design D3: the worker path resubmits; the synchronous path
    // re-emits). Without this the rebuilt backend would push acknowledged
    // (non-dirty) stores, upload nothing, and draw an empty scene.
    d->storesValid = false;
    d->enginesDirty = true;
    d->emittedRevision = 0;
    d->hasWorkerResult = false;
    d->submittedRevision = 0;
    d->readyRevision = 0;
    d->presentedRevision = 0;
    // Renderer-tier selection (gpu-rendering-pipeline design A8/D7, task 7.1).
    // The AVOGADRO_RENDERER override is read before the first frame is drawn;
    // without it the highest compiled-in tier the context supports runs. A
    // forced tier that is compiled out or unsupported fails at startup with a
    // clear diagnostic instead of silently degrading (design D7).
    bool tierForced = false;
    const rendering::RendererTier requestedTier =
      rendering::requestedRendererTier(tierForced);
    const rendering::RendererTier resolvedTier =
      rendering::resolveRendererTier(tierForced, requestedTier);
    if (resolvedTier == rendering::RendererTier::Unavailable) {
      // Design D7: a tier that is compiled out or unsupported but explicitly
      // requested fails startup with a clear diagnostic instead of silently
      // degrading (which would hide driver problems and make bug reports
      // unreproducible). qFatal rather than a modal box: the misconfiguration
      // is on the command line/environment, and the failure must be visible
      // (and testable) in headless runs too.
      if (tierForced) {
        qFatal("The requested renderer tier '%s' (AVOGADRO_RENDERER) cannot be "
               "used: it is not compiled into this build or this OpenGL context "
               "does not meet its requirements. Remove AVOGADRO_RENDERER to let "
               "Avogadro choose a supported tier.",
               qPrintable(rendering::tierName(requestedTier)));
      } else {
        qFatal("No rendering tier can run on this OpenGL context: the modern "
               "tier needs GL 3.3, and the fixed-function compat/legacy tiers "
               "cannot run on a Core-profile context. Update the OpenGL "
               "driver to one that offers a compatibility profile (or GL 3.3 "
               "or newer).");
      }
    }

    d->rendererTier = resolvedTier;
    d->useModernTier = (resolvedTier == rendering::RendererTier::Modern);

    if (resolvedTier == rendering::RendererTier::Compat) {
      // Compat-tier submit stage (task 7.2): the context is current here, so
      // the unit geometry client arrays can be captured now.
      d->compatBackend = new rendering::CompatRendererBackend();
      d->compatBackend->initialize();
      if (!d->compatBackend->isInitialized()) {
        delete d->compatBackend;
        d->compatBackend = nullptr;
        qFatal("The compat renderer tier could not initialise its unit "
               "geometry on this context.");
      }
    }

    if (d->useModernTier) {
      d->backend = new rendering::ModernRendererBackend();
      d->backend->initialize();
      if (d->backend->isInitialized())
        d->backend->setDetailLevel(d->pendingDetailLevel);
      if (!d->backend->isInitialized()) {
        // Context is 3.3+ but the backend could not build its shaders/VAOs;
        // drop it and stay on the fixed-function tier rather than show a
        // black screen. This is an initialisation failure, not a capability
        // probe result, so it is not a forced-tier violation.
        delete d->backend;
        d->backend = nullptr;
        d->useModernTier = false;
        // Accurate fallback log (fix-windows-first-run-crash): we are NOT using
        // the modern tier here — the instanced backend could not build, so the
        // frame presents through the fixed-function path until a later frame can
        // initialize the modern backend (see renderModern()'s lazy recovery).
        // Report the presentation path actually in use, not the requested tier;
        // the final "renderer tier =" log below already prints (fixed-function)
        // on this path via d->useModernTier (gpu-rendering-pipeline delta).
        qDebug() << "GLWidget: modern backend failed to initialise; presenting"
                 << "through the fixed-function path until the modern backend "
                 << "initializes on a later frame.";
      }
    }

    qDebug() << "GLWidget initialised... renderer tier ="
             << rendering::tierName(d->rendererTier)
             << (d->useModernTier ? "(instanced)" : "(fixed-function)");
  }

  void GLWidget::paintGL()
  {
    resizeGL(width(), height()); // fix for bug #1797069. don't remove!
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    // Author the camera projection CPU-side on every profile so the modern
    // tier's uMVP (and project()/unProject()) see a valid matrix even on a
    // Core profile context where the fixed-function matrix stack does not
    // exist (fix-modern-tier-camera-decoupling).
    d->camera->applyProjection();

    // Core-profile containment (fix-modern-tier-core-profile 3.1): the
    // fixed-function matrix stack does not exist on a Core context, so the
    // per-frame stack upload is skipped there instead of raising per-frame
    // GL_INVALID_ENUM (the modern backend's draw-path error isolation drains
    // whatever stale state remains). The fixed-function upload is a pure
    // consumer of the Camera-authored matrices above.
    if (context()->format().profile() != QSurfaceFormat::CoreProfile) {
      // setup the OpenGL projection matrix from the camera's authored matrix
      glMatrixMode( GL_PROJECTION );
      glLoadIdentity();
      glMultMatrixd( d->camera->projection().data() );

      // setup the OpenGL modelview matrix using the camera
      glMatrixMode( GL_MODELVIEW );
      glLoadIdentity();
      d->camera->applyModelview();
    }

    render();
  }

  void GLWidget::paintGL2()
  {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    // setup the OpenGL projection matrix from the camera's authored matrix
    // (applyProjection() now only authors it CPU-side, so upload it explicitly
    // — same as the GL_SELECT pick path).
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();
    d->camera->applyProjection();
    glMultMatrixd( d->camera->projection().data() );

    // setup the OpenGL modelview matrix using the camera
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadIdentity();
    d->camera->applyModelview();

    glEnable( GL_CULL_FACE );
    glEnable( GL_LIGHTING );
    glShadeModel( GL_SMOOTH );
    glEnable( GL_DEPTH_TEST );

    render();

    // Restore the OpenGL modelview matrix for the GLGraphicsView painter
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix();
    glMatrixMode( GL_PROJECTION );
    glPopMatrix();

    glDisable( GL_CULL_FACE );
    glDisable( GL_LIGHTING );
  }

  void GLWidget::paintEvent( QPaintEvent * )
  {
    if(updatesEnabled())
    {
#ifdef ENABLE_THREADED_GL
      // tell our thread to paint
      d->paintCondition.wakeAll();
#else
      // Guard (fix-windows-first-run-crash / gpu-rendering-pipeline): skip the
      // paint when the widget has no valid, current-able context. Qt re-issues
      // expose events, so nothing drawable is lost; issuing initializeGL /
      // paintGL with no context current is the UB that crashed the Windows first
      // paint. QOpenGLWidget::makeCurrent() returns void on every platform, so
      // the probe is QOpenGLWidget::isValid(): false while the widget's context
      // is not yet realized (e.g. a view created while hidden), true once it is.
      // This is the same cross-platform signal the unconditional makeCurrent()
      // relied on before, with the context-less case now detected explicitly.
      if (!isValid())
        return;
      makeCurrent();
      if(!d->initialized) {
        d->initialized = true;
        initializeGL();
      }
      // Always clear with opaque alpha to ensure solid background on Wayland
      glClearColor(d->background.redF(), d->background.greenF(),
                   d->background.blueF(), 1.0f);
      paintGL();
#endif
    }
  }

  void GLWidget::resizeEvent( QResizeEvent *event )
  {
    // Allow base class to recreate the framebuffer object as needed
    QOpenGLWidget::resizeEvent(event);
#ifdef ENABLE_THREADED_GL
    d->thread->resize( event->size().width(), event->size().height() );
#else
    // Guard (fix-windows-first-run-crash / gpu-rendering-pipeline): only run GL
    // work when the widget has a valid, current-able context. On Windows the
    // resize event can fire before the widget's context is realized (e.g. a view
    // created while hidden); issuing initializeGL / resizeGL with no context
    // current is UB that poisons driver state and crashes on first paint.
    // QOpenGLWidget::makeCurrent() returns void on every platform, so the probe
    // is QOpenGLWidget::isValid() — the same signal the unconditional
    // makeCurrent() relied on, now checking the context-less case explicitly.
    // GL work is skipped when the context is not valid, but resized() is still
    // emitted below so listeners keep seeing the resize (task 1.1). The
    // ENABLE_THREADED_GL path is compiled out by default and untouched.
    if (isValid()) {
      makeCurrent();
      if(!d->initialized)
      {
        d->initialized = true;
        initializeGL();
      }
      // GLXWaitX() is called by the TT resizeEvent on Linux... We may need
      // specific functions here - need to look at Mac and Windows code.
      resizeGL(event->size().width(), event->size().height());
    }
#endif
    emit resized();
  }

  void GLWidget::resizeGL( int width, int height )
  {
  // Use devicePixelRatioF for HiDPI support
  qreal dpr = devicePixelRatioF();
  glViewport(0, 0, static_cast<GLint>(width * dpr), static_cast<GLint>(height * dpr));
  }

  QRect GLWidget::deviceViewportRect() const
  {
    // HiDPI device-pixel framebuffer geometry (fix-hidpi-viewport-coordinates,
    // design D1). Matches resizeGL's glViewport so the modern/compat frame
    // submit, the pick pass and the camera projection all speak device px.
    qreal dpr = devicePixelRatioF();
    return QRect(0, 0, qRound(width() * dpr), qRound(height() * dpr));
  }

  void GLWidget::setBackground( const QColor &background )
  {
#ifdef ENABLE_THREADED_GL
    d->renderMutex.lock();
#endif
    d->background = background;
    d->background.setAlphaF(1.0);
#ifdef ENABLE_THREADED_GL
    d->renderMutex.unlock();
#endif
  }

  QColor GLWidget::background() const
  {
    return d->background;
  }

  void GLWidget::setColorMap(Color *colorMap)
  {
    d->colorMap = colorMap;
  }

  Color *GLWidget::colorMap() const
  {
    if (d->colorMap) {
      return d->colorMap;
    }
    else {
      if (!d->defaultColorMap) {
        PluginManager *plugins = PluginManager::instance();
        PluginFactory *factory = plugins->factory("ElementColor",
                                                Plugin::ColorType);
        if (factory) {
          d->defaultColorMap =
            static_cast<Color *>(factory->createInstance());
        } else {
          QList<PluginFactory *> factories =
            plugins->factories(Plugin::ColorType);
          if (!factories.isEmpty())
            d->defaultColorMap = static_cast<Color *>(
              factories.first()->createInstance());
        }
      }
      return d->defaultColorMap;
    }
  }

  void GLWidget::setQuality(int quality)
  {
    // Invalidate the display lists and change the painter quality level
    invalidateDLs();
    d->painter->setQuality(quality);

    // Modern tier: map the slider onto the existing LodLevel presets exactly as
    // SettingsDialog::qualityChanged labels them (0-1 Low, 2 Medium, 3-4 High)
    // and rebuild the unit sphere/cylinder index buffers only when the level
    // changed. Instances are LOD-independent, so worker results stay valid
    // (no re-keying). d->backend is null before the first GL context (e.g. when
    // readSettings() runs at startup), so guard against it.
    rendering::LodLevel level;
    if (quality <= 1)
      level = rendering::LodLow;
    else if (quality >= 3)
      level = rendering::LodHigh;
    else
      level = rendering::LodMedium;
    // Always stash the mapped level so a persisted slider value set by
    // readSettings() (before the backend exists) reaches the backend once it
    // is created; apply immediately when the backend is already live.
    d->pendingDetailLevel = level;
    if (d->backend)
      d->backend->setDetailLevel(level);
  }

  int GLWidget::quality() const
  {
    return d->painter->quality();
  }

  void GLWidget::setFogLevel(int level)
  {
    d->fogLevel = level;
  }

  int GLWidget::fogLevel() const
  {
    return d->fogLevel;
  }

  void GLWidget::setRenderAxes(bool renderAxes)
  {
    d->renderAxes = renderAxes;
    update();
  }

  bool GLWidget::renderAxes()
  {
    return d->renderAxes;
  }

  void GLWidget::setRenderDebug(bool renderDebug)
  {
    d->renderDebug = renderDebug;
    update();
  }

  bool GLWidget::renderDebug()
  {
    return d->renderDebug;
  }

  void GLWidget::setRenderModelViewDebug(bool renderModelViewDebug)
  {
    d->renderModelViewDebug = renderModelViewDebug;
    update();
  }

  bool GLWidget::renderModelViewDebug() const
  {
    return d->renderModelViewDebug;
  }

  void GLWidget::renderModern()
  {
    if (!d->molecule) {
      qDebug() << "GLWidget::renderModern(): No molecule set.";
      return;
    }
    // Fog (fix-modern-tier-consumer-parity task 4.2): mirror the legacy
    // fixed-function GL_LINEAR fog math so the modern tier depth-cues to the
    // background identically. setFog() stores the values; drawInstances()
    // applies them as a per-frame uniform. `d->backend` may be null on the
    // read-lock fallback path, so guard inside.
    auto applyFog = [&]() {
      if (!d->backend)
        return;
      if (d->fogLevel == 0) {
        d->backend->setFog(false, QVector3D(0.0f, 0.0f, 0.0f), 1.0f, 0.0f);
        return;
      }
      Vector3d distance =
        (camera()->modelview() * d->center.homogeneous()).head<3>();
      double distanceToCenter = distance.norm();
      float start = static_cast<float>(distanceToCenter
          - (d->fogLevel / 8.0) * d->radius);
      float end = static_cast<float>(distanceToCenter
          + ((10 - d->fogLevel) / 8.0 * 2.0) * d->radius);
      QVector3D color(d->background.redF(), d->background.greenF(),
                      d->background.blueF());
      d->backend->setFog(true, color, start, end);
    };
    if (!d->molecule->lock()->tryLockForRead()) {
      // Read lock unavailable (design D3 / fix-modern-tier-frame-presentation):
      // present the retained frame instead of a cleared one. beginFrame() does
      // not zero the draw counts, so endFrame() re-presents the last pushed
      // geometry with the current camera transform. We bare-return after this;
      // emittedRevision / storesValid / enginesDirty are left untouched because
      // no result was adopted on this path. The backend may not be initialised
      // yet on this path (initialisation happens below), so only present when
      // there is a live backend to draw with.
      if (d->backend && d->backend->beginFrame(camera(), deviceViewportRect(),
                                               d->builder.quality())) {
        d->backend->setModelViewProjection(currentModelViewProjection());
        d->backend->setModelView(currentModelView());
        applyFog();
        d->backend->endFrame();
        // Shared overlay pass (fix-modern-tier-consumer-parity): present the
        // tool HUD, cell box, and meshes too, so the retained frame is not
        // geometry-only while the read lock is contended.
        paintOverlaysAfterFrame();
      }
      return;
    }
    // HiDPI fix (task 2.1 / design D3): release the molecule read lock on
    // every exit path via RAII. renderModern has several early returns (no
    // backend, worker still building); hand-placed unlocks are the exact
    // pattern that leaked in the modern paths and deadlocked writers.
    struct ReadLockGuard {
      QReadWriteLock *lock;
      ~ReadLockGuard() { lock->unlock(); }
    } readLockGuard{d->molecule->lock()};
    if (!d->backend) {
      // Backend not initialised on this context yet. initializeGL can run
      // before the widget's context is current (observed on first show under
      // GLX: the resize-driven paint precedes a usable context), so recover
      // lazily on the first frame that does have one instead of staying
      // fixed-function forever. A context-loss rebuild lands here too — the
      // fresh backend has empty VBOs, so the scene is rebuilt/resubmitted.
      if (d->rendererTier == rendering::RendererTier::Modern
          && QOpenGLContext::currentContext()) {
        d->backend = new rendering::ModernRendererBackend();
        d->backend->initialize();
        if (!d->backend->isInitialized()) {
          delete d->backend;
          d->backend = nullptr;
        } else {
          d->useModernTier = true; // recovered: the instanced path is live
          // Apply the quality level read before the backend existed.
          d->backend->setDetailLevel(d->pendingDetailLevel);
          d->storesValid = false;
          d->enginesDirty = true;
          d->emittedRevision = 0;
          d->hasWorkerResult = false;
          d->submittedRevision = 0;
          d->readyRevision = 0;
          d->presentedRevision = 0;
          qDebug() << "GLWidget: modern backend recovered; resuming the instanced"
                   << " (modern tier) presentation path.";
        }
      }
    }
    if (!d->backend) {
      // Still no backend (no current context): nothing to draw this frame.
      return;
    }

    const unsigned int revision = d->molecule->revision();
    const bool sceneChanged =
      d->enginesDirty || !d->storesValid || revision != d->emittedRevision;

    // Worker path (gpu-rendering-pipeline task 6.1, design A7/D3): while the
    // scene is covered by the snapshot tessellator, rebuilds are dispatched
    // to the builder pool and every frame presents the last ready state —
    // a rebuild in flight never stalls input.
    const bool workerPath = workerPathCapable();
    if (workerPath && sceneChanged && revision != d->submittedRevision)
      submitWorkerBuild(revision);

    if (workerPath) {
      // Forward-progress present (design D1 / fix-modern-tier-revision-churn):
      // adopt whenever a result is at least as new as the last presented
      // generation. builderResultReady already advanced presentedRevision to
      // readyRevision on adoption, so this holds on every frame until a newer
      // result arrives; it never regresses to older content and never blanks
      // while a build is in flight (drawLastReady handles that gap).
      if (d->hasWorkerResult && d->readyRevision >= d->presentedRevision) {
        // Adopt the worker-built chunks: pushing uploads only the dirty
        // ranges, under the backend's ≤2 ms/frame budget (design D2/D3).
        const QMatrix4x4 mvp = currentModelViewProjection();
        if (d->backend->beginFrame(camera(), deviceViewportRect(),
                                   d->builder.quality())) {
          d->backend->setModelViewProjection(mvp);
          d->backend->setModelView(currentModelView());
          applyFog();
          d->backend->pushSpheres(d->workerResult.spheres);
          d->backend->pushBonds(d->workerResult.bonds);
          d->backend->pushLines(d->workerResult.lines);
          d->backend->endFrame();
          d->backendDrawCalls = d->backend->drawCallsThisFrame();
          d->backendFrames = d->backend->framesSubmitted();
        }
        d->emittedRevision = revision;
        d->storesValid = true;
        d->enginesDirty = false;
        // Shared overlay pass (fix-modern-tier-consumer-parity, task 1.1):
        // bracketed tool overlay + captured text, exactly as the synchronous
        // emission path below does. Runs under an active painter bracket, so
        // tool overlays (AutoOpt energy/HUD halos, bond-centric labels) that
        // previously vanished with no active bracket are restored. The
        // store persists across adopt frames (only cleared on the next emit),
        // so labels stay visible and re-project with the rotating camera.
        paintOverlaysAfterFrame();
        return;
      }
      if (sceneChanged) {
        // Build still in flight: present the last ready chunks untouched.
        drawLastReady();
        return;
      }
    }

    // Re-emit only when the scene actually changed (design A7): rotation
    // frames reuse the previous frame's chunk stores — pushing stores with
    // no dirty ranges uploads nothing, so the frame is pure instanced draws.
    if (sceneChanged) {
      emitSceneToBuilder();
      d->emittedRevision = revision;
      d->storesValid = true;
      d->enginesDirty = false;
    }

    // Upload the finished stores and issue the bounded instanced draws.
    if (d->backend->beginFrame(camera(), deviceViewportRect(),
                               d->builder.quality())) {
      d->backend->setModelViewProjection(currentModelViewProjection());
      d->backend->setModelView(currentModelView());
      applyFog();
      d->backend->pushSpheres(d->builder.spheres());
      d->backend->pushBonds(d->builder.bonds());
      d->backend->pushLines(d->builder.lines());
      d->backend->endFrame();
      d->backendDrawCalls = d->backend->drawCallsThisFrame();
      d->backendFrames = d->backend->framesSubmitted();
    }

    // Shared overlay pass (fix-modern-tier-consumer-parity, task 1.1): runs
    // after the instanced frame under a bracketed painter, restoring tool
    // overlays that previously vanished with no active bracket. Emits the
    // captured engine text (labels, measurements) through the real painter,
    // matching the legacy tier.
    paintOverlaysAfterFrame();
  }

  QMatrix4x4 GLWidget::currentModelViewProjection()
  {
    // The Camera is the single authority for both matrices (fix-modern-tier-
    // camera-decoupling): compose the standard projection * modelview product
    // from the stored matrices, with no GL matrix-stack read, so it is valid
    // even on a Core profile context where the fixed-function stack is dead.
    // The backend's vertex shader evaluates uMVP * vec4(worldPos, 1.0).
    const Eigen::Matrix4f proj = d->camera->projection().matrix().cast<float>();
    const Eigen::Matrix4f mv = d->camera->modelview().matrix().cast<float>();
    return composeModelViewProjection(QMatrix4x4(proj.data()), QMatrix4x4(mv.data()));
  }

  QMatrix4x4 GLWidget::currentModelView()
  {
    // The model-view matrix the Camera stores (fix-modern-tier-camera-
    // decoupling). The backend's lit vertex shaders evaluate
    // mat3(uModelView) * normal, so this must be the camera's modelview. Same
    // .transposed() convention as composeModelViewProjection: Qt's raw-array
    // constructor reads a column-major array as row-major.
    const Eigen::Matrix4f mv = d->camera->modelview().matrix().cast<float>();
    return QMatrix4x4(mv.data()).transposed();
  }

  QMatrix4x4 GLWidget::composeModelViewProjection(const QMatrix4x4 &projMat,
                                                  const QMatrix4x4 &mvMat)
  {
    // QMatrix4x4's raw-array constructor reads GL's column-major arrays as
    // row-major, so a QMatrix4x4 built from a GL matrix holds it transposed.
    // operator* is the standard written-order product, so transposing each GL
    // matrix back and multiplying in written order yields projection * modelview.
    return projMat.transposed() * mvMat.transposed();
  }

  //! Engines (plus the active tool) emit the scene into the chunk stores.
  //! Shared by the modern tier (instanced shaders) and the compat tier
  //! (fixed-function flush), so both tiers render byte-identical instance
  //! sets and any engine migration lands on both at once (design D4).
  QMatrix4x4 GLWidget::emitSceneToBuilder()
  {
    const QMatrix4x4 mvp = currentModelViewProjection();

    // Paint the engines into the SceneBuilder (a Painter) instead of issuing
    // GL. The pd override routes every pd->painter()->draw* call to the
    // SceneBuilder, which appends per-instance records to the chunk stores
    // the backend uploads. Engines are already sorted by engineLessThan, so
    // the opaque-then-transparent pass order matches the legacy path.
    d->pd->setOverridePainter(&d->builder);
    d->builder.begin();
    d->builder.clearStores();

    // Opaque engine elements first.
    foreach (Engine *engine, d->engines) {
      if (engine->isEnabled())
        engine->renderOpaque(d->pd);
    }

    // Transparent engine elements.
    glEnable(GL_BLEND);
    foreach (Engine *engine, d->engines) {
      if (engine->isEnabled() && engine->layers() & Engine::Transparent) {
        engine->renderTransparent(d->pd);
      }
    }
    glDisable(GL_BLEND);

    // Emission is engines-only: the active tool's overlay paint now runs
    // through the shared overlay pass (paintOverlaysAfterFrame) AFTER the
    // backend frame, under a bracketed painter. Removing it here means the
    // tool is no longer painted with no active painter bracket on the sync
    // path, and the instanced molecule geometry is no longer rasterized
    // under the tool on that path (fix-modern-tier-consumer-parity task 1.1;
    // supersedes fix-modern-tier-overlay-parity task 4.2).
    d->builder.end();
    d->pd->clearOverridePainter();
    return mvp;
  }

  bool GLWidget::workerPathCapable() const
  {
    // The tessellator reproduces the BSDY ball-and-stick output; any other
    // enabled engine keeps the synchronous path (design D4 mixed pipeline).
    if (!d->builderPool)
      return false;
    foreach (Engine *engine, d->engines) {
      if (engine->isEnabled() && !engine->isSnapshotCovered())
        return false;
    }
    return true;
  }

  void GLWidget::submitWorkerBuild(unsigned int revision)
  {
    // Take the snapshot under the molecule read lock (design A7): render()
    // holds lockForRead for the duration of this call, and the snapshot must
    // never reference the live vectors (Atom::pos() points into them).
    rendering::SceneSnapshot snapshot;
    snapshot.revision = revision;
    const Molecule *mol = d->molecule;
    if (!mol)
      return;

    const unsigned int n = mol->numAtoms();
    snapshot.atoms.resize(n);
    QHash<unsigned long, unsigned int> idToIndex;
    idToIndex.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
      const Atom *a = mol->atom(i);
      if (!a)
        continue;
      rendering::SnapshotAtom &sa = snapshot.atoms[i];
      const Eigen::Vector3d &pos = *a->pos();
      sa.pos[0] = static_cast<float>(pos[0]);
      sa.pos[1] = static_cast<float>(pos[1]);
      sa.pos[2] = static_cast<float>(pos[2]);
      sa.atomicNumber = static_cast<unsigned char>(a->atomicNumber());
      // Atom index == its position in the snapshot (taken in molecule order).
      sa.index = i;
      idToIndex.insert(a->id(), i);
    }
    for (unsigned int i = 0; i < mol->numBonds(); ++i) {
      const Bond *b = mol->bond(i);
      if (!b)
        continue;
      rendering::SnapshotBond sb;
      sb.begin = idToIndex.value(b->beginAtomId(), 0);
      sb.end = idToIndex.value(b->endAtomId(), 0);
      sb.order = b->order();
      // Bond index == its position in the snapshot (taken in molecule order).
      sb.index = i;
      snapshot.bonds.append(sb);
    }

    // Selection ids come from the widget's selection state (task 4.3).
    QSet<unsigned int> selectedAtoms;
    QSet<unsigned int> selectedBonds;
    for (const Primitive *p : selectedPrimitives()) {
      if (!p)
        continue;
      if (p->type() == Primitive::AtomType)
        selectedAtoms.insert(p->id());
      else if (p->type() == Primitive::BondType)
        selectedBonds.insert(p->id());
    }

    rendering::JobParams params;
    foreach (Engine *engine, d->engines) {
      if (engine->isEnabled() && engine->isSnapshotCovered()) {
        params = engine->captureJobParams(colorMap(), selectedAtoms,
                                          selectedBonds);
        break;
      }
    }
    // The worker indexes captured atoms by the snapshot's atom count. When no
    // engine is snapshot-covered (params stays default, empty atoms) the worker
    // would dereference out of bounds in emitOpaqueAtoms. Default-construct the
    // captured atoms so params.atoms always matches snapshot.atoms.
    params.atoms.resize(snapshot.atoms.size());

    d->submittedRevision = revision;
    d->builderPool->submit(snapshot, params, revision);
  }

  void GLWidget::builderResultReady(
    unsigned long jobId, const rendering::TessellationResult &result,
    unsigned int generation)
  {
    Q_UNUSED(jobId);
    // Forward-progress adoption (design D1 / fix-modern-tier-revision-churn).
    // Adopt only results strictly newer than what has already been presented;
    // discard everything older-or-equal. Under sustained molecule-revision
    // churn the old gate (generation == molecule->revision(), or discard if
    // generation < revision) starved: every build lost the race, was
    // discarded and resubmitted, and the display froze at the last adopted
    // revision. Adopting forward bounds presentation latency to one build no
    // matter how fast the revision climbs, and monotonicity guarantees a
    // stale result can never overwrite newer presented content. The decision
    // is isolated in decideResultAdoption() so it can be unit tested.
    if (!d->molecule)
      return;
    bool resubmit = false;
    if (rendering::decideResultAdoption(generation, d->presentedRevision,
                                        d->molecule->revision(),
                                        d->submittedRevision, resubmit)
        == rendering::ResultAdoption::Adopt) {
      // Adopt: this result is strictly newer than anything presented so far.
      d->workerResult = result;
      d->readyRevision = generation;
      d->presentedRevision = generation;
      d->hasWorkerResult = true;
      // If the live molecule has moved past this build, submit the newest
      // revision immediately so the next adopted result makes further progress
      // (design D1). renderModern() also does this, but doing it here closes
      // the window without waiting for the next frame.
      if (resubmit)
        submitWorkerBuild(d->molecule->revision());
      update(); // adopt the freshly built chunks on the next frame
    } else {
      // Superseded by a newer presented result (or an exact duplicate).
      // Drop it, but still ask for the newest live revision so the display
      // can catch up once churn stops (design D1).
      if (resubmit)
        submitWorkerBuild(d->molecule->revision());
    }
  }

  void GLWidget::sceneContentsChanged()
  {
    d->enginesDirty = true;
    update();
  }

  void GLWidget::drawLastReady()
  {
    // Present the last ready state (design D3 / fix-modern-tier-frame-presentation):
    // re-present the geometry the backend already holds in its GPU-resident VBOs.
    // beginFrame() no longer zeroes the draw counts, so an endFrame() with no
    // push re-presents the previously pushed geometry instead of a cleared
    // frame. The MVP is set from the live camera so the retained scene keeps
    // tracking rotation while a rebuild is in flight; without it the picture
    // would freeze at the last pose. No molecule lock is needed — this is GL
    // state, not molecule data, and the same reads happen unconditionally in
    // the adopt/sync paths. Nothing is dirty after the adopting frame, so this
    // is pure instanced draws — no emission, no uploads, no input stall.
    if (d->backend->beginFrame(camera(), deviceViewportRect(),
                               d->builder.quality())) {
      d->backend->setModelViewProjection(currentModelViewProjection());
      d->backend->setModelView(currentModelView());
      d->backend->endFrame();
      d->backendDrawCalls = d->backend->drawCallsThisFrame();
      d->backendFrames = d->backend->framesSubmitted();
      // Shared overlay pass (fix-modern-tier-consumer-parity): the retained
      // frame would otherwise present geometry only, transiently dropping the
      // tool HUD, cell box, and meshes during a startup gap. Re-paint them.
      paintOverlaysAfterFrame();
    }

    // Flush any text captured by the last engine emission (labels, etc.).
    // On the "build still in flight" path no emission ran this frame, so the
    // store still holds the last captured items (persisted until the next
    // clearStores in emitSceneToBuilder); flushing them keeps labels visible
    // and re-projected with the rotating camera across retained frames.
    flushCapturedText();
  }

  void GLWidget::flushCapturedText()
  {
    // Capture-then-flush (fix-modern-tier-overlay-parity, design decision 1):
    // the modern/compat engine pass records text primitives in the SceneBuilder
    // instead of discarding them (SceneBuilder::drawText). Render them here
    // through the widget's real painter, exactly as the legacy branch draws
    // text: screen-space items at their coordinates, world-space items
    // projected with the live camera (the GL matrices paintGL() left on the
    // stack are still current, matching GLPainter::drawText(Vector3d)).
    const QVector<rendering::TextItem> &items = d->builder.textItems();
    if (items.isEmpty())
      return;

    d->painter->begin(this);
    for (const rendering::TextItem &item : items) {
      // GLPainter::drawText reads the GL current color (it ignores the stored
      // Painter color), so set the current color per item here, exactly as every
      // colored text producer does before it draws (fix-modern-tier-overlay-parity
      // P1: without this, flushed labels render in a single stale color and the
      // parity goal "same color as legacy" is missed for non-default colors).
      glColor4f(static_cast<GLfloat>(item.color.redF()),
                static_cast<GLfloat>(item.color.greenF()),
                static_cast<GLfloat>(item.color.blueF()),
                static_cast<GLfloat>(item.color.alphaF()));
      if (item.worldSpace)
        d->painter->drawText(item.worldPos, item.text);
      else
        d->painter->drawText(item.x, item.y, item.text);
    }
    d->painter->end();
  }

  // Shared overlay pass for the modern and compat tiers (fix-modern-tier-
  // consumer-parity, design D1). Runs after the backend's frame, under a
  // single GLPainter begin/end bracket, in this order: unit-cell pass
  // (task 3.2, gated below), mesh flush (task 2.1), the active tool's overlay
  // paint, and finally the captured engine text (flushCapturedText).
  //
  // This is the single call site every modern/compat path uses to draw tool
  // overlays. Previously the tool paint ran with no active painter bracket
  // (GLPainter::end() zeroes d->widget at the end of the previous frame's
  // overlay block), so every widget->painter()->draw* call hit isValid() ==
  // false and silently returned — dropping AutoOpt's energy/HUD halos,
  // bond-centric labels, and everything else tools paint. begin/end nesting
  // is safe because GLPainter counts overflow (glpainter_p.cpp:1516-1530).
  void GLWidget::paintOverlaysAfterFrame()
  {
    d->painter->begin(this);

    // Unit-cell pass (task 3.2): draw cell edges + axes before the mesh flush
    // so blended orbital/ESP surfaces composite over the opaque cell edges,
    // matching the legacy layering (cell edges are opaque lines). Gated on a
    // real unit cell so molecular files are unaffected. renderCrystalAxes()
    // is painter-based (drawBoxEdges) and shared with the legacy tier, so the
    // modern tier renders identical cell edges/axes. The legacy glCallList
    // repetition loop (renderCrystal) never repeats (aCells/bCells/cCells are
    // fixed at 1), so this single call loses nothing.
    // Gate on a real unit cell AND the renderUnitCellAxes preference, matching
    // the legacy tier (renderCrystalAxes:1850), so a user who disabled the cell
    // axes does not get cell edges only on the modern tier.
    if (d->molecule != nullptr && d->molecule->OBUnitCell() != nullptr
        && d->renderUnitCellAxes)
      renderCrystalAxes();

    // Mesh flush (task 2.1): draw every captured mesh (orbital/ESP surfaces)
    // through the real painter after the instanced frame, so meshes that the
    // instanced backend never touched become visible. Placed after the cell
    // pass so translucent surfaces composite over the cell edges.
    flushCapturedMeshes();

    // Tool overlay paint (task 1.1): composites above molecule geometry and
    // the captured meshes. AutoOptTool toggles GL_BLEND around its own draws,
    // so running after the instanced frame is safe.
    if (d->tool)
      d->tool->paint(this);

    // Captured engine text (labels, measurements) flushes through the real
    // painter here too. The store persists across adopt frames (only cleared
    // on the next emit), so labels stay visible and re-project with the
    // rotating camera on retained frames.
    flushCapturedText();

    d->painter->end();
  }

  // Flush every captured mesh (orbital / ESP surfaces) through the real
  // painter. Called from paintOverlaysAfterFrame() after the instanced frame:
  // the instanced backend never touches meshes, so without this they were
  // computed and captured by SceneBuilder but silently dropped on the modern
  // tier. Reuses GLPainter's tested VBO streaming (task 5.1) so a stable mesh
  // uploads once and draws with a cached VBO; an unstable mesh is skipped
  // until it stabilizes. Blending is enabled for the whole flush (meshes are
  // translucent and drawn over the opaque molecule, matching the legacy
  // transparent pass).
  void GLWidget::flushCapturedMeshes()
  {
    const rendering::ChunkStore<rendering::MeshInstance> &meshes =
      d->builder.meshes();
    if (meshes.isEmpty())
      return;

    // Meshes are translucent (alpha ~0.75) and drawn over the opaque molecule,
    // so blending is enabled for the whole flush (matching the legacy
    // transparent pass) and restored afterwards. glPolygonMode / GL_LIGHTING
    // are restored by GLPainter::drawMesh/drawColorMesh on exit.
    glEnable(GL_BLEND);
    for (unsigned int i = 0; i < meshes.size(); ++i) {
      const rendering::MeshInstance &inst = meshes.at(i);
      // Skip a null or still-being-built mesh: drawing an unstable mesh is not
      // safe (SurfaceEngine's pattern), and GLPainter::ensureMeshVBO gates on
      // stability too. It will be drawn once it stabilizes.
      if (inst.mesh == nullptr || !inst.mesh->stable())
        continue;
      // The mesh is owned by a worker/snapshot result; take a read lock for
      // the duration of the draw so the worker cannot free/rebuild it
      // mid-draw (SurfaceEngine's pattern, surfaceengine.cpp:82-100).
      if (!inst.mesh->lock()->tryLockForRead())
        continue;
      // D10 / task 4.1: backface culling. Closed isosurfaces halve fill cost
      // by culling the far faces; enable it around each draw and restore it
      // afterwards so other passes are unaffected. glCullFace(GL_BACK) is the
      // default face, set explicitly for clarity.
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      // While the camera is being dragged, render the mesh as a wireframe net
      // (mode 1 = GL_LINE, legacy renderQuick parity) instead of skipping it;
      // the solid surface returns once the interaction flag clears.
      const int flushMode = m_cameraInteracting ? 1 : inst.mode;
      // Per-vertex color (ESP surface) vs. the instance's solid color.
      if (inst.colored)
        d->painter->drawColorMesh(*inst.mesh, flushMode);
      else {
        d->painter->setColor(inst.color[0] / 255.0f,
                             inst.color[1] / 255.0f,
                             inst.color[2] / 255.0f,
                             inst.color[3] / 255.0f);
        d->painter->drawMesh(*inst.mesh, flushMode);
      }
      glDisable(GL_CULL_FACE);
      inst.mesh->lock()->unlock();
    }
    glDisable(GL_BLEND);
  }

  //! Compat tier (task 7.2 / design D9): the same chunk stores the modern
  //! tier consumes, flushed through the fixed-function pipeline. Draw calls
  //! scale with the instance count, which the monomer-scale compat target
  //! accepts; picking stays on the GL_SELECT path via hits().
  void GLWidget::renderCompat()
  {
    if (!d->molecule) {
      qDebug() << "GLWidget::renderCompat(): No molecule set.";
      return;
    }
    if (!d->molecule->lock()->tryLockForRead()) {
      qDebug() << "GLWidget::renderCompat(): Could not get read lock on molecule.";
      return;
    }
    // HiDPI fix (task 2.1 / design D3): release the molecule read lock on
    // every exit path via RAII (see renderModern for the rationale).
    struct ReadLockGuard {
      QReadWriteLock *lock;
      ~ReadLockGuard() { lock->unlock(); }
    } readLockGuard{d->molecule->lock()};
    if (!d->compatBackend || !d->compatBackend->isInitialized()) {
      // No compat stage on this context (before initializeGL or after a
      // context-loss rebuild that has not run yet); nothing to draw.
      return;
    }

    const QMatrix4x4 mvp = emitSceneToBuilder();

    if (d->compatBackend->beginFrame(camera(), deviceViewportRect(),
                                     d->builder.quality())) {
      d->compatBackend->setModelViewProjection(mvp); // no-op: FF reads the stack
      d->compatBackend->pushSpheres(d->builder.spheres());
      d->compatBackend->pushBonds(d->builder.bonds());
      d->compatBackend->pushLines(d->builder.lines());
      d->compatBackend->endFrame();
      d->backendDrawCalls = d->compatBackend->drawCallsThisFrame();
      d->backendFrames = d->compatBackend->framesSubmitted();
      // Shared overlay pass (fix-modern-tier-consumer-parity, task 1.1): runs
      // after the backend frame under a bracketed painter on the compat tier
      // too, restoring tool overlays (AutoOpt energy/HUD halos, bond-centric
      // labels) that previously vanished with no active bracket. Emits the
      // captured engine text (labels, measurements) through the real painter,
      // matching the legacy tier.
      paintOverlaysAfterFrame();
    }
  }

  void GLWidget::render()
  {
    if (!d->molecule) {
      qDebug() << "GLWidget::render(): No molecule set.";
      return;
    }
    if (!d->molecule->lock()->tryLockForRead()) {
      qDebug() << "GLWidget::render(): Could not get read lock on molecule.";
      return;
    }
    // Release this read lock on EVERY exit path (RAII). The tier dispatch and
    // the post-dispatch overlay block below (axes + debug/text overlays, drawn
    // for every tier) read the molecule under this lock, so it must outlive
    // them. The modern/compat tier functions take their own nested read lock;
    // they do not unlock this one. Since ENABLE_THREADED_GL is OFF by default
    // rendering and the tools share the GUI thread, a leaked read lock blocks
    // every writer: DrawTool/Manipulate tryLockForWrite() fails (cannot
    // draw/move), updateGeometry() tryLockForRead() fails (camera refocus
    // after fragment insert), and writers hang once Qt's recursive read-lock
    // cap saturates (~25 locks).
    struct ReadLockGuard {
      QReadWriteLock *lock;
      ~ReadLockGuard() { lock->unlock(); }
    } readLockGuard{d->molecule->lock()};

    // Modern instanced tier (gpu-rendering-pipeline task 2.4): the engines
    // emit into the SceneBuilder and the backend issues bounded instanced
    // draws. The compat tier (task 7.2) emits the same stores and flushes
    // them through fixed function. The frozen legacy tier keeps its own
    // per-primitive display-list/GL-painter path below.
    // Dispatch on the resolved tier, not the backend state: the modern
    // backend can recover lazily inside renderModern() when the first
    // initializeGL ran without a current context (design D3 recovery).
    if (d->rendererTier == rendering::RendererTier::Modern) {
      renderModern();
    }
    else if (d->rendererTier == rendering::RendererTier::Compat) {
      renderCompat();
    }
    else {
      d->painter->begin(this);

    if (d->painter->quality() >= 3) {
      glEnable(GL_LIGHT1);
    }
    else {
      glDisable(GL_LIGHT1);
    }
    bool hasUnitCell = (d->molecule->OBUnitCell() != NULL);

    if (d->fogLevel) {
      glFogi(GL_FOG_MODE, GL_LINEAR);
      GLfloat fogColor[4]= {static_cast<GLfloat>(d->background.redF()), static_cast<GLfloat>(d->background.greenF()),
                            static_cast<GLfloat>(d->background.blueF()), static_cast<GLfloat>(d->background.alphaF())};
      glFogfv(GL_FOG_COLOR, fogColor);
      Vector3d distance = (camera()->modelview() * d->center.homogeneous()).head<3>();
      double distanceToCenter = distance.norm();
      glFogf(GL_FOG_DENSITY, 1.0);
      glHint(GL_FOG_HINT, GL_NICEST);
      glFogf(GL_FOG_START, distanceToCenter - (d->fogLevel / 8.0) * d->radius);
      glFogf(GL_FOG_END, distanceToCenter + ((10-d->fogLevel)/8.0 * 2.0) * d->radius);
      glEnable(GL_FOG);
    }
    else {
      glDisable(GL_FOG);
    }

    // Use renderQuick if the view is being moved, otherwise full render
    if (d->quickRender) {
      d->updateListQuick();
      glCallList(d->dlistQuick);
      if (hasUnitCell) {
        renderCrystal(d->dlistQuick);
      }
      // Render the active tool
      if ( d->tool ) {
        d->tool->paint( this );
      }
    }
    else {
      // we save a display list if we're doing a crystal
      if (d->dlistOpaque == 0)
        d->dlistOpaque = glGenLists(1);
      if (d->dlistTransparent == 0)
        d->dlistTransparent = glGenLists(1);

      // Opaque engine elements rendered first
      if (hasUnitCell) glNewList(d->dlistOpaque, GL_COMPILE);
      foreach(Engine *engine, d->engines)
        if(engine->isEnabled()) {
#ifdef ENABLE_GLSL
          if (m_glslEnabled) glUseProgramObjectARB(engine->shader());
#endif
          engine->renderOpaque(d->pd);
        }
#ifdef ENABLE_GLSL
          if (m_glslEnabled) glUseProgramObjectARB(0);
#endif
      if (hasUnitCell) { // end the main list and render the opaque crystal
        glEndList();
        renderCrystal(d->dlistOpaque);
      }

      // Render the active tool
      if ( d->tool ) {
        d->tool->paint( this );
      }

#ifdef ENABLE_PYTHON
      // Render the extensions (for now: python only)
      qRegisterMetaType<GLWidget*>("GLWidget*");
      foreach (Extension *extension, d->extensions) {
        PythonExtension *pyext = qobject_cast<PythonExtension*>(extension);
        if (pyext) {
          // If threaded GL is on, we are in the rendering thread. pyext
          // needs to acquire GIL, but it's already acquired by the GLWidget
          // constructor. That's why we use an indirect queued call to
          // PythonExtension::paint() here
          QMetaObject::invokeMethod(pyext, "paint", Qt::QueuedConnection,
                                    Q_ARG(GLWidget*, this));
        }
      }
#endif

      // Now render transparent
      glEnable(GL_BLEND);
      if (hasUnitCell)
        glNewList(d->dlistTransparent, GL_COMPILE);
      foreach(Engine *engine, d->engines) {
        if(engine->isEnabled() && engine->layers() & Engine::Transparent) {
#ifdef ENABLE_GLSL
          if (m_glslEnabled) glUseProgramObjectARB(engine->shader());
#endif
          engine->renderTransparent(d->pd);
        }
      }
      glDisable(GL_BLEND);
#ifdef ENABLE_GLSL
          if (m_glslEnabled) glUseProgramObjectARB(0);
#endif
      if (hasUnitCell) { // end the main list and render the transparent bits
        glEndList();
        renderCrystal(d->dlistTransparent);
      }
    }
    // Render all the inactive tools
    if ( d->toolGroup ) {
      QList<Tool *> tools = d->toolGroup->tools();
      foreach( Tool *tool, tools ) {
        if ( tool != d->tool ) {
          tool->paint( this );
        }
      }
    }

      d->painter->end();
    }

    // Render overlays (axes + debug/text) after the tier-specific 3D pass for
    // ALL tiers. These were legacy-only below the dispatch, so the modern and
    // compat tiers — which need them most — never executed them. The calls are
    // moved (not copied) and wrapped in the same begin/end coordination the
    // legacy branch used, so the legacy tier does not double-draw. On the
    // modern/compat tiers the engine text (e.g. LabelEngine labels) is flushed
    // separately from the SceneBuilder capture store by renderModern()
    // /renderCompat(); this block draws the axes overlay and the debug/extension
    // text overlay.
    d->painter->begin(this);
    // If enabled draw the axes
    if (d->renderAxes) renderAxesOverlay();

    // Render text overlay
    renderTextOverlay();
    d->painter->end();

    // The read lock acquired at the top of render() is released by the RAII
    // ReadLockGuard above; do not unlock here (would double-unlock).
  }

  void GLWidget::renderCrystal(GLuint displayList)
  {
    std::vector<vector3> cellVectors = d->molecule->OBUnitCell()->GetCellVectors();

    for (int a = 0; a < d->aCells; a++) {
      for (int b = 0; b < d->bCells; b++)  {
        for (int c = 0; c < d->cCells; c++)  {
          glPushMatrix();
          glTranslated(
                       cellVectors[0].x() * a
                       + cellVectors[1].x() * b
                       + cellVectors[2].x() * c,
                       cellVectors[0].y() * a
                       + cellVectors[1].y() * b
                       + cellVectors[2].y() * c,
                       cellVectors[0].z() * a
                       + cellVectors[1].z() * b
                       + cellVectors[2].z() * c );

          glCallList(displayList);
          glPopMatrix();
        }
      }
    } // end of for loops

    if (d->renderUnitCellAxes)
      renderCrystalAxes();
  }

  void GLWidget::renderCrystalAxes()
  {
    const matrix3x3 obmat (d->molecule->OBUnitCell()->GetCellMatrix());

    const Vector3d v1 (obmat(0,0), obmat(0,1), obmat(0,2));
    const Vector3d v2 (obmat(1,0), obmat(1,1), obmat(1,2));
    const Vector3d v3 (obmat(2,0), obmat(2,1), obmat(2,2));
    Vector3d offset;

    d->painter->setColor(&d->cellColor);

    if (d->onlyRenderOriginalUnitCell) {
      offset << 0.0, 0.0, 0.0;
      renderClippedBox(offset, v1, v2, v3, 2.0);
    }
    else
      for (int a = 0; a < d->aCells; a++) {
        for (int b = 0; b < d->bCells; b++)  {
          for (int c = 0; c < d->cCells; c++)  {
            // Calculate offset for this cell
            offset = (a * v1 + b * v2 + c * v3);

            // Draw the clipped box with a linewidth of 2.0
            renderClippedBox(offset, v1, v2, v3, 2.0);
          }
        }
      }
    }
  }

// Use anonymous namespace for renderClippedBox helper functions
namespace {
  // Given two points a and b, and a plane defined by normal vector n
  // and point p, does the plane intersect the line segment formed by
  // ab? If so, intersection is overwritten with the point of
  // intersection and the function returns true.
  static inline bool isPlaneBetweenPoints(const Vector3d &a,
                                          const Vector3d &b,
                                          const Vector3d &n,
                                          const Vector3d &p,
                                          Vector3d *intersection)
  {
    // Resource:
    // http://paulbourke.net/geometry/planeline/

    const double denom = n.dot(b - a);

    // Parallel (in or out of plane)
    if (fabs(denom) < 1e-8)
      return false;

    // Otherwise it intersects the line ab. To find out if it
    // intersects the line segment ab, more calcs are needed:
    const double u = ( n.dot(p - a) ) / ( denom );

    // if u is between (0,1], it intersects segment ab. Otherwise
    // return false.
    if (u <= 0 || u > 1)
      return false;

    // If we don't need to calculate the intersection, return true here
    if (!intersection)
      return true;

    // Calculate intersection, return true
    *intersection = a + u * (b - a);
    return true;
  }

  // Adapted from Malcolm McLean's example at
  // http://bytes.com/topic/c/answers/621985-print-binary-representation
  char* debug16bit(const quint16 x) {
    static char buff[sizeof(quint16) * CHAR_BIT + 1];
    quint16 i;
    quint16 j = sizeof(quint16) * CHAR_BIT - 1;

    buff[j] = 0;
    for(i=0;i<sizeof(quint16) * CHAR_BIT; i++)
      {
        if(x & (1 << i))
          buff[j] = '1';
        else
          buff[j] = '0';
        j--;
      }
    return buff;
  }
}

namespace Avogadro {
  bool GLWidget::renderClippedBox(const Eigen::Vector3d &origin,
                                  const Eigen::Vector3d &v1,
                                  const Eigen::Vector3d &v2,
                                  const Eigen::Vector3d &v3,
                                  double lineWidth)
  {
    // Create points from vectors:
    //
    //         6------8  c1 = origin
    //        /:     /|  c2 = origin + v1
    //       / :    / |  c3 = origin + v2
    //      /  4---/--7  c4 = origin + v3
    //     /  /   /  /   c5 = origin + v1 + v2
    //    3------5  /    c6 = origin + v2 + v3
    //    | /    | /     c7 = origin + v1 + v3
    //    |/     |/      c8 = origin + v1 + v2 + v3
    //    1------2
    //
    // Edges are defined as:
    // edge = pts, hexID   edge = pts, hexID
    //  e1  = 1 2  0x1      e7  = 3 6  0x12
    //  e2  = 1 3  0x2      e8  = 4 6  0x14
    //  e3  = 1 4  0x4      e9  = 4 7  0x18
    //  e4  = 2 5  0x8      e10 = 5 8  0x20
    //  e5  = 2 7  0x10     e11 = 6 8  0x21
    //  e6  = 3 5  0x11     e12 = 7 8  0x22

    // Create all points
    const Eigen::Vector3d &c1 = origin;
    const Eigen::Vector3d  c2 = origin + v1;
    const Eigen::Vector3d  c3 = origin + v2;
    const Eigen::Vector3d  c4 = origin + v3;
    const Eigen::Vector3d  c5 = c2 + v2;
    const Eigen::Vector3d  c6 = c3 + v3;
    const Eigen::Vector3d  c7 = c2 + v3;
    const Eigen::Vector3d  c8 = c5 + v3;

    d->painter->drawBoxEdges(c1, c2, c3, c4, c5, c6, c7, c8, lineWidth);

    // Now for the clipping part. We will find all intersections of
    // the viewing volume's near-plane cell edges, and draw an
    // appropriate polygon that highlights where the clipping
    // occurs. This prevents odd "missing corners" that are visually
    // disturbing.

    // Grab a point in the near clipping plane and it's normal. If
    // there is no clipping plane, return.
    Vector3d clipNormal;
    Vector3d clipPoint;
    if (!d->camera->nearClippingPlane(&clipNormal, &clipPoint)) {
      return false;
    }

    // Nudge the point slightly inside of the viewing volume to ensure
    // that the clip outline isn't clipped itself
    clipPoint += 1e-4 * clipNormal;

    // Check which edges of the plane are intersected by the view
    // plane.
    //
    // The intersections are stored bitwise in a quint16, the
    // least significant bit representing e1 and the four most
    // significant bits being 0. A bit == 1 indicates that the edge is
    // intersected. Initialize to all 0s.
    //
    quint16 intersections = 0;
    //
    // Define masks for the edge bits to prevent errors:
    //
    enum {
      E1MASK =  0x001,
      E2MASK =  0x002,
      E3MASK =  0x004,
      E4MASK =  0x008,
      E5MASK =  0x010,
      E6MASK =  0x020,
      E7MASK =  0x040,
      E8MASK =  0x080,
      E9MASK =  0x100,
      E10MASK = 0x200,
      E11MASK = 0x400,
      E12MASK = 0x800
    };
    //
    // Points of intersections:
    //
    unsigned short intersectionCount = 0;
    Vector3d i1;
    Vector3d i2;
    Vector3d i3;
    Vector3d i4;
    Vector3d i5;
    Vector3d i6;
    Vector3d i7;
    Vector3d i8;
    Vector3d i9;
    Vector3d i10;
    Vector3d i11;
    Vector3d i12;
    //
    // Test points
    //
    if (isPlaneBetweenPoints(c1, c2, clipNormal, clipPoint, &i1)) {
      intersections |= E1MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c1, c3, clipNormal, clipPoint, &i2)) {
      intersections |= E2MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c1, c4, clipNormal, clipPoint, &i3)) {
      intersections |= E3MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c2, c5, clipNormal, clipPoint, &i4)) {
      intersections |= E4MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c2, c7, clipNormal, clipPoint, &i5)) {
      intersections |= E5MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c3, c5, clipNormal, clipPoint, &i6)) {
      intersections |= E6MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c3, c6, clipNormal, clipPoint, &i7)) {
      intersections |= E7MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c4, c6, clipNormal, clipPoint, &i8)) {
      intersections |= E8MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c4, c7, clipNormal, clipPoint, &i9)) {
      intersections |= E9MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c5, c8, clipNormal, clipPoint, &i10)) {
      intersections |= E10MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c6, c8, clipNormal, clipPoint, &i11)) {
      intersections |= E11MASK;
      ++intersectionCount;
    }
    if (isPlaneBetweenPoints(c7, c8, clipNormal, clipPoint, &i12)) {
      intersections |= E12MASK;
      ++intersectionCount;
    }

    // Set stipple pattern for clip lines. glPopAttrib() must be
    // called at all exit points that follow.
    glPushAttrib(GL_LINE_BIT);
    const GLushort clipStipple = 0xF0F0; // A = 1010
    const GLint clipStippleFactor = 1;
    glLineStipple(clipStippleFactor, clipStipple);
    glEnable(GL_LINE_STIPPLE);

    // Handle polygon drawing based on number of intersections
    switch (intersectionCount) {

    case 0:
      // No intersections, just return.
      glPopAttrib();
      return false;

    case 1:
      // Shouldn't happen, probably floating points errors. Print a
      // warning and return.
      qWarning() << "Viewing volume near-plane intersects unit cell only once.";
      glPopAttrib();
      return false;

    case 2:
      // Also shouldn't happen. Bail.
      qWarning() << "Viewing volume near-plane intersects unit cell only twice.";
      glPopAttrib();
      return false;

    case 3: {
      // Either one corner is in the frustum, or only one is
      // out. Either way, just connect the points in the triangle.
      const Vector3d * triangle[3];
      unsigned short triangleInd = 0;

      if (intersections & E1MASK) {
        triangle[triangleInd++] = &i1;
      }
      if (intersections & E2MASK) {
        triangle[triangleInd++] = &i2;
      }
      if (intersections & E3MASK) {
        triangle[triangleInd++] = &i3;
      }
      if (intersections & E4MASK) {
        triangle[triangleInd++] = &i4;
      }
      if (intersections & E5MASK) {
        triangle[triangleInd++] = &i5;
      }
      if (intersections & E6MASK) {
        triangle[triangleInd++] = &i6;
      }
      if (intersections & E7MASK) {
        triangle[triangleInd++] = &i7;
      }
      if (intersections & E8MASK) {
        triangle[triangleInd++] = &i8;
      }
      if (intersections & E9MASK) {
        triangle[triangleInd++] = &i9;
      }
      if (intersections & E10MASK) {
        triangle[triangleInd++] = &i10;
      }
      if (intersections & E11MASK) {
        triangle[triangleInd++] = &i11;
      }
      if (intersections & E12MASK) {
        triangle[triangleInd++] = &i12;
      }

      // If there were more/less than three intersections, something is
      // buggy above
      Q_ASSERT(triangleInd == 3);

      d->painter->drawLine(*(triangle[0]), *(triangle[1]), lineWidth);
      d->painter->drawLine(*(triangle[1]), *(triangle[2]), lineWidth);
      d->painter->drawLine(*(triangle[2]), *(triangle[0]), lineWidth);

      glPopAttrib();
      return true;
    }

    case 4:
      // Case of either two, four, or six corners outside the near
      // plane.
      //
      // 2 corner cuts follow, enumerated in order of isolated
      // coherent edge
      //
      switch (intersections) {
      case ( E2MASK | E4MASK | E5MASK | E3MASK ):
        // e1 excluded; e2, e4, e5, e3 intersected.
        d->painter->drawQuadrilateral(i2, i4, i5, i3, lineWidth);
        glPopAttrib();
        return true;

      case ( E1MASK | E6MASK | E7MASK | E3MASK ):
        // e2 excluded; e1, e6, e7, e3 intersected.
        d->painter->drawQuadrilateral(i1, i6, i7, i3, lineWidth);
        glPopAttrib();
        return true;

      case ( E2MASK | E8MASK | E9MASK | E1MASK ):
        // e3 excluded; e2, e8, e9, e1 intersected.
        d->painter->drawQuadrilateral(i2, i8, i9, i1, lineWidth);
        glPopAttrib();
        return true;

      case ( E1MASK | E6MASK | E10MASK | E5MASK ):
        // e4 excluded; e1, e6, e10, e5 intersected.
        d->painter->drawQuadrilateral(i1, i6, i10, i5, lineWidth);
        glPopAttrib();
        return true;

      case ( E1MASK | E4MASK | E12MASK | E9MASK ):
        // e5 excluded; e1, e4, e12, e9 intersected.
        d->painter->drawQuadrilateral(i1, i4, i12, i9, lineWidth);
        glPopAttrib();
        return true;

      case ( E2MASK | E4MASK | E10MASK | E7MASK ):
        // e6 excluded; e2, e4, e10, e7 intersected.
        d->painter->drawQuadrilateral(i2, i4, i10, i7, lineWidth);
        glPopAttrib();
        return true;

      case ( E2MASK | E6MASK | E11MASK | E8MASK ):
        // e7 excluded; e2, e6, e11, e8 intersected.
        d->painter->drawQuadrilateral(i2, i6, i11, i8, lineWidth);
        glPopAttrib();
        return true;

      case ( E3MASK | E7MASK | E11MASK | E9MASK ):
        // e8 excluded; e3, e7, e11, e9 intersected.
        d->painter->drawQuadrilateral(i3, i7, i11, i9, lineWidth);
        glPopAttrib();
        return true;

      case ( E3MASK | E5MASK | E12MASK | E8MASK ):
        // e9 excluded; e3, e5, e12, e8 intersected.
        d->painter->drawQuadrilateral(i3, i5, i12, i8, lineWidth);
        glPopAttrib();
        return true;

      case ( E4MASK | E12MASK | E11MASK | E6MASK ):
        // e10 excluded; e4, e12, e11, e6 intersected.
        d->painter->drawQuadrilateral(i4, i12, i11, i6, lineWidth);
        glPopAttrib();
        return true;

      case ( E7MASK | E10MASK | E12MASK | E8MASK ):
        // e11 excluded; e7, e10, e12, e8 intersected.
        d->painter->drawQuadrilateral(i7, i10, i12, i8, lineWidth);
        glPopAttrib();
        return true;

      case ( E5MASK | E10MASK | E11MASK | E9MASK ):
        // e12 excluded; e5, e10, e11, e9 intersected.
        d->painter->drawQuadrilateral(i5, i10, i11, i9, lineWidth);
        glPopAttrib();
        return true;

      // Cases of the four coplanar corners outside of the near-plane

      case ( E3MASK | E7MASK | E10MASK | E5MASK ):
        // "parallel" to v1, v2; e3, e7, e10, e5 intersected.
        d->painter->drawQuadrilateral(i3, i7, i10, i5, lineWidth);
        glPopAttrib();
        return true;

      case ( E2MASK | E4MASK | E12MASK | E8MASK ):
        // "parallel" to v1, v3; e2, e4, e12, e8 intersected.
        d->painter->drawQuadrilateral(i2, i4, i12, i8, lineWidth);
        glPopAttrib();
        return true;

      case ( E1MASK | E6MASK | E11MASK | E9MASK ):
        // "parallel" to v2,v3; e1, e6, e11, e9 intersected.
        d->painter->drawQuadrilateral(i1, i6, i11, i9, lineWidth);
        glPopAttrib();
        return true;

      default:
        // Shouldn't be any others:
        qWarning() << "Unhandled 4-point near-plane unit cell intersection:"
                   << debug16bit(intersections);
        glPopAttrib();
        return false;

      } // End switch on 4 intersections

    case 5:
      // Three or five corner split by near-plane, enumerated by the
      // three-corner combinations below:
      switch (intersections) {

      case ( E4MASK | E6MASK | E7MASK | E3MASK | E5MASK ): {
        // Corners: c1, c2, c3; e4, e6, e7, e3, e5 intersected.
        QList<Eigen::Vector3d> points;
        points << i4 << i6 << i7 << i3 << i5;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E2MASK | E4MASK | E5MASK | E9MASK | E8MASK ): {
        // Corners: c1, c2, c4; e2, e4, e5, e9, e8 intersected.
        QList<Eigen::Vector3d> points;
        points << i2 << i4 << i5 << i9 << i8;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E2MASK | E6MASK | E10MASK | E5MASK | E3MASK ): {
        // Corners: c1, c2, c5; e2, e6, e10, e5, e3 intersected.
        QList<Eigen::Vector3d> points;
        points << i2 << i6 << i10 << i5 << i3;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E2MASK | E4MASK | E12MASK | E9MASK | E3MASK ): {
        // Corners: c1, c2, c7; e2, e4, e12, e9, e3 intersected.
        QList<Eigen::Vector3d> points;
        points << i2 << i4 << i12 << i9 << i3;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E6MASK | E1MASK | E9MASK | E8MASK | E7MASK ): {
        // Corners: c1, c3, c4; e6, e1, e9, e8, e7 intersected.
        QList<Eigen::Vector3d> points;
        points << i6 << i1 << i9 << i8 << i7;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E3MASK | E7MASK | E10MASK | E4MASK | E1MASK ): {
        // Corners: c1, c3, c5; e3, e7, e10, e4, e1 intersected.
        QList<Eigen::Vector3d> points;
        points << i3 << i7 << i10 << i4 << i1;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E1MASK | E6MASK | E11MASK | E8MASK | E3MASK ): {
        // Corners: c1, c3, c6; e1, e6, e11, e8, e3 intersected.
        QList<Eigen::Vector3d> points;
        points << i1 << i6 << i11 << i8 << i3;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E1MASK | E9MASK | E11MASK | E7MASK | E2MASK ): {
        // Corners: c1, c4, c6; e1, e9, e11, e7, e2 intersected.
        QList<Eigen::Vector3d> points;
        points << i1 << i9 << i11 << i7 << i2;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E2MASK | E8MASK | E12MASK | E5MASK | E1MASK ): {
        // Corners: c1, c4, c7; e2, e8, e12, e5, e1 intersected.
        QList<Eigen::Vector3d> points;
        points << i2 << i8 << i12 << i5 << i1;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E5MASK | E10MASK | E7MASK | E2MASK | E1MASK ): {
        // Corners: c2, c3, c5; e5, e10, e7, e2, e1 intersected.
        QList<Eigen::Vector3d> points;
        points << i5 << i10 << i7 << i2 << i1;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E1MASK | E3MASK | E8MASK | E12MASK | E4MASK ): {
        // Corners: c2, c4, c7; e1, e3, e8, e12, e4 intersected.
        QList<Eigen::Vector3d> points;
        points << i1 << i3 << i8 << i12 << i4;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E1MASK | E6MASK | E10MASK | E12MASK | E9MASK ): {
        // Corners: c2, c5, c7; e1, e6, e10, e12, e9 intersected.
        QList<Eigen::Vector3d> points;
        points << i1 << i6 << i10 << i12 << i9;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E1MASK | E6MASK | E11MASK | E12MASK | E5MASK ): {
        // Corners: c2, c5, c8; e1, e6, e11, e12, e5 intersected.
        QList<Eigen::Vector3d> points;
        points << i1 << i6 << i11 << i12 << i5;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E1MASK | E4MASK | E10MASK | E11MASK | E9MASK ): {
        // Corners: c2, c7, c8; e1, e4, e10, e11, e9 intersected.
        QList<Eigen::Vector3d> points;
        points << i1 << i4 << i10 << i11 << i9;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E6MASK | E11MASK | E9MASK | E3MASK | E2MASK ): {
        // Corners: c3, c4, c6; e6, e11, e9, e3, e2 intersected.
        QList<Eigen::Vector3d> points;
        points << i6 << i11 << i9 << i3 << i2;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E4MASK | E2MASK | E8MASK | E11MASK | E10MASK ): {
        // Corners: c3, c5, c6; e4, e2, e8, e11, e10 intersected.
        QList<Eigen::Vector3d> points;
        points << i4 << i2 << i8 << i11 << i10;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E2MASK | E4MASK | E12MASK | E11MASK | E7MASK ): {
        // Corners: c3, c5, c8; e2, e4, e12, e11, e7 intersected.
        QList<Eigen::Vector3d> points;
        points << i2 << i4 << i12 << i11 << i7;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E2MASK | E6MASK | E10MASK | E12MASK | E8MASK ): {
        // Corners: c3, c6, c8; e2, e6, e10, e12, e8 intersected.
        QList<Eigen::Vector3d> points;
        points << i2 << i6 << i10 << i12 << i8;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E5MASK | E3MASK | E7MASK | E11MASK | E12MASK ): {
        // Corners: c4, c6, c7; e5, e3, e7, e11, e12 intersected.
        QList<Eigen::Vector3d> points;
        points << i5 << i3 << i7 << i11 << i12;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E3MASK | E7MASK | E10MASK | E12MASK | E9MASK ): {
        // Corners: c4, c6, c8; e3, e7, e10, e12, e9 intersected.
        QList<Eigen::Vector3d> points;
        points << i3 << i7 << i10 << i12 << i9;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E3MASK | E5MASK | E10MASK | E11MASK | E8MASK ): {
        // Corners: c4, c7, c8; e3, e5, e10, e11, e8 intersected.
        QList<Eigen::Vector3d> points;
        points << i3 << i5 << i10 << i11 << i8;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E4MASK | E6MASK | E7MASK | E8MASK | E12MASK ): {
        // Corners: c5, c6, c8; e4, e6, e7, e8, e12 intersected.
        QList<Eigen::Vector3d> points;
        points << i4 << i6 << i7 << i8 << i12;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E4MASK | E6MASK | E11MASK | E9MASK | E5MASK ): {
        // Corners: c5, c7, c8; e4, e6, e11, e9, e5 intersected.
        QList<Eigen::Vector3d> points;
        points << i4 << i6 << i11 << i9 << i5;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E7MASK | E8MASK | E9MASK | E5MASK | E10MASK ): {
        // Corners: c6, c7, c8; e7, e8, e9, e5, e10 intersected.
        QList<Eigen::Vector3d> points;
        points << i7 << i8 << i9 << i5 << i10;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      default:
        // Shouldn't be any others:
        qWarning() << "Unhandled 5-point near-plane unit cell intersection:"
                   << debug16bit(intersections);
        glPopAttrib();
        return false;
      }

    case 6:
      // Diagonal slices with four corners each in each volume.
      switch (intersections) {

      case ( E4MASK | E6MASK | E7MASK | E8MASK | E9MASK | E5MASK ): {
        // Corners: c1, c2, c3, c4; e4, e6, e7, e8, e9, e5 intersected.
        QList<Eigen::Vector3d> points;
        points << i4 << i6 << i7 << i8 << i9 << i5;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E2MASK | E3MASK | E9MASK | E12MASK | E10MASK | E6MASK ): {
        // Corners: c1, c2, c5, c7; e2, e3, e9, e12, e10, e6 intersected.
        QList<Eigen::Vector3d> points;
        points << i2 << i3 << i9 << i12 << i10 << i6;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E1MASK | E3MASK | E8MASK | E11MASK | E10MASK | E4MASK ): {
        // Corners: c1, c3, c5, c6; e1, e3, e8, e11, e10, e4 intersected.
        QList<Eigen::Vector3d> points;
        points << i1 << i3 << i8 << i11 << i10 << i4;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      case ( E2MASK | E7MASK | E11MASK | E12MASK | E5MASK | E1MASK ): {
        // Corners: c1, c4, c6, c7; e2, e7, e11, e12, e5, e1 intersected.
        QList<Eigen::Vector3d> points;
        points << i2 << i7 << i11 << i12 << i5 << i1;
        d->painter->drawLineLoop(points, lineWidth);
        glPopAttrib();
        return true;
      }

      default:
        // Shouldn't be any others:
        qWarning() << "Unhandled 6-point near-plane unit cell intersection:"
                   << debug16bit(intersections);
        glPopAttrib();
        return false;
      }

    default:
      // It doesn't make sense for more intersections to exist.
      qWarning() << "Unhandled" << intersectionCount << "point near-plane"
                 << "unit cell intersection:"
                 << debug16bit(intersections);

      glPopAttrib();
      return false;
    }

    // Shouldn't happen
    glPopAttrib();
    return false;
  }

  void GLWidget::renderAxesOverlay()
  {
    // Core-profile containment (fix-modern-tier-core-profile D3): the glyph
    // preamble uses the fixed-function matrix stack (glMatrixMode/glOrtho/
    // glLoadIdentity), which is invalid on a resolved Core context. Skip the
    // whole overlay on Core so it does not raise GL errors every frame; the
    // modern tier renders without this fixed-function overlay.
    if (context() &&
        context()->format().profile() == QSurfaceFormat::CoreProfile)
      return;

    // Render x, y, z axes as an overlay on the widget
    // Save the opengl projection matrix and set up an orthogonal projection
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    // Ensure the axes are of the same length using the logical widget size
    double aspectRatio = static_cast<double>(d->pd->width()) /
                         static_cast<double>(d->pd->height());
    glOrtho(0, aspectRatio, 0, 1, 0, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Set the origin and calculate the positions of the axes
    Vector3d origin = Vector3d(0.07, 0.07, -.07);
    Vector3d aXa = d->pd->camera()->transformedXAxis() * 0.04 + origin;
    Vector3d aX = d->pd->camera()->transformedXAxis() * 0.06 + origin;
    Vector3d aYa = d->pd->camera()->transformedYAxis() * 0.04 + origin;
    Vector3d aY = d->pd->camera()->transformedYAxis() * 0.06 + origin;
    Vector3d aZa = d->pd->camera()->transformedZAxis() * 0.04 + origin;
    Vector3d aZ = d->pd->camera()->transformedZAxis() * 0.06 + origin;

    // Turn off dynamic scaling in the painter (cylinders don't render correctly)
    d->painter->setDynamicScaling(false);

    // Own the lighting the glyph needs (fix-modern-tier-pick-and-axes, design
    // D7.1). The axes' colors are material-based (Color::applyAsMaterials ->
    // glMaterialfv per draw), and materials are ignored while GL_LIGHTING is
    // disabled. The modern tier's pick() (and some drawText/drawLine draws)
    // leave GL_LIGHTING disabled across frames, so unlit the glyph renders in
    // whatever leftover raster color glColor holds — default white, or yellow
    // after an earlier warm line/HUD draw. Establishing (and restoring) the
    // enable here makes the glyph immune to any past or future disabler: the
    // push/pop scopes the enable to the glyph's draws and restores the ambient
    // state byte-for-byte, so the text overlay that follows (which draws unlit)
    // is unaffected.
    glPushAttrib(GL_LIGHTING_BIT | GL_ENABLE_BIT);
    glEnable(GL_LIGHTING);
    glShadeModel(GL_SMOOTH);

    // Circle next to the axes
    painter()->setColor(1.0, 0.0, 0.0);
    painter()->drawSphere(&origin, 0.005);
    // x axis
    painter()->setColor(1.0, 0.0, 0.0);
    painter()->drawCylinder(origin, aXa, 0.005);
    painter()->drawCone(aXa, aX, 0.01);
    // y axis
    painter()->setColor(0.0, 1.0, 0.0);
    painter()->drawCylinder(origin, aYa, 0.005);
    painter()->drawCone(aYa, aY, 0.01);
    // y axis
    painter()->setColor(0.0, 0.0, 1.0);
    painter()->drawCylinder(origin, aZa, 0.005);
    painter()->drawCone(aZa, aZ, 0.01);

    // Restore the lighting enable state the glyph temporarily changed.
    glPopAttrib();

    // Turn dynamic scaling back on (default state)
    d->painter->setDynamicScaling(true);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
  }

  void GLWidget::renderDebugOverlay()
  {
    qDebug() << Q_FUNC_INFO << "is deprecated."
             << "Please use renderTextOverlay() instead.";
    return renderTextOverlay();
  }

  void GLWidget::renderTextOverlay()
  {
    // Draw all text in white
    d->pd->painter()->setColor(1.0, 1.0, 1.0);

    int x = 5, y = 5;

    if (d->renderDebug) {
      // Title
      y += d->pd->painter()->drawText
        (x, y, "---- " + tr("Debug Information") + " ----");

      // FPS
      y += d->pd->painter()->drawText
        (x, y, tr("FPS: %L1").arg(computeFramesPerSecond(), 0, 'g', 3));

      // Modern-tier backend counters (gpu-rendering-pipeline task 2.4): the
      // number of instanced draws issued for the most recent modern frame and
      // the total frames submitted, used to confirm the draw count stays
      // bounded (2 per frame, one per primitive type) regardless of scene size.
      if (d->useModernTier && d->backend) {
        y += d->pd->painter()->drawText
          (x, y, tr("Draw Calls: %L1 (frames %L2)")
           .arg(d->backendDrawCalls).arg(d->backendFrames));
      }

      // pd size
      y += d->pd->painter()->drawText
        (x, y, tr("View Size: %L1 x %L2")
         .arg(d->pd->width()).arg(d->pd->height()) );

      if (d->renderModelViewDebug) {
        // Model view matrix:
        const Eigen::Projective3d &modelview = d->camera->modelview();
        y += d->pd->painter()->drawText
            (x, y, tr("ModelView row 1: %L1 %L2 %L3 %L4")
             .arg(modelview(0, 0), 6, 'f', 2, ' ')
             .arg(modelview(0, 1), 6, 'f', 2, ' ')
             .arg(modelview(0, 2), 6, 'f', 2, ' ')
             .arg(modelview(0, 3), 6, 'f', 2, ' '));
        y += d->pd->painter()->drawText
            (x, y, tr("ModelView row 2: %L1 %L2 %L3 %L4")
             .arg(modelview(1, 0), 6, 'f', 2, ' ')
             .arg(modelview(1, 1), 6, 'f', 2, ' ')
             .arg(modelview(1, 2), 6, 'f', 2, ' ')
             .arg(modelview(1, 3), 6, 'f', 2, ' '));
        y += d->pd->painter()->drawText
            (x, y, tr("ModelView row 3: %L1 %L2 %L3 %L4")
             .arg(modelview(2, 0), 6, 'f', 2, ' ')
             .arg(modelview(2, 1), 6, 'f', 2, ' ')
             .arg(modelview(2, 2), 6, 'f', 2, ' ')
             .arg(modelview(2, 3), 6, 'f', 2, ' '));
        y += d->pd->painter()->drawText
            (x, y, tr("ModelView row 4: %L1 %L2 %L3 %L4")
             .arg(modelview(3, 0), 6, 'f', 2, ' ')
             .arg(modelview(3, 1), 6, 'f', 2, ' ')
             .arg(modelview(3, 2), 6, 'f', 2, ' ')
             .arg(modelview(3, 3), 6, 'f', 2, ' '));
      }

      // Molecule info
      if (!d->molecule) {
        y += d->pd->painter()->drawText(x, y, tr("No molecule set"));
      }
      else {
        // numAtoms
        y += d->pd->painter()->drawText
          (x, y, tr("Atoms: %L1").arg(d->molecule->numAtoms()));

        // numBonds
        y += d->pd->painter()->drawText
          (x, y, tr("Bonds: %L1").arg(d->molecule->numBonds()));
      }
    } // end debug

    // textOverlay stuff
    if (d->textOverlayLabels.size()) {
      // Lock mutex
      d->textOverlayMutex.lock();

      // For null pointers:
      QList<int> deadLabelIndices;

      // Draw text

      for (int i = 0; i < d->textOverlayLabels.size(); ++i) {
        QPointer<QLabel> &label = d->textOverlayLabels[i];

        // Check that QPointer is valid
        if (label == 0) {
          deadLabelIndices.append(i);
          continue;
        }

        // Draw text
        y += d->pd->painter()->drawText(x, y, label->text());
      }

      // Remove dead entries in reverse order
      for (int i = deadLabelIndices.size()-1; i >= 0; --i) {
        d->textOverlayLabels.removeAt(i);
      }

      // Release mutex
      d->textOverlayMutex.unlock();
    }
  }

  bool GLWidget::event( QEvent *event )
  {
    if(event->type() == QEvent::Show)
      {
        GLWidget::setCurrent(this);
      }

    return QOpenGLWidget::event(event);
  }

  // Scale a mouse event's coordinates by the device-pixel ratio and dispatch it
  // to the active tool, falling back to the navigate tool when the tool ignores
  // it, and returning any resulting command. This is the single HiDPI
  // coordinate + dispatch path shared by the real mouse handlers and the touch
  // synthesizer (add-touch-navigation task 1.3), so touch and mouse events can
  // never diverge in scaling or tool routing.
  //
  // Two details are load-bearing and preserved verbatim (oracle refactor
  // review): the fallback is nested inside `if (d->tool)`, so a widget with no
  // active tool routes the event to NOBODY (not even the navigate tool); and
  // scaledEvent.ignore() precedes dispatch, because a fresh QEvent defaults to
  // accepted and ignore() is what lets !isAccepted() detect a tool that
  // declines by leaving the event untouched (the one-finger-rotate path).
  QUndoCommand* GLWidget::dispatchMouse(QMouseEvent* event)
  {
    QMouseEvent scaledEvent(
      event->type(),
      event->position() * devicePixelRatioF(),
      event->scenePosition() * devicePixelRatioF(),
      event->globalPosition() * devicePixelRatioF(),
      event->button(), event->buttons(), event->modifiers()
    );
    scaledEvent.ignore();

    QUndoCommand* command = 0;

    if ( d->tool ) {
      switch ( event->type() ) {
        case QEvent::MouseButtonPress:
          command = d->tool->mousePressEvent( this, &scaledEvent );
          if (!scaledEvent.isAccepted() && m_navigateTool) {
            command = m_navigateTool->mousePressEvent(this, &scaledEvent);
          }
          break;
        case QEvent::MouseMove:
          command = d->tool->mouseMoveEvent( this, &scaledEvent );
          if (!scaledEvent.isAccepted() && m_navigateTool) {
            command = m_navigateTool->mouseMoveEvent(this, &scaledEvent);
          }
          break;
        case QEvent::MouseButtonRelease:
          command = d->tool->mouseReleaseEvent( this, &scaledEvent );
          if (!scaledEvent.isAccepted() && m_navigateTool) {
            command = m_navigateTool->mouseReleaseEvent(this, &scaledEvent);
          }
          break;
        default:
          break;
      }
    }
    return command;
  }

  void GLWidget::mousePressEvent( QMouseEvent * event )
  {
    // Set the event to ignored, check whether any tools accept it
    event->ignore();

    QUndoCommand *command = dispatchMouse(event);
    if ( command && d->undoStack ) {
      d->undoStack->push( command );
    } else if ( command ) {
      delete command;
    }
    emit activated(this);
    emit mousePress(event);
  }

  void GLWidget::mouseReleaseEvent( QMouseEvent * event )
  {
    // Set the event to ignored, check whether any tools accept it
    event->ignore();

    QUndoCommand *command = dispatchMouse(event);
    if ( command && d->undoStack ) {
      d->undoStack->push( command );
    }
#ifdef ENABLE_THREADED_GL
    d->renderMutex.lock();
#endif
    // Stop using quickRender
    d->quickRender = false;
#ifdef ENABLE_THREADED_GL
    d->renderMutex.unlock();
#endif
    // Solid surface returns on the first post-release frame; the 250 ms timer
    // remains the backstop for missed releases.
    m_cameraInteracting = false;
    // Render the scene at full quality now the mouse button has been released
    update();
    emit mouseRelease(event);
  }

  void GLWidget::mouseMoveEvent( QMouseEvent * event )
  {
    // Set the event to ignored, check whether any tools accept it
    event->ignore();

    // A held button means the user is dragging the camera (rotate/pan). Flag the
    // interaction and (re)arm the 250 ms timer: surfaces render wireframe while
    // dragging (legacy renderQuick parity); the flag selects the flush mode, it
    // no longer skips.
    if ( event->buttons() != Qt::NoButton ) {
      if ( !m_cameraInteracting )
        m_cameraInteracting = true;
      m_interactionTimer.start( 250 );
    }

#ifdef ENABLE_THREADED_GL
    d->renderMutex.lock();
#endif
    // Use quick render while the mouse is down
    if (d->allowQuickRender)
      d->quickRender = true;
#ifdef ENABLE_THREADED_GL
    d->renderMutex.unlock();
#endif
    QUndoCommand *command = dispatchMouse(event);
    if ( command && d->undoStack ) {
      d->undoStack->push( command );
    }
    emit mouseMove(event);
  }

  bool GLWidget::touchEvent( QTouchEvent * event )
  {
    // Feed the per-widget touch state machine (add-touch-navigation). It returns
    // synthesized mouse events (widget-local logical positions) plus camera
    // deltas; tools never see this QTouchEvent (design D2/D3).
    const QList<QEventPoint> points = event->points();
    // Derive the phase from the raw event type: this Qt build's
    // QTouchEvent::isBeginEvent()/isEndEvent() are stubs that always return
    // true, so event->type() (QEvent::TouchBegin/TouchUpdate/TouchEnd) is the
    // only reliable discriminator.
    detail::TouchState::Phase phase = detail::TouchState::Update;
    switch ( event->type() ) {
      case QEvent::TouchBegin:
        phase = detail::TouchState::Begin;
        break;
      case QEvent::TouchUpdate:
        phase = detail::TouchState::Update;
        break;
      case QEvent::TouchEnd:
        phase = detail::TouchState::End;
        break;
      default:
        break;
    }
    detail::TouchResult result = d->m_touch.process(
      points, phase, static_cast<qint64>(event->timestamp()));

    // Two-finger gesture: drive the camera BEFORE any tool sees input. The
    // distance change between the two tracked points zooms, using the wheel
    // sign convention (fingers apart = zoom in); the centroid movement pans.
    // Neither reaches a tool (design D4). The zoom reuses the wheel formula
    // exactly; the pan matches the middle-drag translate path.
    if ( result.cameraActive ) {
      if ( result.zoomDistanceDelta != 0.0 ) {
        const double distanceDeltaDevice =
          result.zoomDistanceDelta * devicePixelRatioF();
        Navigate::zoom( this, center(),
                        -MOUSE_WHEEL_SPEED * distanceDeltaDevice );
      }
      if ( result.centroidFrom != result.centroidTo ) {
        Navigate::translate( this, center(),
                             result.centroidFrom * devicePixelRatioF(),
                             result.centroidTo * devicePixelRatioF() );
      }
      update();
    }

    // Single-finger gesture: route the synthesized mouse events to the tool
    // exactly like a real mouse event (shared dispatchMouse => same HiDPI
    // scaling and tool -> navigate-fallback dispatch). A press the active tool
    // declines falls through to the navigate tool, preserving one-finger
    // rotate (design D5, task 2.4). Press-like push/erase semantics. The
    // SyntheticMouseEvent is reconstructed as a QMouseEvent here (Qt events are
    // non-copyable, so they live only as long as this loop iteration).
    for ( const detail::SyntheticMouseEvent& sm : result.mouseEvents ) {
      QMouseEvent me( sm.type, sm.position, sm.position, sm.button,
                      sm.buttons, sm.modifiers );
      // Route the synthesized mouse event through the same dispatch the real
      // mouse handlers use (tool -> navigate-fallback), and push the returned
      // command to the undo stack exactly like mousePressEvent/ReleaseEvent do
      // below, so a synthesized click/lasso actually mutates the molecule.
      QUndoCommand *command = dispatchMouse( &me );
      if ( command && d->undoStack )
        d->undoStack->push( command );
    }
    if ( !result.mouseEvents.isEmpty() )
      update();

    // We handle the whole touch stream; Qt must not also synthesize mouse
    // events from it (that is the point of WA_AcceptTouchEvents).
    return true;
  }

  void GLWidget::wheelEvent( QWheelEvent * event )
  {
    // Set the event to ignored, check whether any tools accept it
    event->ignore();
    if ( d->tool ) {
      QUndoCommand *command;
      command = d->tool->wheelEvent( this, event );
      // If the mouse event is not accepted, pass it to the navigate tool
      if (!event->isAccepted() && m_navigateTool) {
        command = m_navigateTool->wheelEvent(this, event);
      }
      if ( command && d->undoStack ) {
        d->undoStack->push( command );
      }
    }
    emit wheel(event);
  }

  void GLWidget::mouseDoubleClickEvent( QMouseEvent * event )
  {
    // Set the event to ignored, check whether any tools accept it
    event->ignore();

    if ( d->tool ) {
      QUndoCommand *command;
      command = d->tool->mouseDoubleClickEvent( this, event );
      // If the mouse event is not accepted, pass it to the navigate tool
      if (!event->isAccepted() && m_navigateTool) {
        command = m_navigateTool->mouseDoubleClickEvent(this, event);
      }

      if ( command && d->undoStack ) {
        d->undoStack->push( command );
      }
    }
#ifdef ENABLE_THREADED_GL
    d->renderMutex.lock();
#endif
    // Stop using quickRender
    d->quickRender = false;
#ifdef ENABLE_THREADED_GL
    d->renderMutex.unlock();
#endif
    // Render the scene at full quality now the mouse button has been released
    update();
    emit mouseDoubleClick(event);
  }

  void GLWidget::keyPressEvent(QKeyEvent *event)
  {
    event->ignore();
    if (d->tool) {
      QUndoCommand *command;
      command = d->tool->keyPressEvent(this, event);
      // If the key event is not accepted, pass it to the navigate tool
      if (!event->isAccepted() && m_navigateTool) {
        command = m_navigateTool->keyPressEvent(this, event);
      }
      if ( command && d->undoStack ) {
        d->undoStack->push( command );
      }
    }
    update();
  }

  void GLWidget::keyReleaseEvent(QKeyEvent *event)
  {
    event->ignore();
    if (d->tool) {
      QUndoCommand *command;
      command = d->tool->keyReleaseEvent(this, event);
      // If the mouse event is not accepted, pass it to the navigate tool
      if (!event->isAccepted() && m_navigateTool) {
        command = m_navigateTool->keyReleaseEvent(this, event);
      }
      if ( command && d->undoStack ) {
        d->undoStack->push( command );
      }
    }
  }

  void GLWidget::setMolecule(Molecule *molecule)
  {
    if (!molecule)
      return;

    // disconnect from our old molecule
    if (d->molecule)
      disconnect(d->molecule, 0, this, 0);

    // Emit the molecule changed signal
    emit moleculeChanged(molecule);

    d->molecule = molecule;

    // Clear the selection list
    bool selectionDidChange = !d->selectedPrimitives.isEmpty();
    d->selectedPrimitives.clear();
    if (selectionDidChange)
      emit selectionChanged();

    // compute the molecule's geometric info
    updateGeometry();
    invalidateDLs();

    // When the molecule is updated, the display lists become invalid, we should
    // also render the updated molecule. This should be much simpler than before.
    connect(d->molecule, SIGNAL(updated()), this, SLOT(invalidateDLs()));
    connect(d->molecule, SIGNAL(updated()), this, SLOT(updateGeometry()));
    connect(d->molecule, SIGNAL(updated()), this, SLOT(update()));

    // If primitives, atoms, or bonds are removed, we need to delete them from the selected list
    connect(d->molecule, SIGNAL(primitiveRemoved(Primitive*)),
            this, SLOT(unselectPrimitive(Primitive*)));
    connect(d->molecule, SIGNAL(atomRemoved(Atom*)),
            this, SLOT(unselectAtom(Atom*)));
    connect(d->molecule, SIGNAL(bondRemoved(Bond*)),
            this, SLOT(unselectBond(Bond*)));

    // setup the camera to have a nice viewpoint on the molecule
    d->camera->initializeViewPoint();

    update();
  }

  void GLWidget::unselectPrimitive(Primitive *p)
  {
    bool selectionDidChange = d->selectedPrimitives.contains(p);
    d->selectedPrimitives.remove( p );
    // The engine caches must be invalidated
    d->updateCache = true;
    if (selectionDidChange)
      emit selectionChanged();

    // TODO: remove also from named selections
  }

  void GLWidget::unselectAtom(Atom *a)
  {
    unselectPrimitive(a);
  }

  void GLWidget::unselectBond(Bond *b)
  {
    unselectPrimitive(b);
  }

  const Molecule* GLWidget::molecule() const
  {
    return d->molecule;
  }

  Molecule* GLWidget::molecule()
  {
    return d->molecule;
  }

  const Vector3d & GLWidget::center() const
  {
    return d->center;
  }

  const Vector3d & GLWidget::normalVector() const
  {
    return d->normalVector;
  }

  double GLWidget::radius() const
  {
    return d->radius;
  }

  const Atom *GLWidget::farthestAtom() const
  {
    return d->farthestAtom;
  }

  void GLWidget::updateGeometry()
  {
    if (!d->molecule)
      return;

    // Try to get a read lock for the molecule
    if (!d->molecule->lock()->tryLockForRead())
      return;

    // The molecule radius, center, normal vector, and farthest atom methods
    // already account for the unit cell, if present:
    d->center = d->molecule->center();
    d->radius = d->molecule->radius();
    d->normalVector = d->molecule->normalVector();
    d->farthestAtom = d->molecule->farthestAtom();

    // if any cell repeats are used, adjust the geometries
    if (d->molecule->OBUnitCell() &&
        (d->aCells > 1 || d->bCells > 1 || d->cCells > 1)) {
      // Cache unit cell info
      std::vector<vector3> cellVectors = d->molecule->OBUnitCell()->GetCellVectors();
      const Vector3d aVec (cellVectors[0].AsArray());
      const Vector3d bVec (cellVectors[1].AsArray());
      const Vector3d cVec (cellVectors[2].AsArray());

      // The center must be updated to account for the images
      d->center += (static_cast<double>(d->aCells - 1) * aVec +
                    static_cast<double>(d->bCells - 1) * bVec +
                    static_cast<double>(d->cCells - 1) * cVec) * 0.5;

      // Exploit symmetry and only calculate distance to four of the corners
      // of the supercell
      double scSqRadii[4];
      scSqRadii[0] = ((Eigen::Vector3d::Zero()) - d->center).squaredNorm();
      scSqRadii[1] = (static_cast<double>(d->aCells) * aVec -
                      d->center).squaredNorm();
      scSqRadii[2] = (static_cast<double>(d->bCells) * bVec -
                      d->center).squaredNorm();
      scSqRadii[3] = (static_cast<double>(d->cCells) * cVec -
                      d->center).squaredNorm();

      // Select the largest radius
      double scSqRadius = scSqRadii[0];
      if (scSqRadius > scSqRadii[1])
        scSqRadius = scSqRadii[1];
      if (scSqRadius > scSqRadii[2])
        scSqRadius = scSqRadii[2];
      if (scSqRadius > scSqRadii[3])
        scSqRadius = scSqRadii[3];
      d->radius = sqrt(scSqRadius);
    }

    d->molecule->lock()->unlock();
  }

  Camera * GLWidget::camera() const
  {
    return d->camera;
  }

  QList<Engine *> GLWidget::engines() const
  {
    return d->engines;
  }

  unsigned int GLWidget::backendDrawCallsLastFrame() const
  {
    return d->backendDrawCalls;
  }

  unsigned int GLWidget::backendFramesSubmitted() const
  {
    return d->backendFrames;
  }

  rendering::LodLevel GLWidget::modernDetailLevel() const
  {
    // d->backend is null before the first modern-tier frame (or on the legacy/
    // compat tier); report the default Medium level so tests have a stable
    // baseline (task 4.1).
    return d->backend ? d->backend->detailLevel() : rendering::LodMedium;
  }

  bool GLWidget::rendererSupportsQuality() const
  {
    // Modern (instanced LOD) and legacy (quality-dependent GL_LIGHT1) apply
    // quality; the compat tier renders through the fixed-function chunk path
    // and ignores it (gpu-rendering-pipeline delta, honest gating).
    return d->rendererTier != rendering::RendererTier::Compat;
  }

  bool GLWidget::rendererSupportsFog() const
  {
    // Modern (shader fog uniforms) and legacy (glFog*) apply fog; compat has
    // no fog path (gpu-rendering-pipeline delta, honest gating).
    return d->rendererTier != rendering::RendererTier::Compat;
  }

  unsigned int GLWidget::presentedRevision() const
  {
    return d->presentedRevision;
  }

  QSize GLWidget::frameSize() const
  {
    // d->backend is null when the widget has not rendered a modern-tier frame
    // yet (or fell back to the legacy tier); report an invalid size so the
    // caller can distinguish "no frame yet" from "a 0x0 frame".
    return d->backend ? d->backend->frameSize() : QSize();
  }

  void GLWidget::addEngine(Engine *engine)
  {
    connect(engine, SIGNAL(changed()), this, SLOT(update()));
    connect(engine, SIGNAL(changed()), this, SLOT(invalidateDLs()));
    // Engine settings/selection changes invalidate the worker-built chunks
    // even though the molecule revision is unchanged (task 6.1).
    connect(engine, SIGNAL(changed()), this, SLOT(sceneContentsChanged()));
    connect(this, SIGNAL(moleculeChanged(Molecule *)),
            engine, SLOT(setMolecule(Molecule *)));
    d->engines.append(engine);
    std::sort(d->engines.begin(), d->engines.end(), engineLessThan);
    engine->setPainterDevice(d->pd);
    // If a molecule is already active, propagate it to the newly added engine.
    // The moleculeChanged() signal fired in the widget constructor (setMolecule)
    // before this engine connected, so the engine's own molecule() would stay
    // null. The snapshot/worker path (buildSnapshotAndSubmit) indexes the
    // engine's captured atoms by the snapshot's atom count, so a null molecule
    // there yields fewer captured atoms than snapshot atoms and the worker
    // dereferences out of bounds (snapshotTessellateBSDY/emitOpaqueAtoms).
    if (d->molecule)
      engine->setMolecule(d->molecule);
    emit engineAdded(engine);
    update();
  }

  void GLWidget::removeEngine(Engine *engine)
  {
    disconnect(engine, 0, this, 0);
    disconnect(this, 0, engine, 0);
    d->engines.removeAll(engine);
    emit engineRemoved(engine);
    engine->deleteLater();
    update();
  }

  void GLWidget::setTool(Tool *tool)
  {
    if ( tool ) {
      d->tool = tool;
    }
  }

  void GLWidget::setToolGroup(ToolGroup *toolGroup)
  {
    if ( d->toolGroup ) {
      disconnect( d->toolGroup, 0, this, 0 );
    }

    if ( toolGroup ) {
      d->toolGroup = toolGroup;
      d->tool = toolGroup->activeTool();
      connect( toolGroup, SIGNAL( toolActivated( Tool* ) ),
               this, SLOT( setTool( Tool* ) ) );
      connect( toolGroup, SIGNAL( toolsDestroyed() ),
               this, SLOT( toolsDestroyed() ) );
    }
    // Find the navigate tool and set it
    foreach (Tool *tool, d->toolGroup->tools()) {
      if (tool->identifier() == "Navigate") {
        m_navigateTool = tool;
      }
    }
  }

  void GLWidget::toolsDestroyed()
  {
    d->tool = 0;
    m_navigateTool = 0;
  }

  void GLWidget::addTextOverlay(QLabel* str)
  {
    d->textOverlayMutex.lock();
    d->textOverlayLabels.append(QPointer<QLabel>(str));
    d->textOverlayMutex.unlock();
  }

  void GLWidget::addTextOverlay(const QList<QLabel*> &strs)
  {
    d->textOverlayMutex.lock();
    for (QList<QLabel*>::const_iterator
           it = strs.constBegin(),
           it_end = strs.constEnd();
         it != it_end; ++it) {
      d->textOverlayLabels.append(QPointer<QLabel>(*it));
    }
    d->textOverlayMutex.unlock();
  }

  void GLWidget::setExtensions(QList<Extension*> extensions)
  {
    d->extensions = extensions;
  }


  void GLWidget::setUndoStack( QUndoStack *undoStack )
  {
    // The only caller (MainWindow) constructs an UndoStack; store it as such so
    // the 7 push sites below resolve to the locked UndoStack::push().
    d->undoStack = static_cast<UndoStack*>(undoStack);
  }

  UndoStack *GLWidget::undoStack() const
  {
    return d->undoStack;
  }

  Tool* GLWidget::tool() const
  {
    return d->tool;
  }

  ToolGroup *GLWidget::toolGroup() const
  {
    return d->toolGroup;
  }

  Painter *GLWidget::painter() const
  {
    return d->painter;
  }

  QList<GLHit> GLWidget::hits( int x, int y, int w, int h )
  {
    QList<GLHit> hits;

    if ( !molecule() ) return hits;

    // The modern tier has no GL_SELECT name stack (task 4.1): its picking is a
    // colour-ID pass over the same chunk buffers the frame was drawn with.
    if (d->useModernTier && d->backend && d->backend->isInitialized()) {
      QList<GLHit> modernHits = d->backend->pick(x, y, w, h);
      if (!modernHits.isEmpty())
        return modernHits;
      // An empty result is a genuine miss: fall through to nothing rather
      // than the legacy name-stack path, which would re-render with
      // fixed-function engines the modern tier does not draw.
      return hits;
    }

    // The GL_SELECT selection buffer is capped at SEL_BUF_MAX_SIZE
    // (global.h:116). glRenderMode(GL_RENDER) returns -1 when the buffer
    // overflowed before every name was recorded. Never truncate silently
    // (task 4.2 / design A4): detect the overflow, shrink the selection
    // rectangle around its centre and retry, reporting each overflow.
    const int maxRefineAttempts = 6;
    int attempt = 0;
    int hit_count = 0;
    bool overflowReported = false;

    // setup the selection buffer
    int requiredSelectBufSize = (d->molecule->numAtoms() + d->molecule->numBonds()) * 8;
    if ( requiredSelectBufSize > d->selectBufSize ) {
      //resize selection buffer
      if ( d->selectBuf ) delete[] d->selectBuf;
      // add some margin so that resizing doesn't occur every time an atom is added
      d->selectBufSize = requiredSelectBufSize + SEL_BUF_MARGIN;
      if ( d->selectBufSize > SEL_BUF_MAX_SIZE ) {
        d->selectBufSize = SEL_BUF_MAX_SIZE;
      }
      d->selectBuf = new GLuint[d->selectBufSize];
    }

    while ( true ) {
      GLint viewport[4];
      int cx = w/2 + x;
      int cy = h/2 + y;

#ifdef ENABLE_THREADED_GL
      d->renderMutex.lock();
#endif
      makeCurrent();
      //X   hits.clear();

      glSelectBuffer( d->selectBufSize, d->selectBuf );
      glRenderMode( GL_SELECT );
      glInitNames();

      // Setup a projection matrix for picking in the zone delimited by (x,y,w,h).
      glGetIntegerv( GL_VIEWPORT, viewport );
      glMatrixMode( GL_PROJECTION );
      glPushMatrix();
      glLoadIdentity();
      gluPickMatrix( cx,viewport[3]-cy, w, h,viewport );

      // now multiply that projection matrix with the perspective of the camera
      // (applyProjection() authors the matrix CPU-side; the upload below is a
      // pure consumer that composes it after gluPickMatrix).
      d->camera->applyProjection();
      glMultMatrixd( d->camera->projection().data() );

      // now load the modelview matrix from the camera
      glMatrixMode( GL_MODELVIEW );
      glPushMatrix();
      glLoadIdentity();
      d->camera->applyModelview();

      // now actually render using low quality, "pickrender"
      d->painter->begin(this);
#ifdef ENABLE_GLSL
          if (m_glslEnabled) glUseProgramObjectARB(0);
#endif
      foreach(Engine *engine, d->engines) {
        if(engine->isEnabled()) {
          engine->renderPick(d->pd);
        }
      }
      d->painter->end();

      // returning to normal rendering mode. A return value of -1 means the
      // selection buffer overflowed (hit_count kept as int to detect it).
      hit_count = glRenderMode( GL_RENDER );

      glMatrixMode( GL_PROJECTION );
      glPopMatrix();
      glMatrixMode( GL_MODELVIEW );
      glPopMatrix();

#ifdef ENABLE_THREADED_GL
      doneCurrent();
      d->renderMutex.unlock();
#endif

      // A clean result (>= 0) finishes the loop; an overflow (-1) shrinks the
      // box (centre fixed) and retries until it fits or attempts are exhausted.
      // The decision is isolated in Avogadro::decideSelectRefine so it can be
      // unit tested without a compatibility GL context (task 4.2 / design A4).
      overflowReported = overflowReported || (hit_count == -1);
      if (Avogadro::decideSelectRefine(hit_count, attempt, maxRefineAttempts,
                                       x, y, w, h)
              != Avogadro::SelectRefine::Retry)
        break;
    }

    if ( overflowReported )
      qDebug() << "GLSelect: selection buffer overflow detected; refined selection region after"
               << attempt << "shrinking attempt(s)";

    // if no error occurred and there are hits, process them
    if ( hit_count > 0 ) {
      int i;
      unsigned int j;
      GLuint names, type, *ptr;
      GLuint minZ, maxZ;
      long name;

      //X   printf ("hits = %d\n", hits);
      ptr = ( GLuint * ) d->selectBuf;
      // for all hits and not past end of buffer
      for (i = 0; i < hit_count && !(ptr > d->selectBuf + d->selectBufSize); ++i) {
        names = *ptr++;
        // make sure that we won't be passing the end of bufer
        if ( ptr + names + 2 > d->selectBuf + d->selectBufSize ) {
          break;
        }
        minZ = *ptr++;
        maxZ = *ptr++;

        // allow names of 0
        name = -1;
        for (j = 0; j < names/2; ++j) { /*  for each name */
          type = *ptr++;
          name = *ptr++;
        }
        if (name > -1) {
          //            printf ("%ld(%d) ", name,type);
          hits.append( GLHit( type,name,minZ,maxZ ) );
        }
      }
      //      printf ("\n");
      std::sort(hits.begin(), hits.end());
    }

    return( hits );
  }

  Primitive* GLWidget::computeClickedPrimitive(const QPoint& p)
  {
    QList<GLHit> chits;

    // Perform an OpenGL selection and retrieve the list of hits.
    chits = hits(p.x()-SEL_BOX_HALF_SIZE,
                 p.y()-SEL_BOX_HALF_SIZE,
                 SEL_BOX_SIZE, SEL_BOX_SIZE);

    // Find the first atom or bond (if any) in hits - this will be the closest
    foreach(const GLHit& hit, chits)
    {
      //qDebug() << "Hit: " << hit.name();
      if(hit.type() == Primitive::AtomType)
        return molecule()->atom(hit.name());
      else if(hit.type() == Primitive::BondType)
        return molecule()->bond(hit.name());
    }
    return 0;
  }

  Atom* GLWidget::computeClickedAtom(const QPoint& p)
  {
    QList<GLHit> chits;

    // Perform an OpenGL selection and retrieve the list of hits.
    chits = hits(p.x()-SEL_BOX_HALF_SIZE,
                 p.y()-SEL_BOX_HALF_SIZE,
                 SEL_BOX_SIZE, SEL_BOX_SIZE);

    // Find the first atom (if any) in hits - this will be the closest
    foreach(const GLHit& hit, chits)
      if(hit.type() == Primitive::AtomType)
        return molecule()->atom(hit.name());

    return 0;
  }

  Bond* GLWidget::computeClickedBond(const QPoint& p)
  {
    QList<GLHit> chits;

    // Perform an OpenGL selection and retrieve the list of hits.
    chits = hits(p.x()-SEL_BOX_HALF_SIZE,
                 p.y()-SEL_BOX_HALF_SIZE,
                 SEL_BOX_SIZE, SEL_BOX_SIZE);

    // Find the first bond (if any) in hits - this will be the closest
    foreach(const GLHit& hit, chits)
      if(hit.type() == Primitive::BondType)
        return static_cast<Bond *>(molecule()->bond(hit.name()));

    return 0;
  }

  QSize GLWidget::sizeHint() const
  {
    return minimumSizeHint();
  }

  QSize GLWidget::minimumSizeHint() const
  {
    return QSize( 200,200 );
  }

  double GLWidget::radius( const Primitive *p ) const
  {
    if (!p)
      return 0.0;
    double radius = 0.0;
    foreach(Engine *engine, d->engines)
    {
      if (engine->isEnabled())
      {
        double engineRadius = engine->radius( d->pd, p );
        if ( engineRadius > radius )
          radius = engineRadius;
      }
    }

    return radius;
  }

  void GLWidget::setSelected(PrimitiveList primitives, bool select)
  {
    bool selectionDidChange = false;
    foreach(Primitive *item, primitives) {
      if (select && !d->selectedPrimitives.contains(item)) {
        d->selectedPrimitives.insert( item );
        selectionDidChange = true;
      }
      else if (!select && d->selectedPrimitives.contains(item)) {
        d->selectedPrimitives.remove( item );
        selectionDidChange = true;
      }
      // The engine caches must be invalidated
      d->updateCache = true;
      //      item->update();
    }
    if (selectionDidChange)
      emit selectionChanged();
  }

  PrimitiveList GLWidget::selectedPrimitives() const
  {
    // Materialize on demand: this is a rare call, so the O(n) copy is fine.
    return PrimitiveList(QList<Primitive *>(d->selectedPrimitives.begin(),
                                            d->selectedPrimitives.end()));
  }

  void GLWidget::toggleSelected( PrimitiveList primitives )
  {
    bool selectionDidChange = false;
    foreach(Primitive *item, primitives)
    {
      if (d->selectedPrimitives.contains(item)) {
        d->selectedPrimitives.remove( item );
        selectionDidChange = true;
      }
      else {
        d->selectedPrimitives.insert(item);
        selectionDidChange = true;
      }
    }
    // The engine caches must be invalidated
    d->updateCache = true;
    if (selectionDidChange)
      emit selectionChanged();
  }

  void GLWidget::toggleSelected()
  {
    if (!d->molecule) return;
    bool selectionDidChange = false;
    // Currently handle atoms and bonds
    foreach(Atom *a, d->molecule->atoms()) {
      Primitive *p = static_cast<Primitive *>(a);
      if (d->selectedPrimitives.contains(p)) {
        d->selectedPrimitives.remove(p);
        selectionDidChange = true;
      }
      else {
        d->selectedPrimitives.insert(p);
        selectionDidChange = true;
      }
    }
    foreach(Bond *b, d->molecule->bonds()) {
      Primitive *p = static_cast<Primitive *>(b);
      if (d->selectedPrimitives.contains(p)) {
        d->selectedPrimitives.remove(p);
        selectionDidChange = true;
      }
      else {
        d->selectedPrimitives.insert(p);
        selectionDidChange = true;
      }
    }
    // The engine caches must be invalidated
    d->updateCache = true;
    if (selectionDidChange)
      emit selectionChanged();
  }

  void GLWidget::clearSelected()
  {
    bool selectionDidChange = !d->selectedPrimitives.isEmpty();
    d->selectedPrimitives.clear();
    // The engine caches must be invalidated
    d->updateCache = true;
    if (selectionDidChange)
      emit selectionChanged();
  }

  bool GLWidget::isSelected( const Primitive *p ) const
  {
    // Empty-set early-out: when nothing is selected (the common case) this
    // returns false without touching the hash set. Every per-primitive
    // selection check in the engines funnels through here, so this single
    // guard covers bsdyengine/sphereengine/cartoonengine render loops.
    if (d->selectedPrimitives.isEmpty())
      return false;
    // Return true if the item is selected (O(1) lookup in the QSet).
    return d->selectedPrimitives.contains(const_cast<Primitive *>(p));
  }

  bool GLWidget::addNamedSelection(const QString &name, PrimitiveList &primitives)
  {
    // make sure the name is unique
    for (int i = 0; i < d->namedSelections.size(); ++i)
      if (d->namedSelections.at(i).first == name)
        return false;

    QList<unsigned int> atomIds;
    QList<unsigned int> bondIds;
    foreach(Primitive *item, primitives) {
      if (item->type() == Primitive::AtomType)
        atomIds.append(item->id());
      if (item->type() == Primitive::BondType)
        bondIds.append(item->id());
    }

    QPair<QList<unsigned int>,QList<unsigned int> > pair(atomIds, bondIds);
    QPair<QString, QPair<QList<unsigned int>,QList<unsigned int> > > namedSelection(name, pair);
    d->namedSelections.append(namedSelection);

    emit namedSelectionsChanged();
    return true;
  }

  void GLWidget::removeNamedSelection(const QString &name)
  {
    for (int i = 0; i < d->namedSelections.size(); ++i)
      if (d->namedSelections.at(i).first == name) {
        d->namedSelections.removeAt(i);
        emit namedSelectionsChanged();
        return;
      }
  }

  void GLWidget::removeNamedSelection(int index)
  {
    d->namedSelections.removeAt(index);
  }

  void GLWidget::renameNamedSelection(int index, const QString &name)
  {
    if (name.isEmpty())
      return;

    QPair<QString, QPair<QList<unsigned int>,QList<unsigned int> > > pair = d->namedSelections.takeAt(index);
    pair.first = name;
    d->namedSelections.insert(index, pair);
    emit namedSelectionsChanged();
  }

  QList<QString> GLWidget::namedSelections()
  {
    QList<QString> names;
    for (int i = 0; i < d->namedSelections.size(); ++i)
      names.append(d->namedSelections.at(i).first);

    return names;
  }

  PrimitiveList GLWidget::namedSelectionPrimitives(const QString &name)
  {
    for (int i = 0; i < d->namedSelections.size(); ++i)
      if (d->namedSelections.at(i).first == name) {
    return namedSelectionPrimitives(i);
    }

    return PrimitiveList();
  }

  PrimitiveList GLWidget::namedSelectionPrimitives(int index)
  {
    PrimitiveList list;
    if (!d->molecule)
      return list;

    for (int j = 0; j < d->namedSelections.at(index).second.first.size(); ++j) {
      Atom *atom = d->molecule->atomById(d->namedSelections.at(index).second.first.at(j));
      if (atom)
        list.append(atom);
    }

    for (int j = 0; j < d->namedSelections.at(index).second.second.size(); ++j) {
      Bond *bond = d->molecule->bondById(d->namedSelections.at(index).second.second.at(j));
      if (bond)
        list.append(bond);
    }

    return list;
  }

  void GLWidget::setUnitCells( int a, int b, int c )
  {
    d->aCells = a;
    d->bCells = b;
    d->cCells = c;
    updateGeometry();
    d->camera->initializeViewPoint();
    update();
  }

  void GLWidget::setUnitCellColor(const QColor c)
  {
#ifdef ENABLE_THREADED_GL
    d->renderMutex.lock();
#endif
    d->cellColor = c;
#ifdef ENABLE_THREADED_GL
    d->renderMutex.unlock();
#endif
  }

  void GLWidget::setOnlyRenderOriginalUnitCell(bool b)
  {
    d->onlyRenderOriginalUnitCell = b;
    update();
  }

  void GLWidget::clearUnitCell()
  {
    updateGeometry();
    d->camera->initializeViewPoint();
    update();
  }

  int GLWidget::aCells() const
  {
    return d->aCells;
  }

  int GLWidget::bCells() const
  {
    return d->bCells;
  }

  int GLWidget::cCells() const
  {
    return d->cCells;
  }

  Color GLWidget::unitCellColor() const
  {
    return Color(d->cellColor);
  }

  bool GLWidget::onlyRenderOriginalUnitCell()
  {
    return d->onlyRenderOriginalUnitCell;
  }

  void GLWidget::setProjection(GLWidget::ProjectionType type)
  {
    d->projection = type;
    updateGeometry();
    d->camera->initializeViewPoint();
    update();
  }

  GLWidget::ProjectionType GLWidget::projection() const
  {
    return d->projection;
  }


  inline double GLWidget::computeFramesPerSecond()
  {
    static QElapsedTimer time;
    static bool firstTime = true;
    static int old_time, new_time;
    static int frames;
    static double fps;

    if( firstTime )
    {
      time.start();
      firstTime = false;
      old_time = time.elapsed();
      frames = 0;
      fps = 0;
    }

    new_time = time.elapsed();
    frames++;

    if( new_time - old_time > 200 )
    {
      fps = 1000.0 * frames / double( new_time - old_time );
      frames = 0;
      time.restart();
      old_time = time.elapsed();
    }

    return fps;
  }

  void GLWidget::writeSettings(QSettings &settings) const
  {
    settings.setValue("background", d->background);
    settings.setValue("quality", d->painter->quality());
    settings.setValue("fogLevel", d->fogLevel);
    settings.setValue("renderAxes", d->renderAxes);
    settings.setValue("renderDebug", d->renderDebug);
    settings.setValue("renderModelViewDebug", d->renderModelViewDebug);
    settings.setValue("allowQuickRender", d->allowQuickRender);
    settings.setValue("renderUnitCellAxes", d->renderUnitCellAxes);
    settings.setValue("projection", d->projection);

    int count = d->engines.size();
    settings.beginWriteArray("engines");
    // Clear the array before writing settings - fixes issue when index changes
    settings.remove("");
    for(int i = 0; i< count; i++) {
      settings.setArrayIndex(i);
      d->engines.at(i)->writeSettings(settings);
    }
    settings.endArray();
  }

  void GLWidget::readSettings(QSettings &settings)
  {
    // Make sure to provide some default values for any settings.value("", DEFAULT) call
    setQuality(settings.value("quality", 2).toInt());
    setFogLevel(settings.value("fogLevel", 0).toInt());
    d->background = settings.value("background", QColor(0,0,0,0)).value<QColor>();
    d->renderAxes = settings.value("renderAxes", 1).value<bool>();
    d->renderDebug = settings.value("renderDebug", 0).value<bool>();
    d->renderModelViewDebug =
        settings.value("renderModelViewDebug", 0).value<bool>();
    d->allowQuickRender = settings.value("allowQuickRender", 1).value<bool>();
    d->renderUnitCellAxes = settings.value("renderUnitCellAxes", 1).value<bool>();
    int pr = settings.value("projection", GLWidget::Perspective).toInt();
    // Makes the compiler happy about the type conversion.
    if (pr != 1 && pr != 2)
      pr = 1; // set a default if this is screwed up
    d->projection = GLWidget::ProjectionType(pr);

    loadEngines(settings);

    if(!d->engines.count())
      loadDefaultEngines();
  }

  void GLWidget::loadEngines(QSettings &settings)
  {
    int count = settings.beginReadArray("engines");
    int numEnabled = 0;

    PluginManager *plugins = PluginManager::instance();
    QList<QString> allEngines = plugins->identifiers(Plugin::EngineType);
    for(int i=0; i<count; i++) {
      settings.setArrayIndex(i);
      QString engineClass = settings.value("engineID", QString()).toString();
      PluginFactory *factory = plugins->factory(engineClass,
                                                Plugin::EngineType);
      if(!engineClass.isEmpty() && factory) {
        Engine *engine = static_cast<Engine *>(factory->createInstance(this));
        engine->readSettings(settings);
        if(engine->isEnabled())
          numEnabled++;
        addEngine(engine);
        allEngines.removeAll(engineClass);
      }
    }

    qDebug() << "Settings are missing for the next engines:" << allEngines;

    // Add engines with missing settings
    foreach(const QString &engineClass, allEngines) {
      PluginFactory *factory = plugins->factory(engineClass,
                                                Plugin::EngineType);
      if(factory) {
        Engine *engine = static_cast<Engine *>(factory->createInstance(this));
        addEngine(engine);
        allEngines.removeAll(engineClass);
      }
    }

    settings.endArray();

    // Enable default engine if nothing is enabled
    if(!d->engines.isEmpty() && (numEnabled == 0)) {
      foreach(Engine *engine, d->engines) {
        if(engine->identifier() == "Ball and Stick")
          engine->setEnabled(true);
      }
    }
  }

  void GLWidget::loadDefaultEngines()
  {
    QList<Engine *> engines = d->engines;

    foreach(Engine *engine, engines)
      this->removeEngine(engine);

    PluginManager *plugins = PluginManager::instance();
    foreach(PluginFactory *factory, plugins->factories(Plugin::EngineType)) {
      Engine *engine = static_cast<Engine *>(factory->createInstance(this));
      if (engine->identifier() == "Ball and Stick")
        engine->setEnabled(true);
      addEngine(engine);
    }
  }

  void GLWidget::reloadEngines()
  {
    QSettings settings;

    // save engine settings
    int count = d->engines.size();
    settings.beginWriteArray("engines");
    for(int i = 0; i< count; i++) {
      settings.setArrayIndex(i);
      d->engines.at(i)->writeSettings(settings);
    }
    settings.endArray();

    // delete engines
    foreach(Engine *engine, d->engines) {
      delete engine;
    }

    // clear the engine list
    d->engines.clear();

    loadEngines(settings);
  }


  void GLWidget::setQuickRender(bool enabled)
  {
    d->allowQuickRender = enabled;
  }

  bool GLWidget::quickRender() const
  {
    return d->allowQuickRender;
  }

  void GLWidget::setRenderUnitCellAxes(bool enabled)
  {
    if (d->renderUnitCellAxes != enabled) {
      d->renderUnitCellAxes = enabled;
      emit unitCellAxesRenderChanged(enabled);
    }
  }

  bool GLWidget::renderUnitCellAxes() const
  {
    return d->renderUnitCellAxes;
  }

  void GLWidget::invalidateDLs()
  {
    // Something changed and we need to invalidate the display lists
    d->updateCache = true;
  }

// Copied from current sources of Qt 4.7
#ifndef QT_OPENGL_ES

static void save_gl_state()
{
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

    glShadeModel(GL_FLAT);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void restore_gl_state()
{
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
    glPopClientAttrib();
}

  void gl_draw_text(QPainter *p, int x, int y, const QString &str, const QFont &font, double zoom)
  {
    GLfloat color[4];
    glGetFloatv(GL_CURRENT_COLOR, &color[0]);

    QFont fore_font = font;
    qreal size = font.pointSizeF() * zoom;
    fore_font.setPointSizeF(size);

    QColor col, outline;
    col.setRgbF(color[0], color[1], color[2],color[3]);
    outline.setRgbF((1-color[0])/2,(1-color[1])/2,(1-color[2])/2); // use opposite color, but make it darker
    if(!p) return;  // prevent segfaults

    QPen old_pen = p->pen();
    QFont old_font = p->font();

    // Outline
    p->setPen(outline);
    //QFont out_font = fore_font;
    //out_font.setWeight(font.weight()+10);
    p->setFont(fore_font);
   // p->drawText(x+2, y, str);
    p->drawText(x+1, y, str);
    p->drawText(x-1, y, str);
   // p->drawText(x-2, y, str);
    p->drawText(x, y+1, str);
   // p->drawText(x, y+2, str);
    p->drawText(x, y-1, str);
   // p->drawText(x, y-2, str);

    // Foreground
    p->setPen(col);
    p->setFont(fore_font);
    p->drawText(x, y, str);

    p->setPen(old_pen);
    p->setFont(old_font);
  }

static inline void transform_point(GLdouble out[4], const GLdouble m[16], const GLdouble in[4])
{
#define M(row,col)  m[col*4+row]
    out[0] =
        M(0, 0) * in[0] + M(0, 1) * in[1] + M(0, 2) * in[2] + M(0, 3) * in[3];
    out[1] =
        M(1, 0) * in[0] + M(1, 1) * in[1] + M(1, 2) * in[2] + M(1, 3) * in[3];
    out[2] =
        M(2, 0) * in[0] + M(2, 1) * in[1] + M(2, 2) * in[2] + M(2, 3) * in[3];
    out[3] =
        M(3, 0) * in[0] + M(3, 1) * in[1] + M(3, 2) * in[2] + M(3, 3) * in[3];
#undef M
}

static inline GLint gluProject(GLdouble objx, GLdouble objy, GLdouble objz,
           const GLdouble model[16], const GLdouble proj[16],
           const GLint viewport[4],
           GLdouble * winx, GLdouble * winy, GLdouble * winz)
{
   GLdouble in[4], out[4];

   in[0] = objx;
   in[1] = objy;
   in[2] = objz;
   in[3] = 1.0;
   transform_point(out, model, in);
   transform_point(in, proj, out);

   if (in[3] == 0.0)
      return GL_FALSE;

   in[0] /= in[3];
   in[1] /= in[3];
   in[2] /= in[3];

   *winx = viewport[0] + (1 + in[0]) * viewport[2] / 2;
   *winy = viewport[1] + (1 + in[1]) * viewport[3] / 2;

   *winz = (1 + in[2]) / 2;
   return GL_TRUE;
}

#endif

  // Based on Qt code
  void GLWidget::renderText(double x, double y, double z, const QString &str, const QFont &font, int)
  {
    //QOpenGLWidget::renderText(x,y,z,str,font, i);

#ifndef QT_OPENGL_ES
    if (str.isEmpty() || !isValid())
        return;


    int width = d->pd->width();
    int height = d->pd->height();
    qreal dpr = devicePixelRatioF();
    GLdouble model[4][4], proj[4][4];
    GLint view[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, &model[0][0]);
    glGetDoublev(GL_PROJECTION_MATRIX, &proj[0][0]);
    glGetIntegerv(GL_VIEWPORT, &view[0]);
    GLdouble win_x = 0, win_y = 0, win_z = 0;
    gluProject(x, y, z, &model[0][0], &proj[0][0], &view[0],
                &win_x, &win_y, &win_z);
    win_y = height * dpr - win_y; // y is inverted in device pixels

    //QPaintEngine::Type oldEngineType = qgl_engine_selector()->preferredPaintEngine();
    //QPaintEngine::Type oldEngineType = QGL::preferredPaintEngine();
    QPaintEngine *engine = paintEngine();

    QPainter *p;
    bool reuse_painter = false;
    bool use_depth_testing = glIsEnabled(GL_DEPTH_TEST);
    bool use_scissor_testing = glIsEnabled(GL_SCISSOR_TEST);

    if (engine->isActive()) {
        reuse_painter = true;
        p = engine->painter();
        save_gl_state();
    } else {
        // disable glClear() as a result of QPainter::begin()
        //d->disable_clear_on_painter_begin = true;
        p = new QPainter(this);
    }

    QRect viewport(view[0], view[1], view[2], view[3]);
    if (!use_scissor_testing && viewport != rect()) {
        glScissor(view[0], view[1], view[2], view[3]);
        glEnable(GL_SCISSOR_TEST);
    } else if (use_scissor_testing) {
        glEnable(GL_SCISSOR_TEST);
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Match the viewport to the framebuffer's physical dimensions
    int physWidth = static_cast<int>(width * dpr);
    int physHeight = static_cast<int>(height * dpr);
    glViewport(0, 0, physWidth, physHeight);
    glOrtho(0, physWidth, physHeight, 0, 0, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glAlphaFunc(GL_GREATER, 0.0);
    glEnable(GL_ALPHA_TEST);
    if (use_depth_testing)
        glEnable(GL_DEPTH_TEST);
    glTranslated(0, 0, -win_z);

    gl_draw_text(p, qRound(win_x), qRound(win_y), str, font, 10.0/camera()->distance(Vector3d(0,0,0)));

    if (reuse_painter) {
        restore_gl_state();
    } else {
        p->end();
        delete p;
      //  d->disable_clear_on_painter_begin = false;
    }
    //qgl_engine_selector()->setPreferredPaintEngine(oldEngineType);
#else // QT_OPENGL_ES
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(z);
    Q_UNUSED(str);
    Q_UNUSED(font);
    qWarning("QOpenGLWidget::renderText is not supported under OpenGL/ES");
#endif
  }
}
