/**********************************************************************
  AutoOptTool - Automatic Optimization Tool for Avogadro

  Copyright (C) 2007,2008 by Marcus D. Hanwell
  Copyright (C) 2007 by Geoffrey R. Hutchison
  Copyright (C) 2007 by Benoit Jacob
  Copyright (C) 2008 by Tim Vandermeersch

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Some code is based on Open Babel
  For more information, see <http://openbabel.sourceforge.net/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#ifndef AUTOOPTTOOL_H
#define AUTOOPTTOOL_H

#include <avogadro/glwidget.h>
#include <avogadro/tool.h>
#include <avogadro/molecule.h>

#include "xtb/xtbengine.h"

#include <openbabel/mol.h>
#include <openbabel/forcefield.h>

#include <climits>
#include <QMutex>
#include <QThread>
#include <QAction>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QUndoStack>

namespace Avogadro {
  enum AutoOptAlgorithm
  {
    AutoOptSteepestDescent = 0,
    AutoOptConjugateGradients = 1,
    AutoOptBFGS = 2,
    AutoOptLBFGS = 3,
    AutoOptMolecularDynamics300K = 4,
    AutoOptMolecularDynamics600K = 5,
    AutoOptMolecularDynamics900K = 6
  };

  // Computational backend selected for a session. Force field keeps the
  // existing OpenBabel path; the xTB entries route to the xTB engine (task
  // 4.3). Index order is part of the persisted/UI contract. Named
  // AutoOptEngine to avoid colliding with the Avogadro::Engine class.
  enum AutoOptEngine
  {
    ForceField = 0,
    Gfn1 = 1,
    Gfn2 = 2,
    GfnFf = 3
  };

  class AutoOptThread : public QThread
  {
    Q_OBJECT

    public:
      AutoOptThread(QObject *parent=0);  // m_molecule/m_forceField initialised in the .cpp

      void setup(Molecule *molecule, OpenBabel::OBForceField* forceField,
                 int algorithm, /* int convergence, */ int steps,
                 bool newSession = true);

      void run();

      // Immutable snapshot of the force-field state the GUI thread needs to
      // report (energy, fixed/ignored atoms, drag markers). The worker thread
      // publishes a fresh copy at the end of every update() under
      // m_snapshotMutex; the GUI thread reads only through snapshot().
      struct ConstraintSnapshot {
        OpenBabel::OBBitVec fixed, ignored;
        unsigned int fixAtom = 0, ignoreAtom = 0;
        double energyKJ = 0.0;   // already unit-converted
        bool valid = false;
      };

      // Snapshot accessor: copy the current snapshot under the mutex. ALL
      // snapshot reads MUST go through this; direct m_snapshot access outside
      // the mutex is forbidden.
      ConstraintSnapshot snapshot() const {
        QMutexLocker locker(&m_snapshotMutex);
        return m_snapshot;
      }

      // Force field the optimizer currently runs on, read under m_mutex. The
      // GUI thread (finished()) uses this rather than its own cached copy so it
      // always reads the instance the worker actually optimized with, even
      // after a mid-run force-field switch (task 4.2). Pointer-copy hold time
      // is bounded by the mutex lock (consistent with the thread-confined
      // force-field requirement).
      OpenBabel::OBForceField *forceField() const {
        QMutexLocker locker(&m_mutex);
        return m_forceField;
      }

    public Q_SLOTS:
      void update();
      void stop();
      // Drag-path marker: the GUI thread records the atom it wants fixed while
      // dragging; the worker applies it to the live force field on the next
      // tick (worker is the only thread that touches the live FF). Stored under
      // m_pendingFixMutex, independent of m_mutex, so a drag press/release does
      // not block on the worker's per-tick optimization work.
      void setPendingFixAtom(unsigned idx);

    Q_SIGNALS:
      void finished(bool calculated);
      void setupDone();
      void setupFailed();
      void setupSucces();


    private:
      // Drag-path fix sentinel (no pending marker).
      static constexpr unsigned int kNoPendingFix = UINT_MAX;

      Molecule *m_molecule;
      OpenBabel::OBForceField * m_forceField;
      bool m_velocities;
      int m_algorithm;
      //double m_convergence;
      int m_steps;
      bool m_stop;
      // Guards the optimizer state below. mutable because forceField() reads
      // this mutex from a const method; a non-mutable QMutex yields a const
      // QMutex* in a const method, which MSVC rejects for QMutexLocker
      // (error C2662: cannot convert 'this' from 'const QMutex'). The two other
      // state-guarding mutexes here are mutable for the same reason. Locking
      // semantics are unchanged.
      mutable QMutex m_mutex;
      // Guards m_snapshot (published by update(), read by snapshot()).
      mutable QMutex m_snapshotMutex;
      ConstraintSnapshot m_snapshot;
      // Pending drag fix marker applied by the worker on the next tick.
      unsigned int m_pendingFixAtom;
      // Guards m_pendingFixAtom, decoupled from m_mutex (see setPendingFixAtom).
      mutable QMutex m_pendingFixMutex;
  };

  /**
   * @class AutoOptTool
   * @brief Automatic Optimization Tool
   * @author Marcus D. Hanwell
   *
   * This tool enables the manipulation of the position of
   * the selected atoms while the optimiser is running.
   */
  class AutoOptTool : public Tool
  {
    Q_OBJECT
      AVOGADRO_TOOL("AutoOptimization", tr("AutoOptimization"),
                    tr("Automatic optimization of molecular geometry using "
                       "force fields or xTB methods (GFN1-xTB, GFN2-xTB, "
                       "GFN-FF). Drag atoms to pull them during optimization "
                       "and choose a convergence level (Crude…Extreme)."),
                    tr("AutoOptimization Settings"))

    public:
      //! Constructor
      AutoOptTool(QObject *parent = 0);
      //! Deconstructor
      virtual ~AutoOptTool();

      //! \name Tool Methods
      //@{
      //! \brief Callback methods for ui.actions on the canvas.
      /*!
      */
      virtual QUndoCommand* mousePressEvent(GLWidget *widget, QMouseEvent *event);
      virtual QUndoCommand* mouseReleaseEvent(GLWidget *widget, QMouseEvent *event);
      virtual QUndoCommand* mouseMoveEvent(GLWidget *widget, QMouseEvent *event);
      virtual QUndoCommand* mouseDoubleClickEvent(GLWidget *widget, QMouseEvent *event);
      virtual QUndoCommand* wheelEvent(GLWidget *widget, QWheelEvent *event);

      virtual int usefulness() const;

      virtual bool paint(GLWidget *widget);

      virtual QWidget* settingsWidget();
      /**
       * Write the tool settings so that they can be saved between sessions.
       */
      virtual void writeSettings(QSettings &settings) const;

      /**
       * Read in the settings that have been saved for the tool instance.
       */
      virtual void readSettings(QSettings &settings);


    public Q_SLOTS:
      void finished(bool calculated);
      void setupDone();
      void setupFailed();
      void setupSucces();
      void toggle();
      void enable();
      void disable();
      void abort();

    protected:
      GLWidget *                m_glwidget;
      Atom *                    m_clickedAtom;
      bool                      m_leftButtonPressed;  // rotation
      bool                      m_midButtonPressed;   // scale / zoom
      bool                      m_rightButtonPressed; // translation
      bool                      m_running;
      bool                      m_block;
      bool                      m_setupFailed;
      int                       m_timerId;
      ToolGroup *               m_toolGroup;
      QWidget*                  m_settingsWidget;
      Eigen::Vector3d           m_selectedPrimitivesCenter;    // centroid of selected atoms
      OpenBabel::OBForceField*  m_forceField;
      AutoOptThread *           m_thread;
      // xTB session engine (task 4.3). Created in the constructor, quiesced and
      // deleted in the destructor; the engine owns its xTB objects on the
      // worker thread. Null while no xTB session is active.
      XtbEngine*                m_xtbEngine;
      int                       m_xtbTimerId;   // 100 ms pacing timer for xTB
      double                    m_xtbEnergyHartree;   // last reported energy (HUD)
      int                       m_xtbMethod;    // active xTB method (HUD name)
      QString                   m_xtbSetupFailedReason;   // failed-setup HUD text
      QString                   m_xtbEvalFailedReason;   // failed-evaluation HUD text
      bool                      m_xtbConverged;   // HUD: last xTB session converged

      std::vector<std::string>  m_forceFieldList;

      QComboBox*                m_comboFF;
      QComboBox*                m_comboAlgorithm;
      //QSpinBox*                 m_convergenceSpinBox;
      QSpinBox*                 m_stepsSpinBox;
      QComboBox*                m_comboEngine;
      QComboBox*                m_comboLevel;
      // Backend-specific labels: hidden together with their controls so the
      // panel does not show orphan captions after an engine switch.
      QLabel*                   m_labelFF;
      QLabel*                   m_labelAlg;
      QLabel*                   m_labelLevel;
      QLabel*                   m_labelThreads;
      // xTB worker-thread count (0 = auto). Shown only for an xTB backend.
      QSpinBox*                 m_threadsSpinBox;
      QPushButton*              m_buttonStartStop;
      QCheckBox*                m_fixedMovable;
      QCheckBox*                m_ignoredMovable;

      QPoint                    m_lastDraggingPosition;
      double                    m_lastEnergy;

      void timerEvent(QTimerEvent* event);

      void translate(GLWidget *widget, const Eigen::Vector3d &what, const QPoint &from, const QPoint &to) const;
      // Map a selected AutoOptEngine index to an xTB method id
      // (0=GFN1-xTB, 1=GFN2-xTB, 2=GFN-FF). Only meaningful for xTB engines.
      int engineToMethod(int engine) const;
      // True while an xTB session is live (started on an xTB backend). Drives
      // the drag-routing branches in the mouse handlers (task 4.4). m_xtbMethod
      // is the active method id (0/1/2) during an xTB session, -1 otherwise.
      bool xtbSessionActive() const;
      // Human-readable xTB method name for the HUD (0=GFN1-xTB, 1=GFN2-xTB,
      // 2=GFN-FF). Used by paint() for the xTB energy readout (task 4.5).
      QString xtbMethodName(int method) const;
      // Quiesce the active xTB session: stop the pacing timer, stop the engine,
      // and wait for its worker to finish the in-flight singlepoint. Shared by
      // abort() and disable() (task 4.3). The GUI thread never calls xtb_del*.
      void stopEngine();

    private Q_SLOTS:
      void settingsWidgetDestroyed();
      // React to a backend (engine) selection change: show/hide the FF-only
      // controls (force-field + algorithm combos) versus the xTB controls
      // (convergence-level combo). Steps-per-update is shared by both paths.
      void engineChanged();
      // Reflect xTB runtime availability on the Start button: when an xTB
      // backend is selected but the library is not resolvable, disable Start
      // with a diagnostic tooltip/message; otherwise restore the enabled state.
      // Force-field availability is left to the existing FF logic. Returns
      // true when an xTB backend is selected but the library is not
      // resolvable (Start is gated), false otherwise.
      bool updateXtbAvailability();
      // xTB session callbacks (task 4.3): report the last energy for the HUD,
      // record convergence, and surface a setup failure in the HUD path. The
      // coordinate snapshot is pulled off-engine via takeLatestCoords() in the
      // 100 ms timerEvent rather than through a cross-thread signal.
      void handleXtbProgress(int evaluation, int maxEvaluations, double energyHartree);
      void handleXtbConverged(double finalEnergyHartree);
      void handleXtbSetupFailed(const QString& reason);
      void handleXtbEvaluationFailed(const QString& reason);
      void handleXtbSessionReestablished(int atomCount);
  };

  class AutoOptCommand : public QUndoCommand
  {
    public:
      AutoOptCommand(Molecule *molecule, AutoOptTool *tool, QUndoCommand *parent = 0);

      void redo();
      void undo();
      bool mergeWith ( const QUndoCommand * command );
      int id() const;

    private:
      Molecule m_moleculeCopy;
      Molecule *m_molecule;
      AutoOptTool *m_tool;
      bool undone;
  };

  class AutoOptToolFactory : public QObject, public PluginFactory
  {
    Q_OBJECT
    Q_INTERFACES(Avogadro::PluginFactory)
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
    AVOGADRO_TOOL_FACTORY(AutoOptTool)
  };

} // end namespace Avogadro

#endif
