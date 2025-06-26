/**********************************************************************
  XtbOptTool - Automatic Optimization Tool for Avogadro

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

#ifndef XTBOPTTOOL_H
#define XTBOPTTOOL_H

#include <avogadro/glwidget.h>
#include <avogadro/tool.h>
#include <avogadro/molecule.h>

#include <xtb.h>
#include <Eigen/Core>

#include <QtCore/QMutex>
#include <QtCore/QThread>
#include <QtCore/QSettings>
#include <QtWidgets/QAction>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QUndoStack>
#include <QtWidgets/QProgressBar>

namespace Avogadro {

  class XtbOptThread : public QThread
  {
    Q_OBJECT

    public:
      XtbOptThread(QObject *parent=0);
      ~XtbOptThread();

      bool setup(Molecule *molecule, int algorithm, int steps);

      void run();
      void update();
      const std::vector<double>& coords() const { return m_coords; }
      int natoms() const { return static_cast<int>(m_numbers.size()); }
      QMutex& mutex() { return m_mutex; }
      void cleanup();

    Q_SIGNALS:
      void finished(bool calculated);
      void setupDone();
      void setupFailed();
      void setupSucces();
      void progress(int step, int total, double energy);

    public Q_SLOTS:
      void stop();

    private:
      Molecule *m_molecule;
      xtb_TEnvironment m_env;
      xtb_TMolecule m_xtbMol;
      xtb_TCalculator m_calc;
      xtb_TResults m_results;
      std::vector<int> m_numbers;
      std::vector<double> m_coords;
      bool m_velocities;
      int m_algorithm;
      //double m_convergence;
      int m_steps;
      bool m_stop;
      QMutex m_mutex;
  };

  /**
   * @class XtbOptTool
   * @brief Automatic Optimization Tool
   * @author Marcus D. Hanwell
   *
   * This tool enables the manipulation of the position of
   * the selected atoms while the optimiser is running.
   */
  class XtbOptTool : public Tool
  {
    Q_OBJECT
      AVOGADRO_TOOL("XtbOptimization", tr("XtbOptimization"),
                    tr("Automatic optimization of molecular geometry"),
                    tr("XtbOptimization Settings"))

    public:
      //! Constructor
      XtbOptTool(QObject *parent = 0);
      //! Deconstructor
      virtual ~XtbOptTool();

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
      void threadFinished();
      void updateProgress(int step, int total, double energy);

    protected:
      GLWidget *                m_glwidget;
      Atom *                    m_clickedAtom;
      bool                      m_running;
      bool                      m_setupFailed;
      QWidget*                  m_settingsWidget;
      XtbOptThread *            m_thread;

      QSpinBox*                 m_stepsSpinBox;
      QPushButton*              m_buttonStartStop;
      QSpinBox*                 m_threadsSpinBox;
      QProgressBar*             m_progressBar;

      QPoint                    m_lastDraggingPosition;
      double                    m_lastEnergy;
      double                    m_deltaEnergy;


      void translate(GLWidget *widget, const Eigen::Vector3d &what, const QPoint &from, const QPoint &to) const;

    private Q_SLOTS:
      void settingsWidgetDestroyed();
  };

  class XtbOptCommand : public QUndoCommand
  {
    public:
      XtbOptCommand(Molecule *molecule, XtbOptTool *tool, QUndoCommand *parent = 0);

      void redo();
      void undo();
      bool mergeWith ( const QUndoCommand * command );
      int id() const;

    private:
      Molecule m_moleculeCopy;
      Molecule *m_molecule;
      XtbOptTool *m_tool;
      bool undone;
  };

  class XtbOptToolFactory : public QObject, public PluginFactory
  {
    Q_OBJECT
    Q_INTERFACES(Avogadro::PluginFactory)
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
    AVOGADRO_TOOL_FACTORY(XtbOptTool)
  };

} // end namespace Avogadro

#endif
