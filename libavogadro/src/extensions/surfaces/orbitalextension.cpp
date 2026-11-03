/**********************************************************************
  OrbitalExtension - Extension for visualizing molecular orbitals

  Copyright (C) 2010 David C. Lonie
  Copyright (C) 2009-2011 Marcus D. Hanwell

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.openmolecules.net/>
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

#include "orbitalextension.h"

#include "orbitalwidget.h"

#include <openqube/basissetloader.h>
#include <openqube/basisset.h>
#include <openqube/gaussianset.h>
#include <openqube/gamessukout.h>
#include <openqube/gamessus.h>
#include <openqube/cube.h>

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/cube.h>
#include <avogadro/mesh.h>
#include <avogadro/meshgenerator.h>
#include <avogadro/engine.h>
#include <avogadro/glwidget.h>

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QtConcurrent/QtConcurrentRun>
#include <QMessageBox>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <algorithm>

using OpenQube::BasisSet;
using OpenQube::GaussianSet;
using OpenQube::GamessukOut;
using OpenQube::GAMESSUSOutput;

namespace Avogadro
{

  OrbitalExtension::OrbitalExtension(QObject* parent) :
    DockExtension(parent),
    m_dock(0),
    m_widget(0),
    m_runningMutex(new QMutex),
    m_currentRunningCalculation(-1),
    m_generation(0),
    m_runningGeneration(0),
    m_meshGen(0),
    m_basis(0),
    m_molecule(0),
    m_qube(0),
    m_pendingOrbital(0)
  {
    QAction* action = new QAction(this);
    action->setText(tr("Molecular Orbitals..."));
    m_actions.append(action);
  }

  OrbitalExtension::~OrbitalExtension()
  {
    // Quiesce the in-flight basis-set parse worker so it cannot call
    // QMetaObject::invokeMethod(this, ...) on a destroyed extension. The
    // queued continuation is cancelled by Qt if this is destroyed before
    // delivery.
    if (m_basisLoadFuture.isRunning())
      m_basisLoadFuture.waitForFinished();
    if (m_cubeCopyFuture.isRunning())
      m_cubeCopyFuture.waitForFinished();
    delete m_runningMutex;
  }

  OrbitalExtension::SelectionPlan
  OrbitalExtension::planOrbitalSelection(bool completed, bool queuedOrRunning)
  {
    // D4: a completed orbital renders immediately; a queued/running one is
    // marked pending and rendered on completion; one that is neither is
    // enqueued (at the selected quality) and rendered on completion.
    if (completed)
      return SelectionPlan::RenderNow;
    if (queuedOrRunning)
      return SelectionPlan::RenderWhenReady;
    return SelectionPlan::EnqueueThenRender;
  }

  QList<QAction *> OrbitalExtension::actions() const
  {
    return m_actions;
  }

  QString OrbitalExtension::menuPath(QAction*) const
  {
    return tr("E&xtensions");
  }

  QDockWidget * OrbitalExtension::dockWidget()
  {
    if (!m_dock) {
//      m_dock = new OrbitalDock( tr("Orbitals"),
//                                qobject_cast<QWidget *>(parent()) );
      m_dock = new QDockWidget( tr("Orbitals"));
      m_dock->setObjectName("orbitalDock");
      if (!m_widget) {
        m_widget = new OrbitalWidget(m_dock);
        connect(m_widget, SIGNAL(orbitalSelected(unsigned int)),
                this, SLOT(renderOrbital(unsigned int)));
        connect(m_widget, SIGNAL(renderRequested(unsigned int, double)),
                this, SLOT(calculateOrbitalFromWidget(unsigned int, double)));
        connect(m_widget, SIGNAL(calculateAll()),
                this, SLOT(precalculateOrbitals()));
      }
    }

    m_dock->setWidget(m_widget);
    m_dock->setVisible(true);
    return m_dock;
  }

  void OrbitalExtension::setMolecule(Molecule *molecule)
  {
    // N.1: bump the generation counter first, before any teardown, so a
    // stale completion callback from the outgoing document can no longer be
    // mistaken for work of the incoming document.
    // fetch_add is atomic (m_generation is read on worker threads), so this is
    // not a data race. Using the default seq_cst ordering (not relaxed) gives a
    // happens-before with the teardown that immediately follows, closing the
    // formal gap: a worker that re-reads m_generation after the switch sees the
    // new epoch. The epoch compare is self-validating either way.
    m_generation.fetch_add(1);

    // N.2: quiesce background work owned by the outgoing document before
    // releasing its data. A cube calc runs on QtConcurrent (writes through
    // m_basis internals) and a mesh gen runs on its own QThread; both must
    // be disconnected and waited on before m_basis / the queue are destroyed.
    bool calcInFlight = (m_currentRunningCalculation != -1);

    if (m_basis)
      disconnect(&m_basis->watcher(), 0, this, 0);
    if (m_meshGen)
      m_meshGen->disconnect();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (m_basis && m_basis->watcher().isRunning())
      m_basis->watcher().future().waitForFinished();
    if (m_meshGen && m_meshGen->isRunning())
      m_meshGen->wait();
    // D3: quiesce the in-flight cube-data copy worker before releasing the
    // cube it may still be writing into. Its queued continuation
    // (continueAfterCubeCopy) re-checks the epoch and no-ops on a switch, so
    // this only needs to bound the copy's lifetime; the cube object is safe
    // because the copy completes before any teardown frees it.
    if (m_cubeCopyFuture.isRunning())
      m_cubeCopyFuture.waitForFinished();
    // D3: quiesce the in-flight basis-set parse worker before releasing
    // m_basis. Its queued continuation re-checks the epoch and discards the
    // stale result on a switch (and Qt cancels the queued call if the
    // extension is destroyed before delivery), so this only needs to bound the
    // parse's lifetime — the worker touches no member state.
    if (m_basisLoadFuture.isRunning())
      m_basisLoadFuture.waitForFinished();
    QApplication::restoreOverrideCursor();

    // N.1: the queue mutex is held from checkQueue() until
    // calculationComplete() unlocks it. Because we just disconnected the
    // completion signals, that unlock will never fire, so release the lock
    // here (exactly once) to avoid deadlocking the new document's queue.
    // Stale completion slots now no-op without touching m_runningMutex.
    if (calcInFlight)
      m_runningMutex->unlock();

    // 8.2 (reviewer NOTE 1): a calc that was still in flight leaves its
    // OpenQube cube write-locked — the unlock lives in the completion slot,
    // which will never be delivered now that the watcher connections are
    // gone. The GUI thread acquired that lock, so release it here exactly
    // once, then drop the cube: deleting a locked QReadWriteLock in
    // calculateCube() would be formal UB. (When no calc was in flight the
    // lock is not held and m_qube is either null or a fresh, unlocked cube
    // awaiting its calc — deleting it here is equally safe.)
    if (m_qube) {
      if (calcInFlight)
        m_qube->lock()->unlock();
      delete m_qube;
      m_qube = 0;
    }

    // Now safe to tear down: nothing references m_basis / m_queue.
    m_queue.clear();
    m_currentRunningCalculation = -1;
    m_runningGeneration = 0;
    // D4: clear any pending orbital so a surface from the outgoing document
    // cannot render after the switch (the epoch guard already discards stale
    // completions, but the pending marker must not survive either).
    m_pendingOrbital = 0;

    if (m_basis) {
      delete m_basis;
      m_basis = 0;
    }

    // Track the incoming document before probing it: loadBasis() reads
    // m_molecule->fileName(), and the first moleculeChanged fires before any
    // other code has assigned m_molecule (a null dereference at startup).
    m_molecule = molecule;
    if (!m_molecule)
      return;

    // D3: dispatch the basis parse off the GUI thread. The synchronous tail
    // (dock enable/disable, HOMO/LUMO table fill, precalculation) now runs on
    // the GUI thread from the async continuation via finishBasisLoad(), after
    // the epoch re-check.
    loadBasis();

    // FIXME: Add this feature back in, refactor ideally.
#if 0
    // Load Properties for QTAIM calculation
    if(m_basis)
    {

      // QTAIM is only implemented for Cartesian Gaussians.
      // 6D, 10F, 15G, etc...
      QString basisSetType = m_basis->basisSetType();
      QVariant basisSetTypeVariant(basisSetType);
      m_molecule->setProperty("QTAIMBasisSetType",basisSetTypeVariant);

      if( basisSetType == "Cartesian GTO")
      {

        qDebug() << "QTAIM: Expand Cartesian GTOs into Primitives";
        m_basis->expandIntoPrimitives();

        QString fileNameString(m_molecule->fileName());
        QVariant fileNameVariant(fileNameString);
        m_molecule->setProperty("QTAIMFileName",fileNameVariant);
        QString commentString("Wavefunction from Surfaces extension");
        QVariant commentVariant(commentString);
        m_molecule->setProperty("QTAIMComment",commentVariant);

        // only keep the occupied orbitals
        unsigned int totalNumberOfMolecularOrbitals( static_cast<qint64>(m_basis->numMOs()) );
        qint64 numberOfMolecularOrbitals=-1;
        for( unsigned int i=0 ; i < totalNumberOfMolecularOrbitals ; ++i)
        {
          if( m_basis->HOMO(i) )
            numberOfMolecularOrbitals=static_cast<qint64>(i) + 1;
        }
        QVariant numberOfMolecularOrbitalsVariant(numberOfMolecularOrbitals);
        m_molecule->setProperty("QTAIMNumberOfMolecularOrbitals",numberOfMolecularOrbitalsVariant);

        qint64 numberOfNuclei( static_cast<qint64>(m_molecule->numAtoms()) );
        QVariant numberOfNucleiVariant(numberOfNuclei);
        m_molecule->setProperty("QTAIMNumberOfNuclei",numberOfNucleiVariant);

        // Nuclear Coordinates
        QList<qreal> xcoordList;
        QList<qreal> ycoordList;
        QList<qreal> zcoordList;
        QList<qint64> nucZList;
        for( qint64 i=0 ; i < numberOfNuclei ; ++i )
        {
          static const double BOHR_TO_ANGSTROM = 0.529177249;
          static const double ANGSTROM_TO_BOHR = 1.0 / 0.529177249;
          Atom *atom=m_molecule->atoms().at(i);
          const Eigen::Vector3d pos=*(atom->pos());
          xcoordList.append(pos(0) * ANGSTROM_TO_BOHR );
          ycoordList.append(pos(1) * ANGSTROM_TO_BOHR );
          zcoordList.append(pos(2) * ANGSTROM_TO_BOHR );
          nucZList.append(atom->atomicNumber());
        }

        QVariantList xcoordVariantList;
        QVariantList ycoordVariantList;
        QVariantList zcoordVariantList;
        QVariantList nucZVariantList;
        for( qint64 i=0 ; i < xcoordList.length() ; ++i )
        {
          xcoordVariantList.append( xcoordList.at(i) );
          ycoordVariantList.append( ycoordList.at(i) );
          zcoordVariantList.append( zcoordList.at(i) );
          nucZVariantList.append( nucZList.at(i) );
        }
        m_molecule->setProperty("QTAIMXNuclearCoordinates",xcoordVariantList);
        m_molecule->setProperty("QTAIMYNuclearCoordinates",ycoordVariantList);
        m_molecule->setProperty("QTAIMZNuclearCoordinates",zcoordVariantList);
        m_molecule->setProperty("QTAIMNuclearCharges",nucZVariantList);

        // Primitive Centers
        QList<qreal> X0List( m_basis->X0List() );
        QVariantList X0VariantList;
        for( qint64 i=0 ; i < X0List.length() ; ++i )
        {
          X0VariantList.append( X0List.at(i) );
        }
        m_molecule->setProperty("QTAIMXGaussianPrimitiveCenterCoordinates",X0VariantList);

        QList<qreal> Y0List( m_basis->Y0List() );
        QVariantList Y0VariantList;
        for( qint64 i=0 ; i < Y0List.length() ; ++i )
        {
          Y0VariantList.append( Y0List.at(i) );
        }
        m_molecule->setProperty("QTAIMYGaussianPrimitiveCenterCoordinates",Y0VariantList);

        QList<qreal> Z0List( m_basis->Z0List() );
        QVariantList Z0VariantList;
        for( qint64 i=0 ; i < Z0List.length() ; ++i )
        {
          Z0VariantList.append( Z0List.at(i) );
        }
        m_molecule->setProperty("QTAIMZGaussianPrimitiveCenterCoordinates",Z0VariantList);

        // Angular Momenta
        QList<qint64> xamomList( m_basis->xamomList() );
        QVariantList xamomVariantList;
        for( qint64 i=0 ; i < xamomList.length() ; ++i )
        {
          xamomVariantList.append( xamomList.at(i) );
        }
        m_molecule->setProperty("QTAIMXGaussianPrimitiveAngularMomenta",xamomVariantList);

        QList<qint64> yamomList( m_basis->yamomList() );
        QVariantList yamomVariantList;
        for( qint64 i=0 ; i < yamomList.length() ; ++i )
        {
          yamomVariantList.append( yamomList.at(i) );
        }
        m_molecule->setProperty("QTAIMYGaussianPrimitiveAngularMomenta",yamomVariantList);

        QList<qint64> zamomList( m_basis->zamomList() );
        QVariantList zamomVariantList;
        for( qint64 i=0 ; i < zamomList.length() ; ++i )
        {
          zamomVariantList.append( zamomList.at(i) );
        }
        m_molecule->setProperty("QTAIMZGaussianPrimitiveAngularMomenta",zamomVariantList);


        QList<qreal> alphaList( m_basis->alphaList() );
        QVariantList alphaVariantList;
        for( qint64 i=0 ; i < alphaList.length() ; ++i )
        {
          alphaVariantList.append( alphaList.at(i) );
        }
        m_molecule->setProperty("QTAIMGaussianPrimitiveExponentCoefficients",alphaVariantList);

        // Orbital Eigenvalues
        QList<qreal> orbeList( m_basis->orbeList() );
        QVariantList orbeVariantList;
        for( qint64 i=0 ; i < numberOfMolecularOrbitals /* orbeList.length() */ ; ++i )
        {
          // orbeVariantList.append( orbeList.at(i) );
          qreal zero=0.0;
          orbeVariantList.append( zero );
        }
        m_molecule->setProperty("QTAIMMolecularOrbitalEigenvalues",orbeVariantList);


        // Occupation Numbers (remember only non-zero)
        QList<qreal> occnoList( m_basis->occnoList() );
        QVariantList occnoVariantList;
        for( qint64 i=0 ; i < numberOfMolecularOrbitals /* occnoList.length() */ ; ++i )
        {
//          occnoVariantList.append( occnoList.at(i) );
          qreal two=2.0;
          occnoVariantList.append( two );
        }
        m_molecule->setProperty("QTAIMMolecularOrbitalOccupationNumbers",occnoVariantList);

        // Primitive Coefficients (remember only non-zero)

        qint64 numberOfGaussianPrimitives( static_cast<qint64>( alphaList.length() ) );
        QVariant numberOfGaussianPrimitivesVariant(numberOfGaussianPrimitives);
        m_molecule->setProperty("QTAIMNumberOfGaussianPrimitives",numberOfGaussianPrimitivesVariant);

        QList<qreal> coefList( m_basis->coefList() );
        QVariantList coefVariantList;
        for( qint64 m=0 ; m < numberOfMolecularOrbitals /* coefList.length() */ ; ++m )
        {
          for( qint64 p=0 ; p < numberOfGaussianPrimitives ; ++p )
          {
            coefVariantList.append( coefList.at( m*numberOfGaussianPrimitives + p ) );
          }
        }
        m_molecule->setProperty("QTAIMMolecularOrbitalCoefficients",coefVariantList);

        // Zero the Total Energy for now
        qreal zero=0.0;
        m_molecule->setProperty("QTAIMTotalEnergy", zero );

        // Set the Virial Ratio (-V/T) to 2 for now
        qreal two=2.0;
        m_molecule->setProperty("QTAIMVirialRatio", two );

      }

    }
#endif


  }

  void OrbitalExtension::calculateOrbitalFromWidget(unsigned int orbital,
                                                    double resolution)
  {
    addCalculationToQueue(orbital, resolution, m_widget->isovalue(), 0);
    checkQueue();
  }


  void OrbitalExtension::precalculateOrbitals()
  {
    if (m_basis) {
      // Determine HOMO
      unsigned int homo = ceil( m_basis->numElectrons() / 2.0 );

      qDebug() << " num electrons " << m_basis->numElectrons();

      // Initialize prioritizer at HOMO's index
      int priority = homo;

      // Loop through all MOs, submitting calculations with increasing
      // priority until HOMO is reached, submit both HOMO and LUMO at
      // priority=1, then start increasing for orbitals above LUMO.
      // E.g,
      // .... HOMO-2 HOMO-1 HOMO LUMO LUMO+1 LUMO+2 ... << orbitals
      // ....   3      2     1    1     2      3    ... << priorities

      // Determine range of precalculated orbitals
      int startIndex = (m_widget->precalcLimit())
          ? homo - (m_widget->precalcRange()/2)
          : 0;
      if (startIndex < 0) {
        startIndex = 0;
      }
      int endIndex =  (m_widget->precalcLimit())
          ? homo + (m_widget->precalcRange()/2) - 1
          : m_basis->numMOs();
      if (endIndex > static_cast<int>(m_basis->numMOs() - 1)) {
        endIndex = m_basis->numMOs() - 1;
      }

      // D11: background precalculation runs at most at Medium resolution
      // regardless of the persisted default quality, so it cannot repeat the
      // whole cost xN at Very High. Explicit per-orbital calculation keeps
      // the user-chosen quality (unchanged path in orbitalwidget.cpp).
      OrbitalWidget::OrbitalQuality q = m_widget->precalcLimit()
          ? OrbitalWidget::clampPrecalcQuality(m_widget->defaultQuality())
          : OrbitalWidget::OQ_Medium;

      for (int i = startIndex; i <= endIndex; i++) {
        addCalculationToQueue(i+1,  // orbital
                              OrbitalWidget::OrbitalQualityToDouble(q),
                              m_widget->isovalue(),
                              priority);

        // Update priority. Stays the same when i = homo.
        if ( i + 1 < static_cast<int>(homo) ) priority--;
        else if ( i + 1 > static_cast<int>(homo)) priority++;
      }
    }
    checkQueue();
  }

  void OrbitalExtension::addCalculationToQueue(unsigned int orbital,
                                               double resolution,
                                               double isovalue,
                                               unsigned int priority)
  {
    // Create new queue entry
    calcInfo newCalc;
    newCalc.orbital = orbital;
    newCalc.resolution = resolution;
    newCalc.isovalue = isovalue;
    newCalc.priority = priority;
    newCalc.state = NotStarted;

    // Add new calculation
    m_queue.append(newCalc);

    // Set progress to show 0%
    m_widget->calculationQueued(newCalc.orbital);

    qDebug() << "New calculation added:" << newCalc.orbital
             << "Priority:" << newCalc.priority;
  }

  void OrbitalExtension::startCalculation(unsigned int queueIndex)
  {
    // This will launch calculateMesh when finished.
    m_currentRunningCalculation = queueIndex;
    m_runningGeneration = m_generation;   // N.1: capture epoch for the stale-callback guard

    calcInfo *info = &m_queue[m_currentRunningCalculation];

    qDebug() << info->orbital << " startCalculation() called";

    switch (info->state) {
    case NotStarted: // Start calculation
      calculateCube();
      break;
    case Running: // Nothing below should happen...
      qWarning() << "startCalculation called on a running calc...";
      break;
    case Completed:
      qWarning() << "startCalculation called on a completed calc...";
      break;
    case Canceled:
      qWarning() << "startCalculation called on a canceled calc...";
      break;
    }
  }

  void OrbitalExtension::calculateCube()
  {
    calcInfo *info = &m_queue[m_currentRunningCalculation];

    info->state = Running;

    // Check if the cube we want already exists
    for (int i = 0; i < m_queue.size(); i++) {
      calcInfo *cI = &m_queue[i];
      if (cI->state == Completed &&
          cI->orbital == info->orbital &&
          cI->resolution == info->resolution) {
        info->cube = cI->cube;
        qDebug() << "Reusing cube from calculation " << i << ":\n"
                 << "\tOrbital " << cI->orbital << "\n"
                 << "\tResolution " << cI->resolution;
        calculatePosMesh();
        return;
      }
    }

    // Create new cube
    Cube *cube = m_molecule->addCube();
    info->cube = cube;
    cube->setLimits(m_molecule, info->resolution, 2.5);

    if (m_qube) {
      delete m_qube;
      m_qube = 0;
    }

    m_qube = new OpenQube::Cube;
    m_qube->setLimits(cube->min(), cube->max(), cube->dimensions());

    // Start the cube calculation asynchronously: the heavy per-point setup
    // and point map run on a worker thread, so the GUI stays responsive.
    m_basis->startCubeCalculationAsync(m_qube, info->orbital);
    connect(&m_basis->watcher(), SIGNAL(finished()),
            this, SLOT(calculateCubeDone()));

    m_widget->initializeProgress(info->orbital,
                                 m_basis->watcher().progressMinimum(),
                                 m_basis->watcher().progressMaximum(),
                                 1, 3);

    connect(&m_basis->watcher(), SIGNAL(progressValueChanged(int)),
            this, SLOT(updateProgress(int)));

    qDebug() << info->orbital << " Cube calculation started.";
  }

  void OrbitalExtension::calculateCubeDone()
  {
    // N.1/1.2: stale or invalid callback — never touch m_queue, m_basis, m_qube.
    if (m_runningGeneration != m_generation ||
        m_currentRunningCalculation < 0 ||
        m_currentRunningCalculation >= (int)m_queue.size())
      return;

    calcInfo *info = &m_queue[m_currentRunningCalculation];

    qDebug() << info->orbital << " Cube calculation finished.";

    disconnect(&m_basis->watcher(), 0, this, 0);

    // Hand the grid-sized data copy off to a worker thread (D3) so the GUI
    // thread is not stalled by the memcpy. Take ownership of m_qube, null the
    // member, and let the worker copy + delete it after locking the molecule
    // cube. The queued continuation re-arms the phase machine on the GUI.
    OpenQube::Cube *qube = m_qube;
    m_qube = 0;
    if (qube) {
      const unsigned long generation = m_runningGeneration;
      Cube *cube = info->cube;
      // Track the copy worker so setMolecule can quiesce it on a document
      // switch before releasing the cube. The queued continuation still tracks
      // phase advancement; this member only bounds the copy's lifetime.
      m_cubeCopyFuture = QtConcurrent::run([this, generation, cube, qube]() {
        // (a) Stale document switch while copying: drop the cube, no-op.
        if (generation != m_generation) {
          delete qube;
          return;
        }
        // (b)-(e) Copy under the molecule-cube write lock (lock stays on this
        // worker; the unlock happens here, keeping the pair on one thread).
        // Re-check the epoch AFTER acquiring the lock: a switch landing after
        // the first guard but before the copy is caught here, so we never
        // setData into a cube whose molecule is being torn down.
        cube->lock()->lockForWrite();
        if (generation == m_generation) {
          cube->setData(*qube->data());
        }
        cube->lock()->unlock();
        // (f) The worker now owns qube.
        delete qube;
        // (g) Resume the phase machine on the GUI thread.
        QMetaObject::invokeMethod(
            this, [this, generation, cube] {
              if (generation == m_generation)
                continueAfterCubeCopy(cube);
            },
            Qt::QueuedConnection);
      });
    } else {
      // No cube data (e.g. cancelled mid-calc): resume directly.
      continueAfterCubeCopy(info->cube);
    }
  }

  void OrbitalExtension::continueAfterCubeCopy(Cube *cube)
  {
    // N.1/1.2: re-check the epoch — a document switch may have superseded this
    // calculation while the copy was in flight.
    if (m_runningGeneration != m_generation ||
        m_currentRunningCalculation < 0 ||
        m_currentRunningCalculation >= (int)m_queue.size())
      return;
    (void)cube;
    calculatePosMesh();
  }

  void OrbitalExtension::calculatePosMesh()
  {
    calcInfo *info = &m_queue[m_currentRunningCalculation];

    info->state = Running;

    // Check if the mesh we want already exists
    for (int i = 0; i < m_queue.size(); i++) {
      calcInfo *cI = &m_queue[i];
      if (cI->state == Completed &&
          cI->orbital == info->orbital &&
          cI->resolution == info->resolution &&
          cI->isovalue == info->isovalue) {
        info->posMesh = cI->posMesh;
        qDebug() << "Reusing posMesh from calculation " << i << ":\n"
                 << "\tOrbital " << cI->orbital << "\n"
                 << "\tResolution " << cI->resolution << "\n"
                 << "\tIsovalue " << cI->isovalue;
        m_widget->nextProgressStage(info->orbital, 0, 100);
        calculateNegMesh();
        return;
      }
    }

    Cube *cube = info->cube;

    Mesh *mesh = m_molecule->addMesh();
    mesh->setName(cube->name());
    mesh->setIsoValue(info->isovalue);
    mesh->setCube(cube->id());
    info->posMesh = mesh;

    if (m_meshGen) {
      m_meshGen->disconnect();
      delete m_meshGen;
    }
    m_meshGen = new MeshGenerator;

    connect(m_meshGen, SIGNAL(finished()),
            this, SLOT(calculatePosMeshDone()));

    m_meshGen->initialize(cube, mesh, info->isovalue);

    m_widget->nextProgressStage(info->orbital,
                                m_meshGen->progressMinimum(),
                                m_meshGen->progressMaximum());

    m_meshGen->start();

    connect(m_meshGen, SIGNAL(progressValueChanged(int)),
            this, SLOT(updateProgress(int)));

    qDebug() << info->orbital << " posMesh calculation started.";
  }

  void OrbitalExtension::calculatePosMeshDone()
  {
    // N.1/1.2: stale or invalid callback — never touch m_queue, m_meshGen.
    if (m_runningGeneration != m_generation ||
        m_currentRunningCalculation < 0 ||
        m_currentRunningCalculation >= (int)m_queue.size())
      return;

    calcInfo *info = &m_queue[m_currentRunningCalculation];

    disconnect(m_meshGen, 0,
               this, 0);

    qDebug() << info->orbital << " posMesh calculation finished.";

    calculateNegMesh();
  }

  void OrbitalExtension::calculateNegMesh()
  {
    calcInfo *info = &m_queue[m_currentRunningCalculation];

    info->state = Running;

    // Check if the mesh we want already exists
    for (int i = 0; i < m_queue.size(); i++) {
      calcInfo *cI = &m_queue[i];
      if (cI->state == Completed &&
          cI->orbital == info->orbital &&
          cI->resolution == info->resolution &&
          cI->isovalue == info->isovalue) {
        info->negMesh = cI->negMesh;
        qDebug() << "Reusing posMesh from calculation " << i << ":\n"
                 << "\tOrbital " << cI->orbital << "\n"
                 << "\tResolution " << cI->resolution << "\n"
                 << "\tIsovalue " << cI->isovalue;
        m_widget->nextProgressStage(info->orbital, 0, 100);
        calculationComplete();
        return;
      }
    }

    Cube *cube = info->cube;

    Mesh *mesh = m_molecule->addMesh();
    info->negMesh = mesh;
    mesh->setName(cube->name());
    mesh->setIsoValue(0.0 - info->isovalue);
    mesh->setCube(cube->id());

    if (m_meshGen) {
      m_meshGen->disconnect();
      delete m_meshGen;
    }
    m_meshGen = new MeshGenerator;

    connect(m_meshGen, SIGNAL(finished()),
            this, SLOT(calculateNegMeshDone()));

    m_meshGen->initialize(cube, mesh, 0.0 - info->isovalue,
                          true); // Reverse the surface

    m_widget->nextProgressStage(info->orbital,
                                m_meshGen->progressMinimum(),
                                m_meshGen->progressMaximum());

    m_meshGen->start();

    connect(m_meshGen, SIGNAL(progressValueChanged(int)),
            this, SLOT(updateProgress(int)));

    qDebug() << info->orbital << " negMesh calculation started.";
  }

  void OrbitalExtension::calculateNegMeshDone()
  {
    // N.1/1.2: stale or invalid callback — never touch m_queue, m_meshGen.
    if (m_runningGeneration != m_generation ||
        m_currentRunningCalculation < 0 ||
        m_currentRunningCalculation >= (int)m_queue.size())
      return;

    calcInfo *info = &m_queue[m_currentRunningCalculation];

    disconnect(m_meshGen, 0,
               this, 0);

    qDebug() << info->orbital << " negMesh calculation finished.";
    calculationComplete();
  }

  void OrbitalExtension::calculationComplete()
  {
    // N.1/1.2: stale or invalid callback — return without touching the mutex
    // (setMolecule owns mutex teardown) or m_queue.
    if (m_runningGeneration != m_generation ||
        m_currentRunningCalculation < 0 ||
        m_currentRunningCalculation >= (int)m_queue.size())
      return;

    calcInfo *info = &m_queue[m_currentRunningCalculation];

    m_widget->calculationComplete(info->orbital);

    info->state = Completed;
    m_currentRunningCalculation = -1;
    m_runningMutex->unlock();

    // Show orbital is calculation was user requested
    if (info->priority == 0)
      m_widget->selectOrbital(info->orbital);

    // D4: if the user selected this orbital while it was queued/running (the
    // latest click wins), render it now that the calculation is complete —
    // before checkQueue() so a queued follow-up does not pre-empt it. Clear
    // the pending marker first so a later stale completion cannot re-render.
    if (m_pendingOrbital == info->orbital) {
      m_pendingOrbital = 0;
      renderCompletedOrbital(info->orbital);
    }

    qDebug() << info->orbital << " all calculations complete.";
    checkQueue();
  }

  bool OrbitalExtension::renderCompletedOrbital(unsigned int orbital)
  {
    // No current GLWidget (e.g. headless unit test): nothing to render into.
    GLWidget *current = GLWidget::current();
    if (!current) {
      qDebug() << "No current GLWidget; cannot render orbital " << orbital;
      return false;
    }

    // TODO Actually select the engine. For now, just use the first
    // surface engine
    Engine *engine = 0;

    foreach (Engine *e, current->engines()) {
      if (e->identifier() == "Surfaces") {
        engine = e;
      }
    }
    if (!engine) {
      qDebug() << "Engine is null - no engines of this type loaded.";
      return false; // prevent a crash if the surface engine isn't loaded
    }

    engine->setMolecule(m_molecule); // prevent a crash after switching file

    // Find the most recent calc matching the selected orbital:
    int index = -1;
    for (int i = 0; i < m_queue.size(); i++) {
      if ( m_queue.at(i).orbital == orbital &&
           m_queue.at(i).state == Completed ) {
        index = i;
      }
    }

    if (index == -1) {
      qDebug() << "Orbital not found, or still calculating. Cannot render.";
      return false;
    }

    QSettings settings;
    engine->writeSettings(settings);
    settings.setValue("colorMode", 1);
    settings.setValue("mesh1Id",static_cast<int>(m_queue[index].posMesh->id()));
    settings.setValue("mesh2Id",static_cast<int>(m_queue[index].negMesh->id()));
    engine->readSettings(settings);
    engine->setEnabled(true);
    // Trigger a repaint with the new mesh
    /// FIXME Should be using m_molecule->update() to trigger a repaint in
    /// all open displays, this currently causes crashes - need to track
    /// down the cause.
    current->update();
    return true;
  }

  void OrbitalExtension::renderOrbital(unsigned int orbital)
  {
    qDebug() << "Attempting to render orbital " << orbital;

    // Determine the orbital's state in the queue so the click always responds
    // (D4): a completed orbital renders now; a queued/running one is marked
    // pending and renders on completion; one that is neither is enqueued.
    bool completed = false;
    bool queuedOrRunning = false;
    for (int i = 0; i < m_queue.size(); i++) {
      if (m_queue.at(i).orbital == orbital) {
        if (m_queue.at(i).state == Completed)
          completed = true;
        else
          queuedOrRunning = true;
      }
    }

    switch (planOrbitalSelection(completed, queuedOrRunning)) {
    case SelectionPlan::RenderNow:
      renderCompletedOrbital(orbital);
      break;
    case SelectionPlan::RenderWhenReady:
      // The table's progress column already shows the running/queued calc, so
      // no extra UI is needed here; mark it pending so it renders on
      // completion. The latest click wins (m_pendingOrbital is overwritten).
      m_pendingOrbital = orbital;
      break;
    case SelectionPlan::EnqueueThenRender:
      addCalculationToQueue(orbital,
                            OrbitalWidget::OrbitalQualityToDouble(
                              m_widget->selectedQuality()),
                            m_widget->isovalue(),
                            /*priority=*/0);
      checkQueue();
      m_pendingOrbital = orbital;
      break;
    }
  }

  void OrbitalExtension::checkQueue()
  {
    if (!m_runningMutex->tryLock())
      return;

    // Create a hash: keys=priority, values=indices

    QHash<int, int> hash;
    CalcState state;

    for (int i = 0; i < m_queue.size(); i++) {
      state = m_queue.at(i).state;

      // If there is already a running job, return.
      if ( state == Running ) {
        return;
      }

      if ( state == NotStarted ) {
        hash.insert(m_queue[i].priority, i);
      }
    }

    // Do nothing if all calcs are finished.
    if (hash.size() == 0) {
      m_runningMutex->unlock();
      qDebug() << "Finished queue.";
      return;
    }

    QList<int> priorities = hash.keys();
    std::sort(priorities.begin(), priorities.end());
    startCalculation(hash.value(priorities.first()));
  }

  OpenQube::BasisSet *OrbitalExtension::parseBasisFile(const QString &fileName,
                                                       const QString &format)
  {
    if (fileName.isEmpty())
      return nullptr;

    // Check to see if this molecule has been tagged with a file format
    if (format == QLatin1String("gamout")) {
      qDebug() << " deduced from format ";
      GaussianSet *gaussian = new GaussianSet;
      GAMESSUSOutput gamout(fileName, gaussian);
      return gaussian;
    }
    else if (format == QLatin1String("gukout")) {
      qDebug() << " deduced from format ";
      GaussianSet *gaussian = new GaussianSet;
      GamessukOut gukout(fileName, gaussian);
      return gaussian;
    }

    // Everything looks good, a new basis set needs to be loaded
    // Check for files in this directory -- first the file itself
    // and then any other similar file
    QString basisFileName = OpenQube::BasisSetLoader::MatchBasisSet(fileName);
    if (basisFileName.isEmpty()) {
      qDebug() << "No matching basis set file found: " << fileName;
      return nullptr;
    }
    return OpenQube::BasisSetLoader::LoadBasisSet(basisFileName);
  }

  bool OrbitalExtension::loadBasis()
  {
    // D3: dispatch the parse to a worker thread and resume on the GUI thread
    // via a queued continuation. Capture the file name, file-format tag and the
    // current generation epoch so the continuation can discard a stale parse
    // after a document switch (the worker touches no member state).
    const QString fileName = m_molecule ? m_molecule->fileName() : QString();
    const QString format =
        m_molecule ? m_molecule->property("File Format").toString() : QString();
    const unsigned long generation = m_generation;

    m_basisLoadFuture = QtConcurrent::run(
        [this, fileName, format, generation]() {
          OpenQube::BasisSet *basis = parseBasisFile(fileName, format);
          // Resume on the GUI thread. `this` is the invoke context: Qt cancels
          // the queued call if the extension is destroyed before delivery, so
          // the continuation cannot outlive the extension.
          QMetaObject::invokeMethod(
              this, [this, generation, basis] {
                if (generation != m_generation) {
                  delete basis; // stale: superseded by a document switch
                  return;
                }
                m_basis = basis;
                finishBasisLoad();
              },
              Qt::QueuedConnection);
        });
    return true;
  }

  void OrbitalExtension::finishBasisLoad()
  {
    if (!m_basis || m_basis->numElectrons() == 0) {
      if (m_dock) {
        m_widget->setEnabled(false);
        QList<Orbital> list;
        list.clear();
        m_widget->fillTable(list);

        if (m_dock->toggleViewAction()->isChecked())
          m_dock->toggleViewAction()->activate(QAction::Trigger);
      }
      return; // no electrons, no orbitals, no orbital widget
    }
    // Show dock
    if (m_dock && m_molecule && m_basis) {
      m_widget->setEnabled(true);
      if (!m_dock->toggleViewAction()->isChecked())
        m_dock->toggleViewAction()->activate(QAction::Trigger);
    }

    // Send MO data to table
    if (m_basis) {
      QList<Orbital> list;
      unsigned int homo = ceil( m_basis->numElectrons() / 2.0 );
      unsigned int lumo = homo + 1;
      unsigned int count = homo - 1;
      bool leqHOMO = true; // orbital <= homo

      // energies and symmetries
      //TODO: Alpha / Beta orbitals
      QList<QVariant> alphaEnergies;
      QVariant property = m_molecule->property("alphaOrbitalEnergies");
      if (property.isValid())
        alphaEnergies = property.toList();
      QStringList alphaSymmetries;
      property = m_molecule->property("alphaOrbitalSymmetries");
      if (property.isValid())
        alphaSymmetries = property.toStringList();

      for (size_t i = 0; i < m_basis->numMOs(); i++) {
        QString num = "";
        if (i+1 != homo && i+1 != lumo) {
          num = (leqHOMO) ? "-" : "+";
          num += QString::number(count);
        }

        QString desc = QString("%1")
          // (HOMO|LUMO)(+|-)[0-9]+
          .arg( (leqHOMO)
                ? tr("HOMO", "Highest Occupied MO") + num
                : tr("LUMO", "Lowest Unoccupied MO") + num
                );

        Orbital orb;
        // Get the energy from the molecule property list, if available
        if (static_cast<size_t>(alphaEnergies.size()) > i)
          orb.energy = alphaEnergies[i].toDouble();
        else
          orb.energy = 0.0;
        // symmetries (if available)
        if (static_cast<size_t>(alphaSymmetries.size()) > i)
          orb.symmetry = alphaSymmetries[i];
        orb.index = i;
        orb.description = desc;
        orb.queueEntry = 0;
        orb.min = 0;
        orb.max = 0;
        orb.current = 0;

        list.append(orb);
        if (i+1 < homo) count--;
        else if (i+1 == homo) leqHOMO = false;
        else if (i+1 >= lumo) count++;
      }
      m_widget->fillTable(list);
    }
    precalculateOrbitals();
  }

  void OrbitalExtension::updateProgress(int current)
  {
    // N.1/1.2: stale or invalid callback — never touch m_queue or m_widget.
    if (m_runningGeneration != m_generation ||
        m_currentRunningCalculation < 0 ||
        m_currentRunningCalculation >= (int)m_queue.size())
      return;

    calcInfo *info = &m_queue[m_currentRunningCalculation];
    int orbital = info->orbital;

    // Throttle progress updates so a very fine-grained watcher (millions of
    // emitted values across a large grid) cannot flood the event loop. Emit
    // at most ~10 Hz; always emit once more at the maximum so the bar can
    // still reach 100% when the watcher jumps straight to its max.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastProgressEmit < 100
        && current != m_basis->watcher().progressMaximum())
      return;
    m_lastProgressEmit = now;

    m_widget->updateProgress(orbital, current);
  }

} // End namespace Avogadro


