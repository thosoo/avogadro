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

#include "autoopttool.h"
#include <avogadro/navigate.h>
#include <avogadro/primitive.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/glwidget.h>
#include <avogadro/undostack.h>
#include <avogadro/camera.h>
#include <avogadro/painter.h>
#include <avogadro/toolgroup.h>
#include <avogadro/primitivelist.h>

#include "xtb/xtbapi.h"

#include <openbabel/obiter.h>
#include <openbabel/mol.h>
#include <openbabel/obconversion.h>
#include <openbabel/generic.h>
#include <openbabel/elements.h>

#include <QDebug>
#include <QtPlugin>
#include <QLabel>
#include <QVBoxLayout>
#include <QWriteLocker>
#include <thread>
#include <vector>

using namespace std;
using namespace OpenBabel;
using namespace Eigen;

namespace Avogadro {

  AutoOptTool::AutoOptTool(QObject *parent) : Tool(parent), m_glwidget(0),
  m_clickedAtom(0),
  m_leftButtonPressed(false), m_midButtonPressed(false), m_rightButtonPressed(false),
  m_running(false), m_block(false), m_setupFailed(false), m_timerId(0), m_toolGroup(0),
  m_settingsWidget(0), m_selectedPrimitivesCenter(Eigen::Vector3d::Zero()),
  m_forceField(0), m_thread(0),
  m_xtbEngine(0), m_xtbTimerId(0), m_xtbEnergyHartree(0.0), m_xtbMethod(-1),
  m_xtbSetupFailedReason(), m_xtbConverged(false),
  m_forceFieldList(), m_comboFF(0), m_comboAlgorithm(0),
  m_stepsSpinBox(0), m_comboEngine(0), m_comboLevel(0), m_threadsSpinBox(0),
  m_labelFF(0), m_labelAlg(0), m_labelLevel(0), m_labelThreads(0),
  m_buttonStartStop(0), m_fixedMovable(0), m_ignoredMovable(0),
  m_lastDraggingPosition(), m_lastEnergy(0.0)
  {
    QAction *action = activateAction();
    action->setIcon(QIcon(QString::fromUtf8(":/autoopttool/autoopttool.png")));
    action->setToolTip(tr("Auto Optimization Tool\n\n"
          "Navigation Functions when clicking in empty space.\n"
          "Left Mouse: \tRotate Space\n"
          "Middle Mouse: Zoom Space\n"
          "Right Mouse: \tMove Space\n"
          "Double-Click: \t Reset View\n\n"
          "When running:\n"
          "Left Mouse: \tClick and drag atoms to move them."));
    // An OBConverison object must be instantiated before the
    // FindForceField call will work.
    OBConversion conv; Q_UNUSED(conv);
    m_forceField = OBForceField::FindForceField( "UFF" );
    if (!m_forceField)
      m_forceField = OBForceField::FindForceField( "UFF4MOF" );
    // Check that the force field exists and was initialised OK
    if (!m_forceField) {
      // We can't do anything is the force field cannot be found - OB issue
      emit setupFailed();
      return;
    }
    m_thread = new AutoOptThread;
    connect(m_thread, SIGNAL(finished(bool)), this, SLOT(finished(bool)));

    // xTB session engine (task 4.3). The engine resolves and owns the xTB
    // library objects on its own worker thread; this tool only drives it
    // (setup/tick/stop) and reads published snapshots. Created unconditionally;
    // it stays idle until an xTB backend is selected and started.
    m_xtbEngine = new XtbEngine(this);
    connect(m_xtbEngine, SIGNAL(progress(int,int,double)), this,
            SLOT(handleXtbProgress(int,int,double)));
    connect(m_xtbEngine, SIGNAL(converged(double)), this,
            SLOT(handleXtbConverged(double)));
    connect(m_xtbEngine, SIGNAL(setupFailed(QString)), this,
            SLOT(handleXtbSetupFailed(QString)));
    connect(m_xtbEngine, SIGNAL(evaluationFailed(QString)), this,
            SLOT(handleXtbEvaluationFailed(QString)));
    connect(m_xtbEngine, SIGNAL(sessionReestablished(int)), this,
            SLOT(handleXtbSessionReestablished(int)));
    connect(m_thread, SIGNAL(setupDone()),    this, SLOT(setupDone()));
    connect(m_thread, SIGNAL(setupFailed()),  this, SLOT(setupFailed()));
    connect(m_thread, SIGNAL(setupSucces()),  this, SLOT(setupSucces()));

    OBPlugin::ListAsVector("forcefields", "ids", m_forceFieldList);
    //action->setShortcut(Qt::Key_F10);
  }

  AutoOptTool::~AutoOptTool()
  {
    if (m_thread) {
      m_thread->exit();
      m_thread->wait();
      delete m_thread;
      m_thread = 0;
    }
    // Quiesce the xTB engine before destroying it (task 4.3). The engine
    // destructor also stop()+wait()s its worker, but quiescing here first
    // guarantees no in-flight singlepoint is left running when the tool
    // object goes away. The GUI thread never calls xtb_del* (the worker owns
    // deletion) — see xtbengine task 3.8.
    if (m_xtbEngine) {
      m_xtbEngine->stop();
      if (m_xtbEngine->isRunning())
        m_xtbEngine->wait();
      delete m_xtbEngine;
      m_xtbEngine = 0;
    }
    if (m_settingsWidget)
      m_settingsWidget->deleteLater();
  }

  int AutoOptTool::usefulness() const
  {
    return 10;
  }

  void AutoOptTool::translate(GLWidget *widget, const Eigen::Vector3d &what,
                              const QPoint &from, const QPoint &to) const
  {
    // Both from and to coordinates are already in device pixels
    // GLWidget scales mouse events, and camera()->project() returns device pixels
    Vector3d fromPos = widget->camera()->unProject(from, what);
    Vector3d toPos = widget->camera()->unProject(to, what);

    Vector3d atomTranslation = toPos - fromPos;

    // The xTB worker reads the live molecule under the molecule read lock each
    // tick (design D2). These setPos writes must be serialized with that read
    // or a torn (mixed old/new component) position can be observed, feeding
    // adoptExternalPositions a spurious external-edit signal mid-drag. Take the
    // molecule write lock for the duration of the drag writes. Guard the null
    // molecule (a view can be torn down while a drag is in flight).
    Molecule* mol = widget->molecule();
    if (mol == nullptr) {
      return;
    }
    QWriteLocker locker(mol->lock());

    if (widget->selectedPrimitives().size())
    {
      foreach(Primitive *p, widget->selectedPrimitives())
      {
        if (p->type() == Primitive::AtomType)
        {
          Atom *a = static_cast<Atom *>(p);
          a->setPos(atomTranslation + *a->pos());
          a->update();
        }
      }
    }

    if (m_clickedAtom)
    {
      m_clickedAtom->setPos(atomTranslation + *m_clickedAtom->pos());
      m_clickedAtom->update();
    }
  }

  QUndoCommand* AutoOptTool::mousePressEvent(GLWidget *widget,
                                             QMouseEvent *event)
  {
    m_glwidget = widget;
    m_lastDraggingPosition = event->position().toPoint();

    m_leftButtonPressed = (event->buttons() & Qt::LeftButton
        && event->modifiers() == Qt::NoModifier);
    // On the Mac, either use a three-button mouse
    // or hold down the Shift key
    m_midButtonPressed = ((event->buttons() & Qt::MiddleButton) ||
        (event->buttons() & Qt::LeftButton && event->modifiers()
         & Qt::ShiftModifier));
    // Hold down the Command key (ControlModifier in Qt notation) for right button
    m_rightButtonPressed = ((event->buttons() & Qt::RightButton) ||
        (event->buttons() & Qt::LeftButton &&
         (event->modifiers() == Qt::ControlModifier ||
          event->modifiers() == Qt::MetaModifier)));

    m_clickedAtom = widget->computeClickedAtom(event->position().toPoint());
    if(m_clickedAtom != 0 && m_leftButtonPressed && m_running)
    {
      event->accept();
      // Read the worker-published snapshot (GUI never calls into the live FF).
      const AutoOptThread::ConstraintSnapshot snap = m_thread->snapshot();
      const unsigned int idx = m_clickedAtom->index() + 1;
      if (snap.ignored.BitIsSet(idx) && !m_ignoredMovable->isChecked()) {
        m_clickedAtom = 0;
      }
      else if (snap.fixed.BitIsSet(idx) && !m_fixedMovable->isChecked()) {
        m_clickedAtom = 0;
      }

      // Route the drag fix marker through the worker (single FF caller).
      if (m_clickedAtom)
        m_thread->setPendingFixAtom(idx);
    }

    // xTB drag: pin the clicked atom to the engine (0-based index). The engine
    // masks it from optimizer write-back (task 3.5), so write-back never
    // regresses the dragged atom. No fix markers on the xTB path (FF-only).
    if (xtbSessionActive() && m_clickedAtom && m_leftButtonPressed)
      m_xtbEngine->setDraggedAtom(static_cast<int>(m_clickedAtom->index()));

    widget->update();
    return 0;
  }

  QUndoCommand* AutoOptTool::mouseReleaseEvent(GLWidget *widget, QMouseEvent *)
  {
    m_glwidget = widget;

    m_leftButtonPressed = false;
    m_midButtonPressed = false;
    m_rightButtonPressed = false;

    m_clickedAtom = 0;
    // Route the marker release through the worker (single FF caller).
    // Null-guard: with no force field the worker was never created, so there
    // is no marker to release (design D3).
    if (m_thread)
      m_thread->setPendingFixAtom(0);

    // xTB drag: release the dragged atom so the optimizer resumes covering it
    // from where the drag left it (task 4.4). No fix-marker release on xTB.
    if (xtbSessionActive())
      m_xtbEngine->setDraggedAtom(-1);

    widget->update();
    return 0;
  }

  QUndoCommand* AutoOptTool::mouseMoveEvent(GLWidget *widget,
                                            QMouseEvent *event)
  {
    m_glwidget = widget;
    if(!widget->molecule())
      return 0;

    // Get the currently selected atoms from the view
    PrimitiveList currentSelection = widget->selectedPrimitives();

    // Manipulation can be performed in two ways - centred on an individual atom

    if (m_clickedAtom && m_running) {
      if (m_leftButtonPressed) {
        event->accept();
        // translate the molecule following mouse movement  
        Vector3d begin = widget->camera()->project(*m_clickedAtom->pos());
        // Both camera()->project() and event->position().toPoint() are in device pixels (GLWidget scales mouse events)
        QPoint beginPos(static_cast<int>(begin.x()), static_cast<int>(begin.y()));
        translate(widget, *m_clickedAtom->pos(),
                  beginPos, event->position().toPoint());
      }
    }

    // xTB drag: the translate() call above already wrote the dragged atom's
    // position into the live molecule (pointer authority, design D3). The
    // worker reads the live molecule on its per-tick re-baseline, so no
    // separate position side channel is forwarded to the engine; the atom is
    // masked by index (setDraggedAtom) and its coordinate is adopted from the
    // molecule. Angstrom -> bohr conversion is handled inside the engine.

    m_lastDraggingPosition = event->position().toPoint();
    widget->update();

    return 0;
  }

  QUndoCommand* AutoOptTool::mouseDoubleClickEvent(GLWidget*, QMouseEvent*)
  {
    return 0;
  }

  QUndoCommand* AutoOptTool::wheelEvent(GLWidget*, QWheelEvent*)
  {
    return 0;
  }

  bool AutoOptTool::paint(GLWidget *widget)
  {
    // Force-field-less deployment: the worker was never created (the
    // constructor bailed on the force-field lookup), so there is no snapshot
    // to read and no HUD to draw. Return early instead of dereferencing a
    // null m_thread (design D3). With no force field the HUD block is
    // unreachable anyway (m_running can never become true), so this only
    // removes the crash.
    if (!m_thread)
      return false;
    QPoint labelPos(10, 10);
    QPoint debugPos(10, 50);
    glColor3f(1.0,1.0,1.0);
    // Read the worker-published snapshot once per frame. The GUI thread never
    // calls into the live force field while running; all reporting flows
    // through this immutable snapshot (see design decision 1).
    const AutoOptThread::ConstraintSnapshot snap = m_thread->snapshot();
    if (m_running) {
      if (xtbSessionActive()) {
        // xTB backend (task 4.5): the energy readout comes from the xTB
        // engine's published energy (Hartree), converted to kJ/mol. The FF
        // snapshot is not used on this backend.
        if (m_setupFailed) {
          // xTB setup failure: surface the engine's reason string (task 4.5).
          widget->painter()->drawText(labelPos,
            tr("AutoOpt: xTB setup failed: %1").arg(m_xtbSetupFailedReason));
        }
        else if (!m_xtbEvalFailedReason.isEmpty()) {
          // xTB singlepoint evaluation failure (design D4): the session is
          // alive but the phase is idle-with-failure; the engine's diagnostic
          // is surfaced instead of feeding garbage into the renderer.
          widget->painter()->drawText(labelPos,
            tr("AutoOpt: xTB evaluation failed: %1").arg(m_xtbEvalFailedReason));
        }
        else {
          double energyKJ = m_xtbEnergyHartree * 2625.499748;
          widget->molecule()->setEnergy(energyKJ);
          // Show the stable final energy with a converged marker once the
          // session has converged (or hit the evaluation cap) (task 4.5).
          QString suffix = m_xtbConverged ? tr(" (converged)") : QString();
          widget->painter()->drawText(labelPos,
            tr("xTB %1: E = %2 kJ/mol%3").arg(xtbMethodName(m_xtbMethod)).
            arg(energyKJ).arg(suffix));
          // Design D5: while an atom is dragged, report a bond whose length
          // exceeds bonding range, so the GUI/internals divergence (static
          // bond record vs. distance-based geometry) is visible as it
          // develops. Clears automatically at release or when the distance
          // returns within range (bondStrainText returns empty).
          if (m_clickedAtom && m_leftButtonPressed) {
            QString strain = bondStrainText(widget->molecule(), m_clickedAtom);
            if (!strain.isEmpty())
              widget->painter()->drawText(labelPos + QPoint(0, 20), strain);
          }
        }
      }
      else if (m_setupFailed) {
        widget->painter()->drawText(labelPos,
                                    tr("AutoOpt: Could not setup force field...."));
      }
      else if (snap.valid) {
        // Energy is the last tick's value (unit-converted by the worker).
        double energy = snap.energyKJ;
        widget->molecule()->setEnergy(energy);
        widget->painter()->drawText(labelPos,
            tr("AutoOpt: E = %1 %2 (dE = %3)").arg(energy).
            arg("kJ/mol").
            arg( fabs(m_lastEnergy - energy) ));
        // HUD: report the number of fixed and ignored atoms from the
        // snapshot (the transient drag markers counted too).
        unsigned int fixed = snap.fixed.CountBits();
        unsigned int ignored = snap.ignored.CountBits();
        if (snap.fixAtom)
          ++fixed;
        if (snap.ignoreAtom)
          ++ignored;
        widget->painter()->drawText(debugPos,
            tr("Fixed: %1 Ignored: %2").arg(fixed).arg(ignored));
        m_lastEnergy = energy;
      }
    }

    m_glwidget = widget;

    // Per-atom overlay halos while running: warm red/orange marks fixed (frozen)
    // atoms, blue marks ignored (dummy/halo) atoms. The atom being dragged
    // (m_clickedAtom) is skipped - the bright click halo below already marks it.
    if (m_running && m_glwidget && m_glwidget->molecule()) {
      Molecule *mol = m_glwidget->molecule();
      foreach(Atom *atom, mol->atoms()) {
        unsigned int idx = atom->index() + 1;
        // Test fixed/ignored via the snapshot, not the live force field.
        bool isFixed = snap.fixed.BitIsSet(idx) || snap.fixAtom == idx;
        bool isIgnored = snap.ignored.BitIsSet(idx) || snap.ignoreAtom == idx;
        if (!isFixed && !isIgnored)
          continue;
        if (m_clickedAtom != 0 && m_clickedAtom->index() == atom->index())
          continue;

        double renderRadius = widget->radius(atom);
        renderRadius += 0.10;
        glEnable(GL_BLEND);
        if (isFixed)
          widget->painter()->setColor(1.0, 0.5, 0.0, 0.4); // warm orange
        else
          widget->painter()->setColor(0.2, 0.4, 1.0, 0.4); // blue
        widget->painter()->drawSphere(atom->pos(), renderRadius);
        glDisable(GL_BLEND);
      }
    }

    if(m_leftButtonPressed) {
      if(m_running && m_clickedAtom) {
        // Don't highlight the atom on right mouse unless there is a selection
        double renderRadius = widget->radius(m_clickedAtom);
        renderRadius += 0.10;
        glEnable(GL_BLEND);
        widget->painter()->setColor(1.0, 0.3, 0.3, 0.7);
        widget->painter()->drawSphere(m_clickedAtom->pos(), renderRadius);
        glDisable(GL_BLEND);
      }
    }
    return true;
  }

  QWidget* AutoOptTool::settingsWidget()
  {
    if(!m_settingsWidget) {
      m_settingsWidget = new QWidget;

      // Backend (engine) selector sits above the force-field combo. It chooses
      // between the OpenBabel force-field path and one of the xTB methods.
      QLabel* labelEngine = new QLabel(tr("Engine:"));

      m_comboEngine = new QComboBox(m_settingsWidget);
      m_comboEngine->addItem(tr("Force field"));
      m_comboEngine->addItem(tr("xTB \u2014 GFN1-xTB"));
      m_comboEngine->addItem(tr("xTB \u2014 GFN2-xTB"));
      m_comboEngine->addItem(tr("xTB \u2014 GFN-FF"));
      m_comboEngine->setCurrentIndex(ForceField);

      QLabel* labelFF = new QLabel(tr("Force Field:"));
      m_labelFF = labelFF;

      m_comboFF = new QComboBox(m_settingsWidget);
      for (unsigned int i = 0; i < m_forceFieldList.size(); ++i)
        m_comboFF->addItem(m_forceFieldList[i].c_str());

      // Prefer UFF when available, then UFF4MOF.
      int currentFF = m_comboFF->findText("UFF");
      if (currentFF == -1)
        currentFF = m_comboFF->findText("UFF4MOF");
      if (currentFF != -1) // couldn't find it, go for index 0
        m_comboFF->setCurrentIndex(currentFF);

      // xTB convergence-level selector (Crude..Extreme). Lives in the grid
      // below; shown only for xTB backends via engineChanged().
      QLabel* labelLevel = new QLabel(tr("Convergence Level:"));
      m_labelLevel = labelLevel;

      m_comboLevel = new QComboBox(m_settingsWidget);
      m_comboLevel->addItem(tr("Crude\u2026"));
      m_comboLevel->addItem(tr("Sloppy"));
      m_comboLevel->addItem(tr("Loose"));
      m_comboLevel->addItem(tr("Lax"));
      m_comboLevel->addItem(tr("Normal"));
      m_comboLevel->addItem(tr("Tight"));
      m_comboLevel->addItem(tr("VTight"));
      m_comboLevel->addItem(tr("Extreme"));
      m_comboLevel->setCurrentIndex(4); // Normal

      QGridLayout* grid = new QGridLayout;
      grid->addWidget(labelEngine, 0, 0, Qt::AlignRight);
      grid->addWidget(m_comboEngine, 0, 1, Qt::AlignLeft);
      grid->addWidget(labelFF, 1, 0, Qt::AlignRight);
      grid->addWidget(m_comboFF, 1, 1, Qt::AlignLeft);

      QLabel* labelSteps = new QLabel(tr("Steps per Update:"));
      labelSteps->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

      m_stepsSpinBox = new QSpinBox(m_settingsWidget);
      m_stepsSpinBox->setMinimum(1);
      m_stepsSpinBox->setMaximum(100);
      m_stepsSpinBox->setValue(4);

      grid->addWidget(labelSteps, 2, 0, Qt::AlignRight);
      grid->addWidget(m_stepsSpinBox, 2, 1, Qt::AlignLeft);

      grid->addWidget(labelLevel, 3, 0, Qt::AlignRight);
      grid->addWidget(m_comboLevel, 3, 1, Qt::AlignLeft);

      // xTB worker-thread count. 0 means "auto" (the engine derives a budget
      // from the hardware). Shown only for an xTB backend via engineChanged().
      QLabel* labelThreads = new QLabel(tr("xTB Threads (0 = auto):"));
      labelThreads->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
      m_labelThreads = labelThreads;
      m_threadsSpinBox = new QSpinBox(m_settingsWidget);
      m_threadsSpinBox->setMinimum(0);
      m_threadsSpinBox->setMaximum(16);
      m_threadsSpinBox->setValue(0); // auto
      grid->addWidget(labelThreads, 4, 0, Qt::AlignRight);
      grid->addWidget(m_threadsSpinBox, 4, 1, Qt::AlignLeft);

      QLabel* labelAlg = new QLabel(tr("Algorithm:"));
      m_labelAlg = labelAlg;

      m_comboAlgorithm = new QComboBox(m_settingsWidget);
      m_comboAlgorithm->addItem(tr("Steepest Descent"));
      m_comboAlgorithm->addItem(tr("Conjugate Gradients"));
      m_comboAlgorithm->addItem(tr("BFGS"));
      m_comboAlgorithm->addItem(tr("L-BFGS"));
      m_comboAlgorithm->addItem(tr("Molecular Dynamics (300K)"));
      m_comboAlgorithm->addItem(tr("Molecular Dynamics (600K)"));
      m_comboAlgorithm->addItem(tr("Molecular Dynamics (900K)"));

      m_buttonStartStop = new QPushButton(tr("Start"), m_settingsWidget);

      m_fixedMovable = new QCheckBox(tr("Fixed atoms are movable"),
                                     m_settingsWidget);
      m_ignoredMovable = new QCheckBox(tr("Ignored atoms are movable"),
                                       m_settingsWidget);

      QVBoxLayout* layout = new QVBoxLayout();
      layout->addLayout(grid); // engine, force field, steps, level, threads

      layout->addWidget(labelAlg);
      layout->addWidget(m_comboAlgorithm);
      // m_comboLevel and m_threadsSpinBox are grid rows (3/4): adding them
      // here as well would put each widget under two layouts, which corrupts
      // the geometry on every engine switch.
      layout->addWidget(m_fixedMovable);
      layout->addWidget(m_ignoredMovable);
      layout->addWidget(m_buttonStartStop);
      layout->addStretch(1);
      m_settingsWidget->setLayout(layout);

      // Connect the start/stop button
      connect(m_buttonStartStop, SIGNAL(clicked()), this, SLOT(toggle()));

      // React to a backend selection change (show/hide FF-only vs xTB controls).
      connect(m_comboEngine, SIGNAL(currentIndexChanged(int)),
              this, SLOT(engineChanged()));

      connect(m_settingsWidget, SIGNAL(destroyed()),
              this, SLOT(settingsWidgetDestroyed()));

      // Establish the initial visibility (force-field path by default).
      engineChanged();

      // Check the force field is there, if not disable the start button
      if (!m_forceField)
        m_buttonStartStop->setEnabled(false);
    }

    return m_settingsWidget;
  }

  void AutoOptTool::settingsWidgetDestroyed()
  {
    m_settingsWidget = 0;
  }

  void AutoOptTool::engineChanged()
  {
    if (!m_comboEngine)
      return;

    // Force-field-only controls (force-field + algorithm combos and their
    // captions) are hidden for an xTB backend; the xTB convergence-level and
    // thread-count rows are shown only for an xTB backend. Labels toggle with
    // their controls so no orphan captions remain after a switch. Steps-per-
    // update is shared by both paths and stays visible.
    const bool xtb = m_comboEngine->currentIndex() != ForceField;

    if (m_comboFF)
      m_comboFF->setVisible(!xtb);
    if (m_labelFF)
      m_labelFF->setVisible(!xtb);
    if (m_comboAlgorithm)
      m_comboAlgorithm->setVisible(!xtb);
    if (m_labelAlg)
      m_labelAlg->setVisible(!xtb);
    if (m_comboLevel)
      m_comboLevel->setVisible(xtb);
    if (m_labelLevel)
      m_labelLevel->setVisible(xtb);
    if (m_threadsSpinBox)
      m_threadsSpinBox->setVisible(xtb);
    if (m_labelThreads)
      m_labelThreads->setVisible(xtb);

    // Reflect xTB runtime availability on the Start button.
    updateXtbAvailability();
  }

  bool AutoOptTool::updateXtbAvailability()
  {
    if (!m_comboEngine || !m_buttonStartStop)
      return false;

    const bool xtb = m_comboEngine->currentIndex() != ForceField;

    if (xtb && !XtbApi::isAvailable()) {
      // xTB backend selected but the library is not resolvable: gate the
      // Start button and surface a visible, logged diagnostic. Force fields
      // remain usable, so the message says so rather than implying a hard
      // failure. (D1/D10: runtime discovery, graceful degradation.)
      const QString msg = tr("xTB library not found \u2014 force fields remain available");
      m_buttonStartStop->setEnabled(false);
      m_buttonStartStop->setToolTip(msg);
      emit message(msg);
      return true;
    }

    // Force field, or xTB with the library present: restore the enabled
    // state. (Do not touch FF availability beyond clearing the xTB gate.)
    m_buttonStartStop->setEnabled(true);
    m_buttonStartStop->setToolTip(QString());
    return false;
  }

  int AutoOptTool::engineToMethod(int engine) const
  {
    // Map a selected AutoOptEngine index to an xTB method id used by
    // XtbEngine::setup (0=GFN1-xTB, 1=GFN2-xTB, 2=GFN-FF). The mapping matches
    // the AutoOptEngine enum order declared in the header.
    switch (engine) {
      case Gfn1: return 0;
      case Gfn2: return 1;
      case GfnFf: return 2;
      default: return 1; // GFN2-xTB is the default xTB backend
    }
  }

  bool AutoOptTool::xtbSessionActive() const
  {
    // An xTB session is live only while running on an xTB backend. m_xtbMethod
    // is the active method id (0/1/2) during such a session and -1 otherwise.
    return m_running && m_xtbMethod >= 0;
  }

  QString AutoOptTool::xtbMethodName(int method) const
  {
    // method id as stored in m_xtbMethod (0=GFN1-xTB, 1=GFN2-xTB, 2=GFN-FF).
    switch (method) {
      case 0: return tr("GFN1-xTB");
      case 1: return tr("GFN2-xTB");
      case 2: return tr("GFN-FF");
      default: return tr("xTB");
    }
  }

  QString AutoOptTool::bondStrainText(Molecule* mol, Atom* dragged) const
  {
    // Design D5: report when the dragged atom's distance to a bonded partner
    // exceeds 1.3x the sum of the covalent radii (the standard perception
    // convention used elsewhere in Avogadro), so the divergence between the
    // molecule's persistent bond record and the optimizer's distance-based
    // geometry is visible while it develops. Returns an empty string when no
    // bond is strained (or when there is no atom to inspect).
    if (!mol || !dragged || dragged->atomicNumber() <= 0)
      return QString();

    const Eigen::Vector3d* draggedPos = dragged->pos();
    if (!draggedPos)
      return QString();

    const double draggedRad =
      OpenBabel::OBElements::GetCovalentRad(dragged->atomicNumber());
    const QString draggedName =
      QString(OpenBabel::OBElements::GetSymbol(dragged->atomicNumber()));

    QStringList partnerNames;
    QList<unsigned long> neighbors = dragged->neighbors();
    for (unsigned long n : neighbors) {
      Atom* partner = mol->atom(static_cast<int>(n));
      if (!partner || partner->atomicNumber() <= 0)
        continue;
      const Eigen::Vector3d* ppos = partner->pos();
      if (!ppos)
        continue;
      const double partnerRad =
        OpenBabel::OBElements::GetCovalentRad(partner->atomicNumber());
      const double threshold = 1.3 * (draggedRad + partnerRad);
      const double dist = (*draggedPos - *ppos).norm();
      if (dist > threshold)
        partnerNames << QString(OpenBabel::OBElements::GetSymbol(
          partner->atomicNumber()));
    }

    if (partnerNames.isEmpty())
      return QString();

    // Name the dragged atom and the strained partner(s).
    return tr("Strained bond: %1 to %2").arg(draggedName)
      .arg(partnerNames.join(", "));
  }

  void AutoOptTool::handleXtbProgress(int, int, double energyHartree)
  {
    // Record the last energy for the HUD (task 4.5). Stored in Hartree; the
    // HUD converts to kJ/mol. Progress after the converged latch means a user
    // drag resumed the session: clear the suffix so the HUD reflects that the
    // optimizer is running again.
    m_xtbEnergyHartree = energyHartree;
    m_xtbConverged = false;
  }

  void AutoOptTool::handleXtbConverged(double finalEnergyHartree)
  {
    // Record the final energy once convergence (or the evaluation cap) is
    // reached. The session stays open; the worker simply stops evaluating.
    m_xtbEnergyHartree = finalEnergyHartree;
    // Flag convergence so the HUD shows the stable final energy with a
    // "(converged)" marker (task 4.5).
    m_xtbConverged = true;
  }

  void AutoOptTool::handleXtbSetupFailed(const QString& reason)
  {
    // Surface an xTB setup failure in the HUD path, reusing the FF path's
    // m_setupFailed flag. The engine's reason string is shown by paint().
    m_setupFailed = true;
    m_xtbSetupFailedReason = reason;
  }

  void AutoOptTool::handleXtbEvaluationFailed(const QString& reason)
  {
    // Surface an xTB singlepoint evaluation failure (design D4) in the HUD.
    // Distinct from setup failure: the session is alive but the current phase
    // is idle-with-failure. The reason text is shown by paint(); Stop/Start or
    // a topology edit (which re-establishes the session) clears it via
    // stopEngine() / the session-start reset in enable().
    m_xtbEvalFailedReason = reason;
  }

  void AutoOptTool::handleXtbSessionReestablished(int /*atomCount*/)
  {
    // A topology change re-established the xTB session (design D2): the new
    // phase evaluates normally, so a latched evaluation failure (design D4) no
    // longer applies and its HUD text is cleared. A previous setup failure is
    // also resolved by a successful re-establishment (e.g. the user edited an
    // unsupported element back to a supported one): clear the setup-failure
    // HUD text too, or paint() would keep reporting a stale failure while the
    // session optimizes.
    m_xtbEvalFailedReason.clear();
    m_setupFailed = false;
    m_xtbSetupFailedReason.clear();
  }

  void AutoOptTool::stopEngine()
  {
    // Quiesce the xTB engine (task 4.3). Stop the pacing timer first so no new
    // requestTick() is issued, then stop the worker and wait for the in-flight
    // singlepoint to finish. The engine destructor also quiesces it, but a
    // here quiesce guarantees no singlepoint runs while the tool tears down.
    if (m_xtbTimerId) {
      killTimer(m_xtbTimerId);
      m_xtbTimerId = 0;
    }
    if (m_xtbEngine) {
      m_xtbEngine->stop();
      if (m_xtbEngine->isRunning())
        m_xtbEngine->wait();
    }
    // Clear the active-backend marker so a stale session cannot be mistaken for
    // a live xTB one by the drag-routing branches after a stop.
    m_xtbMethod = -1;
    m_xtbEnergyHartree = 0.0;
    m_xtbConverged = false;
    // Stop/Start clears a latched evaluation failure (design D4 recovery path).
    m_xtbEvalFailedReason.clear();
  }

  void AutoOptTool::toggle()
  {
    // Toggle the timer on and off
    if (m_running)
      disable();
    else
      enable();
  }

  void AutoOptTool::enable()
  {
    // If the force field is false we have nothing and so should return
    if (!m_forceField)
      return;

    // Gate xTB sessions when the runtime library is not resolvable (D1/D10):
    // disable Start with a diagnostic and bail. Force-field availability is
    // left to the existing logic below; this does not change it.
    if (updateXtbAvailability())
      return;

    // The settings dock may not have been built yet (m_comboFF is created by
    // settingsWidget()), and the active GLWidget is only associated on the
    // first canvas interaction (m_glwidget). Without either, Start cannot be
    // honored yet; bail with a diagnostic instead of dereferencing a null or
    // garbage pointer (design D2). The user's next canvas interaction
    // associates the widget, after which Start works normally.
    if (!m_comboFF) {
      qWarning() << "AutoOptTool::enable: no force-field combo (settings widget not built); cannot start";
      return;
    }
    if (!m_glwidget) {
      qWarning() << "AutoOptTool::enable: no view associated yet (m_glwidget null); touch the canvas first";
      return;
    }

    if(!m_running) {
      const int engine = m_comboEngine ? m_comboEngine->currentIndex() : ForceField;
      if (engine != ForceField) {
        // xTB backend (task 4.3): build the session from the current molecule
        // and hand it to the xTB engine. The force-field path is untouched
        // below; this branch returns before it.
        Molecule *mol = m_glwidget->molecule();

        const int method = engineToMethod(engine);
        const int level = m_comboLevel ? m_comboLevel->currentIndex() : 4; // Normal
        const int threads = m_threadsSpinBox ? m_threadsSpinBox->value() : 0;

        // setupFromMolecule() records the live molecule as the engine's source
        // (design D2): the worker re-reads it each tick for live-molecule
        // re-baselining (topology change -> re-establish, external edit ->
        // fresh phase), and the engine's snapshot-aware writeBackToMolecule
        // becomes the SOLE molecule writer for the session. Using headless
        // setup() here would leave m_molecule null and disable that re-baseline
        // entirely, forcing the tool's timer write-back to be the only (and
        // unprotected) writer.
        //
        // setupFromMolecule() returns false (and emits setupFailed) when the
        // library is unavailable or the method cannot be loaded for this
        // molecule. In that case the HUD flag is set by handleXtbSetupFailed;
        // do NOT start a session or the pacing timer.
        if (!m_xtbEngine->setupFromMolecule(mol, method, level, threads)) {
          return;
        }

        // Session accepted: record the active backend for the HUD, start the
        // worker, and drive one tick per 100 ms from the timer below. Clear any
        // stale setup-failure flag from a prior session (the FF path clears it
        // via the setupDone signal; xTB has no such signal, so clear here).
        m_xtbMethod = method;
        m_xtbEnergyHartree = 0.0;
        m_xtbSetupFailedReason.clear();
        m_xtbEvalFailedReason.clear();
        m_setupFailed = false;
        m_running = true;
        m_xtbEngine->start(QThread::LowPriority);
        m_xtbTimerId = startTimer(100);
        m_buttonStartStop->setText(tr("Stop"));
        return;
      }

      // Re-find the force field by the current combo selection (same lookup as
      // timerEvent) so a user-changed force field is honoured on start.
      m_forceField = OBForceField::FindForceField(
                      m_forceFieldList[m_comboFF->currentIndex()]);
      if (!m_forceField) {
        emit setupFailed();
        return;
      }

      // Clear any stale per-instance constraint state left by a previous tool
      // session (drag markers, dummy-atom ignores) so a fresh run starts clean
      // instead of inheriting frozen/ignored atoms from an earlier tool.
      m_forceField->GetConstraints().Clear();
      m_forceField->UnsetFixAtom();
      m_forceField->UnsetIgnoreAtom();

      connect(m_glwidget->molecule(), SIGNAL(destroyed()), this, SLOT(abort()),
              Qt::UniqueConnection);
      m_thread->setup(m_glwidget->molecule(), m_forceField,
                      m_comboAlgorithm->currentIndex(),
                      m_stepsSpinBox->value());
      m_thread->start();
      m_running = true;
      m_buttonStartStop->setText(tr("Stop"));
      UndoStack *stack = m_glwidget->undoStack();
      AutoOptCommand *cmd = new AutoOptCommand(m_glwidget->molecule(),this,0);
      if(stack && cmd)
        stack->push(cmd);
      else
        delete cmd;
    }
  }

  void AutoOptTool::abort()
  {
    // Quiesce the worker BEFORE touching shared force-field state: stop
    // requesting ticks and wait for the in-flight tick to finish. Because
    // molecule destroyed -> abort(), the worker is quiesced before the GUI
    // clears constraint state below. (A molecule destroyed mid-tick is still
    // handled by the worker-side molecule read lock + null-check in update();
    // a pre-~Molecule quiesce would be a separate lifecycle fix.)
    m_running = false;
    killTimer(m_timerId);
    m_timerId = 0;
    m_thread->stop();
    m_thread->quit();
    if (m_thread->isRunning())
      m_thread->wait();
    // Quiesce the xTB engine too, if an xTB session is active (task 4.3).
    stopEngine();
    // Clear transient constraint markers so no stale fix/ignore survives a
    // hard stop (e.g. molecule destroyed). Null-check: the force field may be
    // reset before teardown.
    if (m_forceField) {
      m_forceField->UnsetIgnoreAtom();
      m_forceField->GetConstraints().Clear();
    }
  }

  void AutoOptTool::disable()
  {
    if(m_running) {
      m_running = false;
      m_setupFailed = false;
      killTimer(m_timerId);
      m_timerId = 0;
      // Quiesce the worker before mutating shared force-field state (see
      // design decision 2): stop requesting ticks and wait for the in-flight
      // tick to finish, so teardown cannot race an in-flight update().
      m_thread->stop();
      m_thread->quit();
      if (m_thread->isRunning())
        m_thread->wait();
      // Quiesce the xTB engine, if an xTB session is active (task 4.3).
      stopEngine();

      m_buttonStartStop->setText(tr("Start"));

      if (m_glwidget)
        m_glwidget->update(); // redraw AutoOpt label

      m_clickedAtom = 0;
      m_forceField->UnsetFixAtom();
      m_forceField->UnsetIgnoreAtom();
      m_forceField->GetConstraints().Clear();
      m_leftButtonPressed = false;
      m_midButtonPressed = false;
      m_rightButtonPressed = false;
    }
  }

  void AutoOptTool::timerEvent(QTimerEvent* event)
  {
    // xTB pacing timer (task 4.3): one L-BFGS iteration per 100 ms, applied via
    // the engine's published coordinate snapshot. Dispatch on the timer id
    // before the FF path, which runs on m_timerId.
    if (event->timerId() == m_xtbTimerId) {
      if (!m_xtbEngine || !m_running || m_xtbMethod < 0) {
        // Session ended or never started; drop the tick.
        return;
      }
      if (!m_glwidget || m_glwidget->molecule()->numAtoms() < 2)
        return;

      // Ask the worker to advance one L-BFGS iteration. takeLatestCoords
      // returns false when nothing new is published (e.g. the session converged
      // and stopped), so we simply wait for the next tick.
      //
      // The engine is the SOLE molecule writer for an xTB session (single-writer
      // architecture, task 4.1): its snapshot-aware writeBackToMolecule writes
      // the optimizer's working coords back to the live molecule on the worker
      // thread, preserving any atom changed externally since the read. The tool
      // must NOT write the published snapshot here — that write happens ~100 ms
      // after the worker read, lands in a window where an external edit (undo,
      // another tool) can be clobbered before the next tick adopts it, and is
      // the dominant clobber path in the GUI. Keep this tick as a repaint pump
      // only: advance the engine, consume the snapshot for freshness, and
      // request a repaint; the engine's write-back already moved the molecule.
      m_xtbEngine->requestTick();
      std::vector<double> coordsBohr;
      if (!m_xtbEngine->takeLatestCoords(coordsBohr))
        return;

      m_glwidget->molecule()->update();
      m_glwidget->update();
      return;
    }

    // Guard against a null view: timerEvent can only run while a session is
    // active, but a view is only associated after the first canvas
    // interaction. Removing that ordering assumption costs nothing (design D2).
    if (!m_glwidget) {
      qWarning() << "AutoOptTool::timerEvent: no view associated (m_glwidget null)";
      return;
    }
    // The settings dock may not have been built yet (m_comboFF created by
    // settingsWidget()); bail with a diagnostic instead of dereferencing it.
    // Checked before m_block is set so a bailed tick cannot wedge the session
    // (m_block stays false and the next tick retries).
    if (!m_comboFF) {
      qWarning() << "AutoOptTool::timerEvent: no force-field combo (settings widget not built)";
      return;
    }
    if(m_block || m_glwidget->molecule()->numAtoms() < 2)
      return;
    else
      m_block = true;

    // Resolve the selected force field into a local (task 4.2): the previous
    // per-tick write to the shared m_forceField member ran outside the worker
    // mutex. The worker reads its own force field under m_mutex, and finished()
    // now reads it through AutoOptThread::forceField(), so dropping the member
    // write removes the unsynchronized write without changing behavior. The
    // field is still resolved every tick, so a mid-run combo change takes
    // effect on the next tick.
    OBForceField *ff =
        OBForceField::FindForceField(m_forceFieldList[m_comboFF->currentIndex()]);

    // Check that we can find our force field - if not return
    if (!ff) {
      emit setupFailed();
      return;
    }

    // newSession=false: this is a tick, not a fresh start, so setup() must not
    // reset m_stop. A tick that previously failed Setup (m_stop=true) stays
    // failed instead of being resurrected into a Setup livelock (task 2.2).
    m_thread->setup(m_glwidget->molecule(), ff,
                    m_comboAlgorithm->currentIndex(),
                    m_stepsSpinBox->value(),
                    /*newSession=*/false);
    QMetaObject::invokeMethod(m_thread, "update",
                              Qt::QueuedConnection);
  }

  void AutoOptTool::finished(bool calculated)
  {
    if (m_running && calculated) {
      QList<Atom*> atoms = m_glwidget->molecule()->atoms();

      OBMol mol = m_glwidget->molecule()->OBMol();
      // Documented safe: m_block + queued finished() delivery strictly
      // serialize update() <-> finished(); the next update() cannot be queued
      // until this finished() clears m_block. Do not restructure.
      // Read the force field through the worker (task 4.2): the instance the
      // optimizer actually ran on, so a mid-run force-field switch cannot
      // leave finished() reading coordinates from a stale field. Null-guarded:
      // a quiesced teardown may have left the worker without a force field.
      OBForceField *ff = m_thread->forceField();
      if (ff) {
        ff->GetCoordinates( mol );
        // forces
        if (mol.HasData(OBGenericDataType::ConformerData)) {
          OBConformerData *cd = (OBConformerData*) mol.GetData(OBGenericDataType::ConformerData);
          const vector<vector<vector3> > &allForces = cd->GetForces();
          if (allForces.size()) {
            const vector<vector3> &forces = allForces[0];
            if (forces.size() == mol.NumAtoms()) {
              foreach(Atom* atom, atoms) {
                atom->setForceVector(Eigen::Vector3d(forces[atom->index()].AsArray()));
              }
            }
          }
        }
        // coordinates
        double *coordPtr = mol.GetCoordinates();
        foreach(Atom* atom, atoms) {
          // Dragged-atom write-back skip (fix-modern-tier-revision-churn
          // task 2.1): while the left button is still down the atom under the
          // drag owns its position from the drag path, not from this tick's
          // snapshot. The force field never moved it (it is fix-markered), so
          // its coordinate here is up to one tick stale and pre-drag; writing
          // it back would teleport the atom backward before translate() below
          // re-applies the cursor delta from that stale anchor (the reported
          // "mouse is at the new location but the atom gets there much later").
          // Skip the write for it this tick; translate() still re-anchors it.
          // On release the skip clears and this atom is covered again.
          // coordPtr MUST still advance by 3 for every atom (including the
          // skipped one) so the coordinate array stays aligned: atom i maps to
          // coordPtr + 3*i, so consuming the dragged atom's slot without
          // writing it keeps every later atom on its own coordinates.
          if (!(m_clickedAtom && m_leftButtonPressed && atom == m_clickedAtom))
            atom->setPos(Eigen::Vector3d(coordPtr));
          coordPtr += 3;
        }
      }

      if(m_clickedAtom && m_leftButtonPressed) {
        Vector3d begin = m_glwidget->camera()->project(*m_clickedAtom->pos());
        // Both camera()->project() and m_lastDraggingPosition are in device pixels
        QPoint beginPos(static_cast<int>(begin.x()), static_cast<int>(begin.y()));
        translate(m_glwidget, *m_clickedAtom->pos(), 
                  beginPos,
                  m_lastDraggingPosition);
      }
    }

    m_glwidget->molecule()->update();
    m_glwidget->update();
    m_block = false;
  }

  void AutoOptTool::setupDone()
  {
    if(!m_timerId)
      m_timerId = startTimer(50);
  }

  void AutoOptTool::setupFailed()
  {
    m_setupFailed = true;
  }

  void AutoOptTool::setupSucces()
  {
    m_setupFailed = false;
  }

  AutoOptThread::AutoOptThread(QObject*)
  {
    // Initialise the pointers so update()'s null-check is meaningful even for
    // a queued update() before any setup() has run (undefined otherwise).
    m_molecule = 0;
    m_forceField = 0;
    m_stop = false;
    m_velocities = false;
    m_pendingFixAtom = kNoPendingFix;
  }

  void AutoOptThread::setup(Molecule *molecule,
                            OpenBabel::OBForceField* forceField,
                            int algorithm, int steps,
                            bool newSession)
  {
    m_mutex.lock();
    m_molecule = molecule;
    m_forceField = forceField;
    m_algorithm = algorithm;
    m_steps = steps;
    // Resetting m_stop here is the "start a new session" semantics. A fresh
    // enable()/toggle() passes newSession=true (the default) and clears the
    // stop flag so the worker runs. The per-tick timerEvent path passes
    // newSession=false so a tick can never silently resurrect a session a
    // prior tick marked stopped (a Setup failure sets m_stop=true); without
    // this gate a failed session livelocks, retrying Setup at tick rate
    // forever (task 2.2). The 20x teardown/restart cycle in
    // autoOptConcurrencyRaces relies on setup() clearing m_stop, so the
    // default (true) keeps it green.
    if (newSession) {
      m_stop = false;
      m_velocities = false;
    }
    m_mutex.unlock();
    emit setupDone();
  }


  void AutoOptThread::run()
  {
    // Process queued update() calls in this thread
    moveToThread(this);
    exec();
  }

  void AutoOptThread::update()
  {
    // If the force field is false we have nothing and so should return
    if (!m_forceField)
      return;

    // A stop request skips a whole queued tick's cycle. Combined with the
    // quiesced teardown (disable/abort wait for the in-flight tick), this
    // bounds GUI blocking to a single tick on shutdown.
    if (m_stop)
      return;

    // Read the drag fix marker recorded by the GUI thread under its own mutex
    // (before taking m_mutex) so a drag press/release never blocks on the
    // worker's per-tick optimization work. The marker is applied below under
    // m_mutex, where the live force field is mutated.
    unsigned int pendingFix = kNoPendingFix;
    {
      QMutexLocker pl(&m_pendingFixMutex);
      pendingFix = m_pendingFixAtom;
      m_pendingFixAtom = kNoPendingFix;
    }

    m_mutex.lock();

    m_forceField->SetLogFile(NULL);
    m_forceField->SetLogLevel(OBFF_LOGLVL_NONE);

    // Guard against a dangling/empty molecule pointer. m_molecule can only
    // dangle after Molecule::destroyed() -> abort(); quiesced teardown makes
    // abort() wait for the in-flight tick, but the null-check keeps this
    // worker-side read safe on its own (the ctor zero-initialises it).
    if (!m_molecule) {
      m_mutex.unlock();
      return;
    }

    // Worker-side reads of the live molecule (OBMol() copy + dummy-atom
    // rescan) hold the molecule read lock so they cannot race GUI-thread
    // addAtom/removeAtom/setPos during draw strokes. The lock is released
    // BEFORE Setup() so the optimizer works only on the private copy.
    //
    // Acquisition is bounded (task 1.3 / design D4): instead of an
    // unbounded lockForRead, retry for a bounded number of spins and bail out
    // immediately if the session was quiesced (m_stop) or the retry budget is
    // exhausted. A GUI writer that now holds the write lock (the UndoStack
    // push serializes command application under it) must never wedge the
    // worker in a read-lock acquisition during quiesce, which would make
    // disable()/abort()'s wait() hang. The retry runs while holding m_mutex
    // (the worker's private mutex; nothing else contends on it), and m_stop
    // is checked each spin so a quiesce takes effect without waiting for the
    // writer to release.
    OBMol mol;
    {
      int spins = 0;
      while (!m_molecule->lock()->tryLockForRead()) {
        if (m_stop) {
          m_mutex.unlock();
          return;
        }
        if (++spins > 4096) {
          // Give up rather than spin forever; the next tick (or a fresh
          // enable) retries.
          m_mutex.unlock();
          return;
        }
        std::this_thread::yield();
      }
      // Local RAII read-lock holder. The constructor takes ownership of the
      // already-acquired lock WITHOUT re-acquiring (QReadLocker would deadlock:
      // QReadWriteLock is not re-entrant on the same thread). A named
      // constructor is used instead of aggregate init so this compiles on MSVC,
      // which rejects aggregate-init of a class with a user-provided destructor
      // (GCC/Clang tolerate it, but the Windows installer build is strict here).
      struct ReadLockGuard {
        explicit ReadLockGuard(QReadWriteLock *l) : lock(l) {}
        ~ReadLockGuard() { if (lock) lock->unlock(); }
        QReadWriteLock *lock;
      } readLockGuard(m_molecule->lock());

      mol = m_molecule->OBMol();

      // Reset transient ignore markers each tick (bounded state), then re-scan
      // for excluded/halo dummy atoms (atomicNumber < 1). This self-heals the
      // addAtom -> setAtomicNumber race within one tick instead of letting the
      // "Num Constraints" counter grow without bound.
      m_forceField->GetConstraints().Clear();
      foreach(const Atom *atom, m_molecule->atoms()) {
        if (atom->atomicNumber() < 1)
          m_forceField->GetConstraints().AddIgnore(atom->index() + 1);
      }
    } // read lock released here, before Setup (matches prior behavior)

    // Apply the drag-path fix marker (worker is the only thread that mutates
    // the live force field). The GUI read of the snapshot lags at most one
    // tick, matching the drag's existing one-tick latency.
    if (pendingFix != kNoPendingFix) {
      if (pendingFix == 0)
        m_forceField->UnsetFixAtom();
      else
        m_forceField->SetFixAtom(pendingFix);
    }

    if (!m_forceField->Setup(mol)) {
      m_stop = true;
      // Publish an invalid snapshot so the HUD degrades to hiding rather
      // than showing stale numbers after a setup failure.
      {
        QMutexLocker locker(&m_snapshotMutex);
        m_snapshot = ConstraintSnapshot();
        m_snapshot.valid = false;
      }
      emit setupFailed();
      emit finished(false);
      m_mutex.unlock();
      return;
    }
    else
      emit setupSucces();

    m_forceField->SetConformers(mol);

    switch(m_algorithm) {
      case AutoOptSteepestDescent:
        m_forceField->SteepestDescent(m_steps);
        break;
      case AutoOptConjugateGradients:
        m_forceField->ConjugateGradients(m_steps);
        break;
      case AutoOptBFGS:
        m_forceField->BFGSInitialize(m_steps, 1.0e-7);
        m_forceField->BFGSTakeNSteps(m_steps);
        break;
      case AutoOptLBFGS:
        m_forceField->LBFGSInitialize(m_steps, 1.0e-7,
                                      OBFF_ANALYTICAL_GRADIENT, 7);
        m_forceField->LBFGSTakeNSteps(m_steps);
        break;
      case AutoOptMolecularDynamics300K:
        m_forceField->MolecularDynamicsTakeNSteps(m_steps, 300, 0.001);
        break;
      case AutoOptMolecularDynamics600K:
        m_forceField->MolecularDynamicsTakeNSteps(m_steps, 600, 0.001);
        break;
      case AutoOptMolecularDynamics900K:
        m_forceField->MolecularDynamicsTakeNSteps(m_steps, 900, 0.001);
        break;
    }

    // Publish the constraint/energy snapshot for the GUI thread under the
    // dedicated snapshot mutex. Hold time is microseconds (small struct copy),
    // so 60 fps paints never wait on optimizer work. Must happen under
    // m_mutex so the snapshot is consistent, before unlocking.
    {
      ConstraintSnapshot snap;
      snap.fixed = m_forceField->GetConstraints().GetFixedBitVec();
      snap.ignored = m_forceField->GetConstraints().GetIgnoredBitVec();
      snap.fixAtom = m_forceField->GetFixAtom();
      snap.ignoreAtom = m_forceField->GetIgnoreAtom();
      double energy = m_forceField->Energy(false);
      if (m_forceField->GetUnit().find("kcal") != std::string::npos)
        energy *= KCAL_TO_KJ;
      snap.energyKJ = energy;
      snap.valid = true;
      QMutexLocker locker(&m_snapshotMutex);
      m_snapshot = snap;
    }

    m_mutex.unlock();

    emit finished(m_stop ? false : true);
  }

  void AutoOptThread::stop()
  {
    m_stop = true;
  }

  void AutoOptThread::setPendingFixAtom(unsigned idx)
  {
    // Store under the dedicated pending mutex, not m_mutex, so the GUI thread
    // never blocks on the worker's per-tick optimization work.
    QMutexLocker pl(&m_pendingFixMutex);
    m_pendingFixAtom = idx;
  }

  AutoOptCommand::AutoOptCommand(Molecule *molecule, AutoOptTool *tool,
                                 QUndoCommand *parent)
    : QUndoCommand(parent), m_molecule(0)
  {
    // Store the original molecule before any modifications are made
    setText(QObject::tr("AutoOpt Molecule"));
    m_moleculeCopy = *molecule;
    m_molecule = molecule;
    m_tool = tool;
  }

  void AutoOptCommand::redo()
  {
    m_moleculeCopy = *m_molecule;
  }

  void AutoOptCommand::undo()
  {
    if(m_tool)
      m_tool->disable();
    *m_molecule = m_moleculeCopy;
  }

  bool AutoOptCommand::mergeWith (const QUndoCommand *)
  {
    // Just return true to repeated calls - we have stored the original molecule
    return true;
  }

  int AutoOptCommand::id() const
  {
    return 1311387;
  }

  void AutoOptTool::writeSettings(QSettings &settings) const
  {
    Tool::writeSettings(settings);
    settings.setValue("engine", m_comboEngine ? m_comboEngine->currentIndex() : ForceField);
    settings.setValue("forceField", m_comboFF->currentIndex());
    settings.setValue("algorithm", m_comboAlgorithm->currentIndex());
    settings.setValue("xtbLevel", m_comboLevel ? m_comboLevel->currentIndex() : 4);
    settings.setValue("algorithmVersion", 2);
    settings.setValue("steps", m_stepsSpinBox->value());
    settings.setValue("threads", m_threadsSpinBox ? m_threadsSpinBox->value() : 0);
    settings.setValue("fixedMovable", m_fixedMovable->checkState());
    settings.setValue("ignoredMovable", m_ignoredMovable->checkState());
  }

  void AutoOptTool::readSettings(QSettings &settings)
  {
    Tool::readSettings(settings);
    if (m_comboEngine) {
      int currentEngine = settings.value("engine", ForceField).toInt();
      if (currentEngine < 0 || currentEngine > GfnFf)
        currentEngine = ForceField;
      m_comboEngine->setCurrentIndex(currentEngine);
      // Mirror the resulting control visibility.
      engineChanged();
    }
    // Restore the xTB convergence level (task 4.6: backend + level persist
    // across restarts). Default to Normal when unset.
    if (m_comboLevel) {
      int currentLevel = settings.value("xtbLevel", 4).toInt();
      if (currentLevel < 0 || currentLevel >= m_comboLevel->count())
        currentLevel = 4; // Normal
      m_comboLevel->setCurrentIndex(currentLevel);
    }
    if (m_comboFF) {
      int currentFF = settings.value("forceField", -1).toInt();
      if (currentFF == -1) {
        // haven't set a default
        // Prefer UFF when available, then UFF4MOF.
        currentFF = m_comboFF->findText("UFF");
        if (currentFF == -1)
          currentFF = m_comboFF->findText("UFF4MOF");
        if (currentFF == -1) // couldn't find it, go for index 0
          currentFF = 0;
      }
      m_comboFF->setCurrentIndex(currentFF);
    }
    if (m_comboAlgorithm) {
      int currentAlgorithm = settings.value("algorithm", AutoOptSteepestDescent).toInt();
      // Migrate saved values from older versions where MD options were indices 2-4.
      if (settings.value("algorithmVersion", 1).toInt() < 2 && currentAlgorithm >= 2)
        currentAlgorithm += 2;
      if (currentAlgorithm < 0 || currentAlgorithm >= m_comboAlgorithm->count())
        currentAlgorithm = AutoOptSteepestDescent;
      m_comboAlgorithm->setCurrentIndex(currentAlgorithm);
    }
    if (m_stepsSpinBox)
      m_stepsSpinBox->setValue(settings.value("steps", 4).toInt());
    if (m_threadsSpinBox)
      m_threadsSpinBox->setValue(settings.value("threads", 0).toInt());
    if (m_fixedMovable) {
      m_fixedMovable->setCheckState(
            static_cast<Qt::CheckState>(settings.value("fixedMovable", 2).toInt()));
    }
    if(m_ignoredMovable) {
      m_ignoredMovable->setCheckState(
            static_cast<Qt::CheckState>(settings.value("ignoredMovable", 2).toInt()));
    }
  }

}
