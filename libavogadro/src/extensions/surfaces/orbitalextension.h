/**********************************************************************
  OrbitalExtension - Molecular orbital explorer

  Copyright (C) 2010 David C. Lonie

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

#ifndef ORBITALEXTENSION_H
#define ORBITALEXTENSION_H

#include <avogadro/dockextension.h>

#include <QDockWidget>
#include <QCloseEvent>
#include <QVector>
#include <QMutex>
#include <QList>
#include <QTime>
#include <QFuture>
#include <atomic>

class QProgressDialog;

namespace OpenQube
{
  class BasisSet;
  class Cube;
}

namespace Avogadro
{
  class Cube;
  class Mesh;
  class MeshGenerator;
  class VdWSurface;
  class SurfaceDialog;
  class OrbitalWidget;

  enum CalcState {
    NotStarted = 0,
    Running,
    Completed,
    Canceled
  };

  struct calcInfo {
    Mesh *posMesh;
    Mesh *negMesh;
    Cube *cube;
    unsigned int orbital;
    double resolution;
    double isovalue;
    unsigned int priority;
    CalcState state;
  };

  class OrbitalDock : public QDockWidget
  {
  public:
    OrbitalDock(const QString &title, QWidget *parent = nullptr,
                Qt::WindowFlags flags = Qt::WindowFlags())
      : QDockWidget(title, parent, flags) {}

  protected:
    void closeEvent ( QCloseEvent * event )
    {
      if (widget())
        widget()->hide();
      event->accept();
    }
  };

  class OrbitalExtension : public DockExtension
  {
    Q_OBJECT
    AVOGADRO_EXTENSION("Orbitals", tr("Orbitals"),
                       tr("Calculate and visualize molecular orbitals"))

  public:
    // D4: the decision a renderOrbital click takes, based on the orbital's
    // queue state. Extracted into a pure, GLWidget-free helper so the
    // selection semantics are unit-testable (orbitalselectiontest).
    enum class SelectionPlan {
      RenderNow,          // orbital is already completed -> render immediately
      RenderWhenReady,    // orbital is queued or running -> mark pending, render on completion
      EnqueueThenRender   // orbital is neither completed nor queued -> enqueue, render on completion
    };

    // Pure decision helper (D4): completed -> RenderNow; queuedOrRunning ->
    // RenderWhenReady; neither -> EnqueueThenRender. No GLWidget dependency.
    static SelectionPlan planOrbitalSelection(bool completed, bool queuedOrRunning);

    OrbitalExtension(QObject* parent = 0);
    virtual ~OrbitalExtension();

    virtual QList<QAction *> actions() const;
    virtual QString menuPath(QAction* action) const;
    virtual QDockWidget * dockWidget();
    virtual void setMolecule(Molecule *molecule);

#ifdef AVOGADRO_TEST_ORBITAL_SELECTION
    // Test hooks (orbitalselectiontest, task 3.5): expose the D4
    // pending-orbital member and let the test set up the minimal state needed
    // to drive the real calculationComplete() completion path headlessly (the
    // full path needs a populated widget table and a live GLWidget). These are
    // public because the test calls them directly.
    unsigned int pendingOrbitalForTest() const { return m_pendingOrbital; }
    void setWidgetForTest(OrbitalWidget *w) { m_widget = w; }
    void setQueueForTest(const QList<calcInfo> &q) { m_queue = q; }
    void setUpCompletionForTest(const QList<calcInfo> &queue, int runningIndex,
                                unsigned int pendingOrbital) {
      m_queue = queue;
      m_currentRunningCalculation = runningIndex;
      m_runningGeneration = m_generation;
      m_pendingOrbital = pendingOrbital;
      // Balance the unlock in calculationComplete() (normally held by
      // checkQueue() from its tryLock until a calculation completes).
      m_runningMutex->lock();
    }
#endif

  private slots:
    /**
     * Load the appropriate basis set (if possible)
     */
    bool loadBasis();

    /**
     * Re-render an orbital at a higher resolution
     *
     * @param orbital The orbital to render
     * @param resolution The resolution of the cube
     */
    void calculateOrbitalFromWidget(unsigned int orbital,
                                    double resolution);

    /**
     * Calculate all molecular orbitals at low priority and low
     * resolution.
     */
    void precalculateOrbitals();

    /**
     * Add an orbital calculation to the queue. Lower priority values
     * run first. Do not set automatic calculations to priority zero,
     * this is reserved for user requested calculations and will run
     * first, displaying a progress dialog.
     *
     * @param orbital Orbital number
     * @param resolution Resolution of grid
     * @param isoval Isovalue for surface
     * @param priority Priority. Default = 0 (user requested)
     */
    void addCalculationToQueue(unsigned int orbital,
                               double resolution,
                               double isoval,
                               unsigned int priority = 0);
    /**
     * Check that no calculations are currently running and start the
     * highest priority calculation.
     */
    void checkQueue();

    /**
     * Start or resume the calculation at the indicated index of the
     * queue.
     */
    void startCalculation(unsigned int queueIndex);

    void calculateCube();
    void calculateCubeDone();
    void calculatePosMesh();
    /**
     * Resume after the OpenQube->Avogadro cube-data copy (D3). Called on the
     * GUI thread via a queued method invocation from the copy worker once the
     * grid-sized copy is done. Re-checks the generation epoch before handing
     * off to calculatePosMesh() so a document switch during the copy no-ops.
     */
    void continueAfterCubeCopy(Cube *cube);
    void calculatePosMeshDone();
    void calculateNegMesh();
    void calculateNegMeshDone();
    void calculationComplete();

    /**
     * Draw the indicated orbital on the GLWidget
     */
    void renderOrbital(unsigned int orbital);

    /**
     * Render the most recent completed calculation of the given orbital on the
     * surface engine (the D4 RenderNow path). Returns false (and does nothing)
     * if no completed calculation exists for that orbital or no surface engine
     * is available.
     */
    bool renderCompletedOrbital(unsigned int orbital);

    /**
     * Update the progress of the current calculation
     */
    void updateProgress(int current);

    /**
     * The D3 setMolecule tail, run on the GUI thread from the basis-load
     * continuation once the async parse produced a BasisSet and the epoch is
     * still current: enables/disables the dock, fills the orbital table
     * (HOMO/LUMO detection) and starts precalculation. Extracted from
     * setMolecule so the async basis-load continuation can drive it.
     */
    void finishBasisLoad();

  private:

    /**
     * D3 worker: parse a basis set from a file name + file-format tag and
     * return a new OpenQube::BasisSet* (or null). Touches no member state, so
     * it can run on a QtConcurrent worker thread; the caller owns the result.
     */
    static OpenQube::BasisSet *parseBasisFile(const QString &fileName,
                                              const QString &format);

    QDockWidget *m_dock;
    OrbitalWidget *m_widget;
    QMutex *m_runningMutex;

    QList<calcInfo> m_queue;
    int m_currentRunningCalculation;
    // N.1: bumped on each document switch. Atomic because worker threads (the
    // cube-copy worker, the ESP-color worker) read it without further
    // synchronization; relaxed is enough — the epoch compare is self-validating
    // (a mismatch makes the callback no-op).
    std::atomic<unsigned long> m_generation;
    unsigned long m_runningGeneration; // N.1: captured per launched calculation
    // D3: tracks the in-flight cube-data copy worker (QtConcurrent::run) so
    // setMolecule can quiesce it on a document switch before releasing the cube
    // it may still be writing into.
    QFuture<void> m_cubeCopyFuture;
    // D3: tracks the in-flight basis-set parse worker (QtConcurrent::run) so
    // setMolecule can quiesce it on a document switch before releasing m_basis.
    QFuture<void> m_basisLoadFuture;
    qint64 m_lastProgressEmit = 0;     // throttled progress emit timestamp
    MeshGenerator *m_meshGen;
    OpenQube::BasisSet *m_basis;
    QList<QAction *> m_actions;
    Molecule *m_molecule;
    OpenQube::Cube *m_qube;
    QTime m_time;
    // D4: the orbital the user selected while it was queued/running (0 = none).
    // Rendered automatically when its calculation completes; cleared on a
    // document switch so no stale surface renders after the switch.
    unsigned int m_pendingOrbital;
  };

  class OrbitalExtensionFactory : public QObject, public PluginFactory
  {
      Q_OBJECT
      Q_INTERFACES(Avogadro::PluginFactory)
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
      AVOGADRO_EXTENSION_FACTORY(OrbitalExtension)
  };

} // End namespace Avogadro

#endif
