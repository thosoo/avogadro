/**********************************************************************
  OrcaInputDialog - ORCA Input Deck Dialog for Avogadro

  Copyright (C) 2014 Dagmar Lenk

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

#include "orcainputdialog.h"
#include "orcaextension.h"

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <openbabel/atom.h>
#include <avogadro/periodictableview.h>

#include <openbabel/mol.h>
#include <openbabel/elements.h>
#include <openbabel/obiter.h>
#include <openbabel/internalcoord.h>

#include <Eigen/Geometry>

#include <vector>

#include <QtGui>
#include <QButtonGroup>
#include <QComboBox>
#include <QDebug>
#include <QPushButton>

#include <QString>
#include <QTextStream>
#include <QtGlobal>
#include <QFileDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTimer>

using namespace OpenBabel;
using namespace Eigen;
using namespace std;

namespace Avogadro {

namespace {
QStringList orcaSolventEntries()
{
    return QStringList()
      << "1,1,1-trichloroethane"
      << "1,1,2-trichloroethane"
      << "1,2,4-trimethylbenzene"
      << "1,2-dibromoethane"
      << "1,2-dichloroethane"
      << "1,2-ethanediol"
      << "1,4-dioxane / dioxane"
      << "1-bromo-2-methylpropane"
      << "1-bromooctane / bromooctane"
      << "1-bromopentane"
      << "1-bromopropane"
      << "1-butanol / butanol"
      << "1-chlorohexane / chlorohexane"
      << "1-chloropentane"
      << "1-chloropropane"
      << "1-decanol / decanol"
      << "1-fluorooctane"
      << "1-heptanol / heptanol"
      << "1-hexanol / hexanol"
      << "1-hexene"
      << "1-hexyne"
      << "1-iodobutane"
      << "1-iodohexadecane / hexadecyliodide"
      << "1-iodopentane"
      << "1-iodopropane"
      << "1-nitropropane"
      << "1-nonanol / nonanol"
      << "1-octanol / octanol"
      << "1-pentanol / pentanol"
      << "1-pentene"
      << "1-propanol / propanol"
      << "2,2,2-trifluoroethanol"
      << "2,2,4-trimethylpentane / isooctane"
      << "2,4-dimethylpentane"
      << "2,4-dimethylpyridine"
      << "2,6-dimethylpyridine"
      << "2-bromopropane"
      << "2-butanol / secbutanol"
      << "2-chlorobutane"
      << "2-heptanone"
      << "2-hexanone"
      << "2-methoxyethanol / methoxyethanol"
      << "2-methyl-1-propanol / isobutanol"
      << "2-methyl-2-propanol"
      << "2-methylpentane"
      << "2-methylpyridine / 2methylpyridine"
      << "2-nitropropane"
      << "2-octanone"
      << "2-pentanone"
      << "2-propanol / isopropanol"
      << "2-propen-1-ol"
      << "e-2-pentene"
      << "3-methylpyridine"
      << "3-pentanone"
      << "4-heptanone"
      << "4-methyl-2-pentanone / 4methyl2pentanone"
      << "4-methylpyridine"
      << "5-nonanone"
      << "acetic acid / aceticacid"
      << "acetone"
      << "acetonitrile / mecn / ch3cn"
      << "acetophenone"
      << "ammonia"
      << "aniline"
      << "anisole"
      << "benzaldehyde"
      << "benzene"
      << "benzonitrile"
      << "benzyl alcohol / benzylalcohol"
      << "bromobenzene"
      << "bromoethane"
      << "bromoform"
      << "butanal"
      << "butanoic acid"
      << "butanone"
      << "butanonitrile"
      << "butyl ethanoate / butyl acetate / butylacetate"
      << "butylamine"
      << "n-butylbenzene / butylbenzene"
      << "sec-butylbenzene / secbutylbenzene"
      << "tert-butylbenzene / tbutylbenzene"
      << "carbon disulfide / carbondisulfide / cs2"
      << "carbon tetrachloride / ccl4"
      << "chlorobenzene"
      << "chloroform / chcl3"
      << "a-chlorotoluene"
      << "o-chlorotoluene"
      << "conductor"
      << "m-cresol / mcresol"
      << "o-cresol"
      << "cyclohexane"
      << "cyclohexanone"
      << "cyclopentane"
      << "cyclopentanol"
      << "cyclopentanone"
      << "decalin"
      << "cis-decalin"
      << "n-decane / decane"
      << "dibromomethane"
      << "dibutylether"
      << "o-dichlorobenzene / odichlorobenzene"
      << "e-1,2-dichloroethene"
      << "z-1,2-dichloroethene"
      << "dichloromethane / ch2cl2 / dcm"
      << "diethyl ether / diethylether"
      << "diethyl sulfide"
      << "diethylamine"
      << "diiodomethane"
      << "diisopropyl ether / diisopropylether"
      << "cis-1,2-dimethylcyclohexane"
      << "dimethyl disulfide"
      << "n,n-dimethylacetamide / dimethylacetamide"
      << "n,n-dimethylformamide / dimethylformamide / dmf"
      << "dimethylsulfoxide / dmso"
      << "diphenylether"
      << "dipropylamine"
      << "n-dodecane / dodecane"
      << "ethanethiol"
      << "ethanol"
      << "ethyl acetate / ethylacetate / ethanoate"
      << "ethyl methanoate"
      << "ethyl phenyl ether / ethoxybenzene"
      << "ethylbenzene"
      << "fluorobenzene"
      << "formamide"
      << "formic acid"
      << "furan / furane"
      << "n-heptane / heptane"
      << "n-hexadecane / hexadecane"
      << "n-hexane / hexane"
      << "hexanoic acid"
      << "iodobenzene"
      << "iodoethane"
      << "iodomethane"
      << "isopropylbenzene"
      << "p-isopropyltoluene / isopropyltoluene"
      << "mesitylene"
      << "methanol"
      << "methyl benzoate"
      << "methyl butanoate"
      << "methyl ethanoate"
      << "methyl methanoate"
      << "methyl propanoate"
      << "n-methylaniline"
      << "methylcyclohexane"
      << "n-methylformamide / methylformamide"
      << "nitrobenzene / phno2"
      << "nitroethane"
      << "nitromethane / meno2"
      << "o-nitrotoluene / onitrotoluene"
      << "n-nonane / nonane"
      << "n-octane / octane"
      << "n-pentadecane / pentadecane"
      << "octanol(wet) / wetoctanol / woctanol"
      << "pentanal"
      << "n-pentane / pentane"
      << "pentanoic acid"
      << "pentyl ethanoate"
      << "pentylamine"
      << "perfluorobenzene / hexafluorobenzene"
      << "phenol"
      << "propanal"
      << "propanoic acid"
      << "propanonitrile"
      << "propyl ethanoate"
      << "propylamine"
      << "pyridine"
      << "tetrachloroethene / c2cl4"
      << "tetrahydrofuran / thf"
      << "tetrahydrothiophene-s,s-dioxide / tetrahydrothiophenedioxide / sulfolane"
      << "tetralin"
      << "thiophene"
      << "thiophenol"
      << "toluene"
      << "trans-decalin"
      << "tributylphosphate"
      << "trichloroethene"
      << "triethylamine"
      << "n-undecane / undecane"
      << "water / h2o"
      << "xylene"
      << "m-xylene"
      << "o-xylene"
      << "p-xylene";
}

QStringList fallbackBasisEntries()
{
    return QStringList()
      << "def2-SVP"
      << "def2-SV(P)"
      << "def2-TZVP"
      << "def2-TZVP(-f)"
      << "def2-TZVPP"
      << "def2-QZVPP";
}

QStringList fallbackDFTFunctionalEntries()
{
    return QStringList()
      << "PBE"
      << "r2SCAN"
      << "B3LYP"
      << "PBE0"
      << "TPSSh"
      << "M06L";
}

int boundedComboIndex(const QComboBox *combo, int index)
{
    if (!combo || combo->count() <= 0)
        return -1;
    return qBound(0, index, combo->count() - 1);
}

void setComboIndexBounded(QComboBox *combo, int index)
{
    const int boundedIndex = boundedComboIndex(combo, index);
    if (boundedIndex >= 0)
        combo->setCurrentIndex(boundedIndex);
}
}

OrcaInputDialog::OrcaInputDialog(QWidget *parent, Qt::WindowFlags f ) :
    QDialog( parent, f ), m_molecule(NULL), basicData(NULL), basisData(NULL),
    controlData(NULL), dataData(NULL), scfData(NULL), dftData(NULL),
    m_basic(true), m_advanced(false),
    m_scfConvButtons(NULL), m_scfConv2ndButtons(NULL), m_output(), m_savePath(),
    m_dirty(false), m_warned(false), m_initializing(true),
    m_pendingMoleculeSync(false)
{
    qInfo() << "OrcaInputDialog: constructor start";
    basicData = new OrcaBasicData;
    basisData = new OrcaBasisData;
    controlData = new OrcaControlData;
    scfData = new OrcaSCFData;
    dftData = new OrcaDFTData;
    dataData = new OrcaDataData;
    qInfo() << "OrcaInputDialog: data objects allocated";

    // This initializes the ui member function to contain pointers to
    // all GUI elements in the orcainputdialog.ui file
    qInfo() << "OrcaInputDialog: before setupUi";
    ui.setupUi(this);
    qInfo() << "OrcaInputDialog: setupUi complete";

    // write items into comboboxes

    qInfo() << "OrcaInputDialog: before initComboboxes";
    initComboboxes();
    qInfo() << "OrcaInputDialog: after initComboboxes";

    ui.basicCalculationCombo->setItemText(0, tr("Single Point"));
    ui.basicCalculationCombo->setItemText(1, tr("Geometry Optimization"));
    ui.basicCalculationCombo->setItemText(2, tr("Frequencies"));
    ui.basicCalculationCombo->addItem(tr("Optimization + Frequencies"));
    ui.label_7->setText(tr("Job type"));
    ui.basicMethodCombo->clear();
    ui.basicMethodCombo->addItems(QStringList() << "HF" << "DFT");

    ui.controlRunTypeCombo->setItemText(0, tr("Single Point"));
    ui.controlRunTypeCombo->setItemText(1, tr("Geometry Optimization"));
    ui.controlRunTypeCombo->setItemText(2, tr("Frequencies"));
    ui.controlRunTypeCombo->addItem(tr("Optimization + Frequencies"));
    ui.label_13->setText(tr("Job type"));

    ui.label_33->setText(tr("Dispersion"));
    ui.dispersionCombo->clear();
    ui.dispersionCombo->addItems(QStringList() << tr("None") << "D3BJ" << "D4");
    ui.solvationModelCombo->clear();
    ui.solvationModelCombo->addItems(QStringList() << tr("None") << "CPCM" << "CPCMC" << "SMD");
    ui.solvationCombo->clear();
    ui.solvationCombo->addItems(orcaSolventEntries());
    ui.cpcmSurfaceTypeCombo->clear();
    ui.cpcmSurfaceTypeCombo->addItems(QStringList() << tr("Default")
                                     << "vdw_gaussian"
                                     << "gepol_ses"
                                     << "gepol_ses_gaussian");
    ui.label_31->setText(tr("nprocs"));
    ui.label_32->setText(tr("MaxCore (MB)"));
    ui.nprocsCombo->clear();
    ui.nprocsCombo->addItems(QStringList() << "1" << "2" << "4" << "8" << "16" << "32");
    ui.maxCoreCombo->clear();
    ui.maxCoreCombo->addItems(QStringList() << "0" << "500" << "1000" << "2000" << "4000");
    ui.tddftCheck->setText(tr("Enable TD-DFT"));

    // init dialog boxes
    qInfo() << "OrcaInputDialog: before initBasicData";
    initBasicData();
    qInfo() << "OrcaInputDialog: after initBasicData";
    qInfo() << "OrcaInputDialog: before initBasisData";
    initBasisData();
    qInfo() << "OrcaInputDialog: after initBasisData";
    qInfo() << "OrcaInputDialog: before initControlData";
    initControlData();
    qInfo() << "OrcaInputDialog: after initControlData";
    qInfo() << "OrcaInputDialog: before initSCFData";
    initSCFData();
    qInfo() << "OrcaInputDialog: after initSCFData";
    qInfo() << "OrcaInputDialog: before initDFTData";
    initDFTData();
    qInfo() << "OrcaInputDialog: after initDFTData";
    qInfo() << "OrcaInputDialog: before initResourcesData";
    initResourcesData();
    qInfo() << "OrcaInputDialog: after initResourcesData";
    qInfo() << "OrcaInputDialog: before initSolvationData";
    initSolvationData();
    qInfo() << "OrcaInputDialog: after initSolvationData";
    qInfo() << "OrcaInputDialog: before initDataData";
    initDataData();
    qInfo() << "OrcaInputDialog: after initDataData";

    ui.scfDampingGroup->hide();
    ui.scfLevelShiftGroup->hide();
    ui.scfDIISRadio->hide();
    ui.scfKDIISRadio->hide();
    ui.scfSOSCFRadio->hide();
    ui.scfNRSCFRadio->hide();
    ui.scfAHSCFRadio->hide();
    ui.scfTypeCombo->hide();
    ui.label_24->hide();
    ui.label_10->hide();
    ui.dataPrintCombo->hide();
    ui.groupBox_3->hide();
    ui.basisRelativisticGroup->hide();
    setComboIndexBounded(ui.dataFormatCombo, 0);
    ui.dataFormatCombo->setEnabled(false);


    ui.modeTabWidget->setCurrentIndex(0);
    ui.advancedStacked->setCurrentIndex(0);
    ui.advancedTree->expandAll();
    ui.advancedTree->setCurrentItem(ui.advancedTree->topLevelItem(0));

    QSettings settings;
    qInfo() << "OrcaInputDialog: before readSettings";
    readSettings(settings);
    qInfo() << "OrcaInputDialog: after readSettings";

    // Connect the GUI elements to the correct slots after the widgets and
    // backing data are fully initialized. The ORCA dialog restores state and
    // synchronizes molecule data lazily, so constructor-time signals must not
    // enter runtime slots before setup is complete.
    qInfo() << "OrcaInputDialog: before connectModes";
    connectModes();
    qInfo() << "OrcaInputDialog: after connectModes";
    qInfo() << "OrcaInputDialog: before connectBasic";
    connectBasic();
    qInfo() << "OrcaInputDialog: after connectBasic";
    qInfo() << "OrcaInputDialog: before connectAdvanced";
    connectAdvanced();
    qInfo() << "OrcaInputDialog: after connectAdvanced";
    qInfo() << "OrcaInputDialog: before connectPreview";
    connectPreview();
    qInfo() << "OrcaInputDialog: after connectPreview";
    qInfo() << "OrcaInputDialog: before connectButtons";
    connectButtons();
    qInfo() << "OrcaInputDialog: after connectButtons";

    // Enable/Disable Widgets

    ui.basisAuxBasisSetCombo->setEnabled(false);
//    ui.basisAuxECPCheck->setEnabled(false);

    bool auxCorrNeeded;
    if (controlData->mp2Enabled() || controlData->ccsdEnabled()) {
        auxCorrNeeded = true;
    } else {
        auxCorrNeeded = false;
    }
    ui.basisAuxCorrBasisSetCombo->setEnabled(auxCorrNeeded);
//    ui.basisAuxCorrECPCheck->setEnabled(false);
    m_initializing = false;
    qInfo() << "OrcaInputDialog: constructor end";
}

  OrcaInputDialog::~OrcaInputDialog()
  {
      QSettings settings;
      writeSettings(settings);   
      delete basicData;
      delete basisData;
      delete controlData;
      delete scfData;
      delete dftData;
      delete dataData;
  }
  void OrcaInputDialog::showEvent(QShowEvent *event)
  {
    qInfo() << "OrcaInputDialog: showEvent start";
    QDialog::showEvent(event);
    if (m_pendingMoleculeSync)
      applyMoleculeToUi();
    // Generate an initial preview of the input deck
    QTimer::singleShot(0, this, &OrcaInputDialog::updatePreviewText);
    qInfo() << "OrcaInputDialog: showEvent end";
  }

  void OrcaInputDialog::connectModes()
  {
      connect( ui.modeTabWidget, SIGNAL( currentChanged( int ) ), this, SLOT( setMode( int ) ) );
  }

  void OrcaInputDialog::connectAdvanced()
  {
      // Advanced Basis Set Slots
      connect(ui.basisBasisSetCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setBasisBasisSet);
      connect(ui.basisAuxBasisSetCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setBasisAuxBasisSet);
      connect(ui.basisAuxCorrBasisSetCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setBasisAuxCorrBasisSet);
//      connect (ui.basisECPCheck, SIGNAL(toggled(bool)), this, SLOT(setBasisUseEPC (bool )));
//      connect (ui.basisAuxECPCheck, SIGNAL(toggled (bool)), this, SLOT( setBasisUseAuxEPC (bool )));
//      connect (ui.basisAuxCorrECPCheck, SIGNAL(toggled (bool)), this, SLOT( setBasisUseAuxCorrEPC (bool )));
//      connect (ui.basisRelativisticGroup, SIGNAL(toggled(bool)), this, SLOT(setBasisUseRel(bool)));
//      connect (ui.basisRelativisticCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(setBasisRel(int )));
//      connect (ui.basisDKHSpin, SIGNAL(valueChanged(int)), this, SLOT(setBasisDKHOrder(int)));

      // Advanced Control Slots
      connect(ui.advancedTree, SIGNAL(clicked(QModelIndex)), this, SLOT(advancedItemClicked(QModelIndex)));

      connect(ui.controlRunTypeCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setControlCalculation);
      connect(ui.controlMultiplicitySpin, SIGNAL(valueChanged(int)),
              this, SLOT(setControlMultiplicity(int)));
      connect(ui.controlChargeSpin, SIGNAL(valueChanged(int)),
              this, SLOT(setControlCharge(int)));
      connect(ui.controlMethodCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setControlMethod);

      // Advanced SCF Slots

      connect (ui.scfDampingGroup, SIGNAL(toggled(bool)), this, SLOT( setSCFUseDamping(bool)));
      connect (ui.scfLevelShiftGroup, SIGNAL(toggled(bool)), this, SLOT(setSCFUseLevelShift(bool)));
      connect (ui.scfDampFactorDSpin, SIGNAL(valueChanged(double)), this, SLOT(setSCFDampFactor (double)));
      connect (ui.scfLevelShiftDSpin, SIGNAL(valueChanged(double)), this, SLOT(setSCFLevelShift(double)));
      connect (ui.scfDampErrorDSpin, SIGNAL(valueChanged(double)), this, SLOT(setSCFDampError (double)));
      connect (ui.scfLevelErrorDSpin, SIGNAL(valueChanged(double)), this, SLOT(setSCFLevelError(double)));
      connect(ui.scfAccCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setSCFAccuracy);

      connect(ui.scfTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setSCFType);
      connect (ui.scfMaxIterSpin, SIGNAL(valueChanged(int)), this , SLOT (setSCFMaxIter(int)));

      if (m_scfConvButtons) {
          connect(m_scfConvButtons, &QButtonGroup::idClicked,
                  this, &OrcaInputDialog::setSCFConverger);
      }
      if (m_scfConv2ndButtons) {
          connect(m_scfConv2ndButtons, &QButtonGroup::idClicked,
                  this, &OrcaInputDialog::setSCF2ndConverger);
      }

      // Advanced DFT Slots

      connect(ui.solvationCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setSolvation);
      connect(ui.solvationModelCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setSolvationModel);
      connect (ui.cpcmGroup, SIGNAL(toggled(bool)), this, SLOT(setCpcmAdvancedEnabled(bool)));
      connect (ui.cpcmDRACOCheck, SIGNAL(toggled(bool)), this, SLOT(setCpcmDRACO(bool)));
      connect (ui.cpcmEpsilonSpin, SIGNAL(valueChanged(double)), this, SLOT(setCpcmEpsilon(double)));
      connect (ui.cpcmRefracSpin, SIGNAL(valueChanged(double)), this, SLOT(setCpcmRefrac(double)));
      connect (ui.cpcmRSolvSpin, SIGNAL(valueChanged(double)), this, SLOT(setCpcmRSolv(double)));
      connect(ui.cpcmSurfaceTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setCpcmSurfaceType);
      connect(ui.dispersionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setDispersion);
      connect(ui.dftFunctionalCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setDFTFunctional);

      // Advanced resource / excited-state slots
      connect(ui.maxCoreCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setResourcesMaxCore);
      connect(ui.nprocsCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setResourcesNProcs);
      connect (ui.tddftCheck, SIGNAL(toggled(bool)), this, SLOT(setTDDFTEnabled(bool)));
      connect (ui.tddftRootsSpin, SIGNAL(valueChanged(int)), this, SLOT(setTDDFTRoots(int)));
      connect (ui.nmrCheck, SIGNAL(toggled(bool)), this, SLOT(setNMRShielding(bool)));

      // Advanced Data Slots
      connect(ui.dataFormatCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setDataFormat);
      connect(ui.dataCommentLine, SIGNAL(editingFinished()), this, SLOT(setDataComment()));
      connect(ui.dataPrintCombo, qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setPrintLevel);
      connect(ui.MOPrintCheck, SIGNAL(toggled(bool)), this, SLOT(setMOPrint(bool)));
      connect(ui.basisPrintCheck, SIGNAL(toggled(bool)),this, SLOT(setBasisPrint(bool)));
  }


  void OrcaInputDialog::connectPreview()
  {
      connect(ui.previewText, SIGNAL(cursorPositionChanged()),
              this, SLOT(previewEdited()));

      connect(ui.hideButton, SIGNAL(clicked()),
              this, SLOT(hideClicked()));
  }

  void OrcaInputDialog::connectButtons()
  {
      connect(ui.generateButton, SIGNAL(clicked()),
              this, SLOT(generateClicked()));
      connect(ui.resetButton, SIGNAL(clicked()),
              this, SLOT(resetClicked()));
      connect(ui.closeButton, &QPushButton::clicked,
              this, &QDialog::close);
  }

  void OrcaInputDialog::connectBasic()
  {
      connect(ui.basicCommentLine, SIGNAL(editingFinished()),
              this, SLOT(setBasicComment()));
      connect(ui.basicCalculationCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setBasicCalculation);
      connect(ui.basicMethodCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setBasicMethod);
      connect(ui.basicBasisSetCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setBasicBasis);
      connect(ui.basicMultiplicitySpin, SIGNAL(valueChanged(int)),
              this, SLOT(setBasicMultiplicity(int)));

      connect(ui.basicChargeSpin, SIGNAL(valueChanged(int)),
              this, SLOT(setBasicCharge(int)));

      connect(ui.basicFormatCombo,
              qOverload<int>(&QComboBox::currentIndexChanged),
              this, &OrcaInputDialog::setBasicCoordsFormat);
  }

  void OrcaInputDialog::advancedItemClicked(const QModelIndex &index )
  {
      int i = index.row();

      QModelIndex parent = index.parent();
      if(parent.isValid())
      {
        i += ui.advancedTree->topLevelItemCount();
      }

      ui.advancedStacked->setCurrentIndex(i);
  }

void  OrcaInputDialog::initComboboxes()
  {
      qInfo() << "OrcaInputDialog: initComboboxes start";
      const QMetaObject &metaObject = OrcaExtension::staticMetaObject;
      QStringList items;
      bool foundDFTFunctionalType = false;
      bool foundBasisType = false;
      for (int i=0; i < metaObject.enumeratorCount(); ++i) {
          items.clear();
          QMetaEnum m = metaObject.enumerator(i);
          QString enumType = m.name();
          if (enumType == "DFTFunctionalType") {
              foundDFTFunctionalType = true;
              dftData->setEnumDFT(m);
              for (int j=0; j<m.keyCount();j++) {
                  items += QLatin1String(m.key(j));
              }
              ui.dftFunctionalCombo->addItems(items);
          } else if (enumType == "basisType") {
              foundBasisType = true;
              basicData->setEnumBasis(m);
              basisData->setEnumBasis(m);

              for (int j=0; j<m.keyCount();j++) {
                  items += QLatin1String(m.key(j));
                  items[j].replace("SV_P", "SV(P)");
                  items[j].replace("TZVP_F", "TZVP(-f)");
                  items[j].prepend("def2-");
              }
              ui.basicBasisSetCombo->addItems(items);

              ui.basisBasisSetCombo->addItems(items);

              for (int j=0; j<m.keyCount();j++) {
                  items[j].append("/C");
              }
              ui.basisAuxCorrBasisSetCombo->addItems(items);

              items.clear();
              items << "def2/J";
              ui.basisAuxBasisSetCombo->addItems(items);   // Only one aux-basisset available
          }
      }
      if (!foundDFTFunctionalType) {
          ui.dftFunctionalCombo->addItems(fallbackDFTFunctionalEntries());
      }
      if (!foundBasisType) {
          items = fallbackBasisEntries();
          ui.basicBasisSetCombo->addItems(items);
          ui.basisBasisSetCombo->addItems(items);
          for (QString &item : items)
              item.append("/C");
          ui.basisAuxCorrBasisSetCombo->addItems(items);
          ui.basisAuxBasisSetCombo->addItem("def2/J");
      }
      qInfo() << "OrcaInputDialog: initComboboxes end";
  }
  void OrcaInputDialog::initBasicData()
  {
      const QSignalBlocker b1(ui.basicCalculationCombo);
      const QSignalBlocker b2(ui.basicMethodCombo);
      const QSignalBlocker b3(ui.basicBasisSetCombo);
      const QSignalBlocker b4(ui.basicChargeSpin);
      const QSignalBlocker b5(ui.basicMultiplicitySpin);
      const QSignalBlocker b6(ui.basicFormatCombo);
      setComboIndexBounded(ui.basicCalculationCombo, basicData->getCalculation());
      setComboIndexBounded(ui.basicMethodCombo, basicData->getMethod() == DFT ? 1 : 0);
      setComboIndexBounded(ui.basicBasisSetCombo, basicData->getBasis ());
      ui.basicChargeSpin->setValue(basicData->getCharge());
      ui.basicMultiplicitySpin->setValue(basicData->getMultiplicity());
      setComboIndexBounded(ui.basicFormatCombo, basicData->getFormat());

  }

  void OrcaInputDialog::initBasisData()
  {
      const QSignalBlocker b1(ui.basisBasisSetCombo);
      const QSignalBlocker b2(ui.basisAuxBasisSetCombo);
      const QSignalBlocker b3(ui.basisAuxCorrBasisSetCombo);
      setComboIndexBounded(ui.basisBasisSetCombo, basisData->getBasis());
      setComboIndexBounded(ui.basisAuxBasisSetCombo, basisData->getAuxBasis());
      setComboIndexBounded(ui.basisAuxCorrBasisSetCombo, basisData->getAuxCorrBasis());
      ui.basisRelativisticGroup->hide();
//      ui.basisECPCheck->setChecked(basisData->EPCEnabled());
//      ui.basisAuxECPCheck->setChecked(basisData->auxEPCEnabled());
//      ui.basisAuxCorrECPCheck->setChecked(basisData->auxCorrEPCEnabled());

//      ui.basisRelativisticGroup->setChecked(basisData->relEnabled());
//      ui.basisRelativisticCombo->setEnabled(basisData->relEnabled());
//      ui.basisRelativisticCombo->setCurrentIndex(basisData->getRel());
//      ui.basisDKHSpin->setVisible(basisData->dkhEnabled());
//      ui.basisDKHLabel->setVisible(basisData->dkhEnabled());
  }

  void OrcaInputDialog::initControlData()
  {
      const QSignalBlocker b1(ui.controlChargeSpin);
      const QSignalBlocker b2(ui.controlMultiplicitySpin);
      const QSignalBlocker b3(ui.controlRunTypeCombo);
      const QSignalBlocker b4(ui.controlMethodCombo);
      ui.controlChargeSpin->setValue(controlData->getCharge());
      ui.controlMultiplicitySpin->setValue((controlData->getMultiplicity()));

      setComboIndexBounded(ui.controlRunTypeCombo, controlData->getCalculation());
      setComboIndexBounded(ui.controlMethodCombo, controlData->getMethod());
  }

  void OrcaInputDialog::initSCFData()
  {
      const QSignalBlocker b1(ui.scfAccCombo);
      const QSignalBlocker b2(ui.scfTypeCombo);
      const QSignalBlocker b3(ui.scfMaxIterSpin);
      setComboIndexBounded(ui.scfAccCombo, scfData->getAccuracy());
      setComboIndexBounded(ui.scfTypeCombo, scfData->getType());
      ui.scfMaxIterSpin->setValue(scfData->getMaxIter());

      ui.scfLevelShiftGroup->setChecked(scfData->levelShiftEnabled());
      ui.scfDampingGroup->setChecked(scfData->dampingEnabled());

      ui.scfLevelShiftDSpin->setValue(scfData->getLevelShift());
      ui.scfLevelErrorDSpin->setValue(scfData->getLevelError());

      ui.scfDampFactorDSpin->setValue(scfData->getDampFactor());
      ui.scfDampErrorDSpin->setValue(scfData->getDampError());

      if (m_scfConvButtons == NULL) {
          m_scfConvButtons = new QButtonGroup(this);
          m_scfConvButtons->addButton(ui.scfDIISRadio, 0);
          m_scfConvButtons->addButton(ui.scfKDIISRadio, 1);
      }
      if (m_scfConv2ndButtons == NULL) {
          m_scfConv2ndButtons = new QButtonGroup(this);
          m_scfConv2ndButtons->addButton(ui.scfSOSCFRadio, 0);
          m_scfConv2ndButtons->addButton(ui.scfNRSCFRadio, 1);
          m_scfConv2ndButtons->addButton(ui.scfAHSCFRadio, 2);
      }

      QRadioButton *convButton =
        qobject_cast<QRadioButton *>( m_scfConvButtons->button( scfData->getConv() ));

      if ( convButton ) {
          convButton->setChecked( true );
      }

      QRadioButton *conv2ndButton =
        qobject_cast<QRadioButton *>( m_scfConv2ndButtons->button(scfData->getConv2nd()) );

      if ( conv2ndButton ) {
          conv2ndButton->setChecked( true );
      }
  }

  void OrcaInputDialog::initDFTData()
  {
      const QSignalBlocker b1(ui.dispersionCombo);
      const QSignalBlocker b2(ui.dftFunctionalCombo);
      const QSignalBlocker b3(ui.tddftRootsSpin);
      const QSignalBlocker b4(ui.nmrCheck);
      setComboIndexBounded(ui.dispersionCombo, controlData->getDispersion());
      setComboIndexBounded(ui.dftFunctionalCombo, dftData->getDFTFunctional());
      ui.tddftRootsSpin->setValue(controlData->getTDDFTRoots());
      ui.nmrCheck->setChecked(controlData->nmrShieldingEnabled());
      ui.tddftRootsSpin->setEnabled(controlData->dftEnabled() && controlData->tddftEnabled());

  }

  void OrcaInputDialog::initSolvationData()
  {
      const QSignalBlocker b1(ui.solvationModelCombo);
      const QSignalBlocker b2(ui.solvationCombo);
      const QSignalBlocker b3(ui.cpcmGroup);
      const QSignalBlocker b4(ui.cpcmDRACOCheck);
      const QSignalBlocker b5(ui.cpcmEpsilonSpin);
      const QSignalBlocker b6(ui.cpcmRefracSpin);
      const QSignalBlocker b7(ui.cpcmRSolvSpin);
      const QSignalBlocker b8(ui.cpcmSurfaceTypeCombo);
      setComboIndexBounded(ui.solvationModelCombo, controlData->getSolvationModel());
      const int solventIndex =
        qMax(0, ui.solvationCombo->findText(controlData->getSolventName()));
      setComboIndexBounded(ui.solvationCombo, solventIndex);
      ui.cpcmGroup->setChecked(controlData->cpcmAdvancedEnabled());
      ui.cpcmDRACOCheck->setChecked(controlData->dracoEnabled());
      ui.cpcmEpsilonSpin->setValue(controlData->getCpcmEpsilon());
      ui.cpcmRefracSpin->setValue(controlData->getCpcmRefrac());
      ui.cpcmRSolvSpin->setValue(controlData->getCpcmRSolv());
      setComboIndexBounded(ui.cpcmSurfaceTypeCombo, controlData->getCpcmSurfaceType());
      ui.cpcmDRACOCheck->setEnabled(controlData->cpcmAdvancedEnabled());
      ui.cpcmEpsilonSpin->setEnabled(controlData->cpcmAdvancedEnabled());
      ui.cpcmRefracSpin->setEnabled(controlData->cpcmAdvancedEnabled());
      ui.cpcmRSolvSpin->setEnabled(controlData->cpcmAdvancedEnabled());
      ui.cpcmSurfaceTypeCombo->setEnabled(controlData->cpcmAdvancedEnabled());
  }

  void OrcaInputDialog::initResourcesData()
  {
      const QSignalBlocker b1(ui.nprocsCombo);
      const QSignalBlocker b2(ui.maxCoreCombo);
      const QSignalBlocker b3(ui.tddftCheck);
      const int nProcIndex = qMax(0, ui.nprocsCombo->findText(QString::number(controlData->getNProcs())));
      setComboIndexBounded(ui.nprocsCombo, nProcIndex);
      const int maxCoreIndex = qMax(0, ui.maxCoreCombo->findText(QString::number(controlData->getMaxCore())));
      setComboIndexBounded(ui.maxCoreCombo, maxCoreIndex);
      ui.tddftCheck->setChecked(controlData->tddftEnabled());

  }

  void OrcaInputDialog::initDataData()
  {
      const QSignalBlocker b1(ui.dataPrintCombo);
      const QSignalBlocker b2(ui.MOPrintCheck);
      const QSignalBlocker b3(ui.basisPrintCheck);
      setComboIndexBounded(ui.dataPrintCombo, dataData->getPrintLevel());
      if (dataData->MOPrintEnabled()) {
          ui.MOPrintCheck->setChecked(true);
      } else {
          ui.MOPrintCheck->setChecked(false);
      }
      if (dataData->basisPrintEnabled()) {
          ui.basisPrintCheck->setChecked(true);
      } else {
          ui.basisPrintCheck->setChecked(false);
      }
  }

  void OrcaInputDialog::updateAdvancedSetup()
  {
      initBasisData();
      initControlData();
      initSCFData();
      initDFTData();
      initSolvationData();
      initDataData();
      initResourcesData();

      QTreeWidgetItem *controlItem = ui.advancedTree->topLevelItem(1);
      controlItem->child(1)->setText(0, tr("Resources"));
      controlItem->child(2)->setText(0, tr("DFT"));
      controlItem->child(3)->setText(0, tr("Solvation"));
      controlItem->child(4)->setText(0, tr("TD-DFT"));

      ui.resourcesPage->setEnabled(true);
      controlItem->child(1)->setHidden(false);

      bool dftEnabled = controlData->dftEnabled();
      const bool solvationEnabled = dftEnabled || controlData->hfEnabled();
      const bool nmrCompatible = dftEnabled || controlData->hfEnabled();
      ui.dftOptionsPage->setEnabled( dftEnabled );
      controlItem->child(2)->setHidden(!dftEnabled);
      ui.solvationPage->setEnabled(solvationEnabled);
      controlItem->child(3)->setHidden(false);
      ui.tddftPage->setEnabled(dftEnabled);
      controlItem->child(4)->setHidden(!dftEnabled);
      ui.tddftCheck->setEnabled(dftEnabled);
      ui.tddftRootsSpin->setEnabled(dftEnabled && ui.tddftCheck->isChecked());
      ui.nmrCheck->setEnabled(nmrCompatible);
      const bool cpcmEnabled = solvationEnabled && controlData->getSolvationModel() != SOLV_MODEL_NONE;
      ui.solvationCombo->setEnabled(cpcmEnabled);
      ui.cpcmGroup->setEnabled(cpcmEnabled);
      ui.cpcmDRACOCheck->setEnabled(cpcmEnabled && controlData->cpcmAdvancedEnabled());
      ui.cpcmEpsilonSpin->setEnabled(cpcmEnabled && controlData->cpcmAdvancedEnabled());
      ui.cpcmRefracSpin->setEnabled(cpcmEnabled && controlData->cpcmAdvancedEnabled());
      ui.cpcmRSolvSpin->setEnabled(cpcmEnabled && controlData->cpcmAdvancedEnabled());
      ui.cpcmSurfaceTypeCombo->setEnabled(cpcmEnabled && controlData->cpcmAdvancedEnabled());

//      bool mp2Enabled = controlData->mp2Enabled();
//      ui.mp2Page->setEnabled( mp2Enabled );
//      controlItem->child(3)->setHidden(!mp2Enabled);

      updatePreviewText();
  }

  void OrcaInputDialog::updateBasicSetup()
  {
      initBasicData();
      updatePreviewText();
  }

  void OrcaInputDialog::setMode( int mode )
  {
      if (mode == 0) {
          m_basic = true;
          m_advanced = false;
          updateBasicSetup();
      } else if (mode == 1){
          m_basic = false;
          m_advanced = true;
          updateAdvancedSetup();
      }

    ui.modeTabWidget->setCurrentIndex( mode );
  }
  void OrcaInputDialog::setMolecule(Molecule *molecule)
  {
      qInfo() << "OrcaInputDialog: setMolecule start";
      // Disconnect the old molecule first...

      if (m_molecule)
        disconnect(m_molecule, 0, this, 0);

      m_molecule = molecule;
      m_pendingMoleculeSync = true;

      if (!m_molecule) {
          m_pendingMoleculeSync = false;
          updatePreviewText();
          qInfo() << "OrcaInputDialog: setMolecule end";
          return;
      }

      if (m_molecule){
          // Update the preview text whenever primitives are changed

          connect(m_molecule, SIGNAL(atomRemoved(Atom *)),
                  this, SLOT(updatePreviewText()));
          connect(m_molecule, SIGNAL(atomAdded(Atom *)),
                  this, SLOT(updatePreviewText()));
          connect(m_molecule, SIGNAL(atomUpdated(Atom *)),
                  this, SLOT(updatePreviewText()));
          if (!m_initializing && isVisible()) {
              applyMoleculeToUi();
              updatePreviewText();
          }
      }
      qInfo() << "OrcaInputDialog: setMolecule end";
  }

  void OrcaInputDialog::applyMoleculeToUi()
  {
      qInfo() << "OrcaInputDialog: applyMoleculeToUi start";
      if (m_initializing || !m_molecule) {
          qInfo() << "OrcaInputDialog: applyMoleculeToUi end";
          return;
      }
      if (!basicData || !controlData) {
          qInfo() << "OrcaInputDialog: applyMoleculeToUi end";
          return;
      }

      OpenBabel::OBMol obmol = m_molecule->OBMol();
      const int multiplicity = obmol.GetTotalSpinMultiplicity();
      const int charge = obmol.GetTotalCharge();

      basicData->setMultiplicity(multiplicity);
      controlData->setMultiplicity(multiplicity);
      basicData->setCharge(charge);
      controlData->setCharge(charge);

      {
          const QSignalBlocker b1(ui.basicMultiplicitySpin);
          const QSignalBlocker b2(ui.controlMultiplicitySpin);
          const QSignalBlocker b3(ui.basicChargeSpin);
          const QSignalBlocker b4(ui.controlChargeSpin);
          ui.basicMultiplicitySpin->setValue(multiplicity);
          ui.controlMultiplicitySpin->setValue(multiplicity);
          ui.basicChargeSpin->setValue(charge);
          ui.controlChargeSpin->setValue(charge);
      }

      m_pendingMoleculeSync = false;
      qInfo() << "OrcaInputDialog: applyMoleculeToUi end";
  }

  void OrcaInputDialog::resetClicked()
  {
      if (m_advanced) {
          basisData->reset();
          controlData->reset();
          scfData->reset();
          dftData->reset();
          dataData->reset();
          updateAdvancedSetup();
      } else {
          basicData->reset();
          updateBasicSetup();
      }

      updatePreviewText();
  }

  void OrcaInputDialog::generateClicked()
  {
    saveInputFile(ui.previewText->toPlainText(), tr("ORCA Input Deck"), QString("inp"));
  }

  void OrcaInputDialog::hideClicked()
  {
      // If the hide preview button is clicked : hide/show the preview text and change button label

      if (ui.previewText->isVisible()) {
        ui.previewText->hide();
        ui.hideButton->setText(tr("Show Preview"));

      }
      else {
          ui.previewText->show();
          ui.hideButton->setText(tr("Hide Preview"));
      }
  }

  void OrcaInputDialog::previewEdited()
  {
      qDebug() <<"ui.previewText->document " <<  ui.previewText->document()->isModified() << "\n";
  }

//
// Set Basic Widgets
//
  void OrcaInputDialog::setBasicComment()
  {
      basicData->setComment(ui.basicCommentLine->text());
      updateBasicSetup();
  }

  void OrcaInputDialog::setBasicCalculation(int n)
  {
      basicData->setCalculation(n);
      updateBasicSetup();
  }

  void OrcaInputDialog::setBasicMethod(int n)
  {
      basicData->setMethod(n == 1 ? DFT : HF);
      updateBasicSetup();
  }

  void OrcaInputDialog::setBasicBasis(int n)
  {
      basicData->setBasis(n);
      updateBasicSetup();
  }

  void OrcaInputDialog::setBasicMultiplicity(int n)
  {
      basicData->setMultiplicity(n);
      if (ui.basicMultiplicitySpin->value() != n) {
          ui.basicMultiplicitySpin->setValue(n);
      }
      updateBasicSetup();
  }

  void OrcaInputDialog::setBasicCharge(int n)
  {
      basicData->setCharge (n);
      updateBasicSetup();
  }

  void OrcaInputDialog::setBasicCoordsFormat(int n)
  {
      basicData->setFormat(n);
      updateBasicSetup();
  }
//
// Set Advanced Basis Widgets
//
  void OrcaInputDialog::setBasisBasisSet(int n)
  {
      basisData->setBasis(n);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setBasisAuxBasisSet(int n)
  {
      basisData->setAuxBasis(n);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setBasisAuxCorrBasisSet(int n)
  {
      basisData->setAuxCorrBasis(n);
      updateAdvancedSetup();
  }

//  void OrcaInputDialog::setBasisUseEPC(bool value)
//  {
//      basisData->setEPCChecked(value);
////      basisData->setAuxEPCChecked (value);
////      basisData->setAuxCorrEPCChecked (value);
//      updateAdvancedSetup();

//  }
//  void OrcaInputDialog::setBasisUseAuxEPC (bool value)
//  {
//      basisData->setAuxEPCChecked (value);
//      updateAdvancedSetup();
//  }

//  void OrcaInputDialog::setBasisUseAuxCorrEPC (bool value)
//  {
//      basisData->setAuxCorrEPCChecked (value);
//      updateAdvancedSetup();
//  }
//  void OrcaInputDialog::setBasisUseRel(bool value)
//  {
//      basisData->setRelChecked(value);
//      if (value) {
//          ui.basisRelativisticCombo->setEnabled(true);
//          if (basisData->dkhEnabled()) {
//              ui.basisDKHSpin->setVisible(true);
//              ui.basisDKHLabel->setVisible(true);
//          }
//      } else {
//          ui.basisDKHSpin->setVisible(false);
//          ui.basisDKHLabel->setVisible(false);
//      }
//      updateAdvancedSetup();
//  }

//  void OrcaInputDialog::setBasisRel(int n)
//  {
//      basisData->setRel(n);
//      if (n == DKH) {
//          basisData->setDKHChecked(true);
//          ui.basisDKHSpin->setVisible(true);
//          ui.basisDKHLabel->setVisible(true);
//      } else {
//          basisData->setDKHChecked(false);
//          ui.basisDKHSpin->setVisible(false);
//          ui.basisDKHLabel->setVisible(false);
//      }

//      updateAdvancedSetup();
//  }


//  void OrcaInputDialog::setBasisDKHOrder(int n)
//  {
//      basisData->setDKHOrder(n);
//      updateAdvancedSetup();
//  }

//
// Set Advanced Control Widgets
//

  void OrcaInputDialog::setControlCalculation(int n)
  {
      controlData->setCalculation(n);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setControlCharge(int n)
  {
      controlData->setCharge(n);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setControlMultiplicity(int n)
  {
      controlData->setMultiplicity(n);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setControlMethod(int n)
  {
      controlData->setMethod(n);
      const bool needsAuxC = controlData->mp2Enabled();
      ui.basisAuxCorrBasisSetCombo->setEnabled(needsAuxC);
      if (!controlData->dftEnabled()) {
          controlData->setTDDFTEnabled(false);
      }
      if (!(controlData->dftEnabled() || controlData->hfEnabled())) {
          controlData->setSolvationModel(SOLV_MODEL_NONE);
          controlData->setCpcmAdvancedEnabled(false);
      }
      if (controlData->mp2Enabled() || controlData->ccsdEnabled()) {
          controlData->setNMRShielding(false);
      }
      updateAdvancedSetup();
  }
//
// Advanced SCF Widgets
//
  void OrcaInputDialog::setSCFAccuracy(int n)
  {
      scfData->setAccuracy(n);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setSCFType(int n)
  {
      scfData->setType (n);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setSCFMaxIter(int n)
  {
      scfData->setMaxIter(n);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setSCFDampFactor(double f)
  {
      scfData->setDampFactor( f);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setSCFDampError(double f)
  {
      scfData->setDampError( f);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setSCFLevelShift(double f)
  {
      scfData->setLevelShift(f);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setSCFLevelError(double f)
  {
      scfData->setLevelError(f);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setSCFUseDamping(bool value)
  {
      scfData->setDampingChecked(value);
      if (value) {
          scfData->setDampFactor(ui.scfDampFactorDSpin->value());
          scfData->setDampError(ui.scfDampErrorDSpin->value());
      }
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setSCFUseLevelShift(bool value)
  {
      scfData->setLevelShiftChecked(value);
      if (value) {
          scfData->setLevelShift(ui.scfLevelShiftDSpin->value());
          scfData->setLevelError(ui.scfLevelErrorDSpin->value());
      }
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setSCFConverger(int n)
  {
      scfData->setConv(n);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setSCF2ndConverger(int n)
  {
      scfData->setConv2nd(n);
      updateAdvancedSetup();
  }

//
// Advanced DFT WIdgets
//

  void OrcaInputDialog::setSolvation(int n)
  {
      controlData->setSolventName(ui.solvationCombo->itemText(n));
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setSolvationModel(int n)
  {
      controlData->setSolvationModel(n);
      if (n == SOLV_MODEL_NONE) {
          controlData->setCpcmAdvancedEnabled(false);
      } else if (controlData->getSolventName().isEmpty()) {
          controlData->setSolventName(ui.solvationCombo->itemText(0));
      }
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setCpcmAdvancedEnabled(bool value)
  {
      controlData->setCpcmAdvancedEnabled(value);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setCpcmDRACO(bool value)
  {
      controlData->setDracoEnabled(value);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setCpcmEpsilon(double value)
  {
      controlData->setCpcmEpsilon(value);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setCpcmRefrac(double value)
  {
      controlData->setCpcmRefrac(value);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setCpcmRSolv(double value)
  {
      controlData->setCpcmRSolv(value);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setCpcmSurfaceType(int n)
  {
      controlData->setCpcmSurfaceType(n);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setDispersion(int n)
  {
      controlData->setDispersion(n);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setDFTFunctional(int n)
  {
      dftData->setDFTFunctional(n);
      updateAdvancedSetup();
  }
//
// Advanced resources / excited-state widgets
//
  void OrcaInputDialog::setResourcesMaxCore(int n)
  {
      controlData->setMaxCore(ui.maxCoreCombo->itemText(n).toInt());
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setResourcesNProcs(int n)
  {
      controlData->setNProcs(ui.nprocsCombo->itemText(n).toInt());
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setTDDFTEnabled(bool value)
  {
      controlData->setTDDFTEnabled(value);
      ui.tddftRootsSpin->setEnabled(value && controlData->dftEnabled());
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setTDDFTRoots(int n)
  {
      controlData->setTDDFTRoots(n);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setNMRShielding(bool value)
  {
      controlData->setNMRShielding(value);
      updateAdvancedSetup();
  }

//
// Set Advanced Data Widgets
//
  void OrcaInputDialog::setDataFormat(int n)
  {
      dataData->setFormat(n);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setDataComment()
  {
      dataData->setComment(ui.dataCommentLine->text());
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setPrintLevel(int n)
  {
      dataData->setPrintLvl(n);
      updateAdvancedSetup();
  }
  void OrcaInputDialog::setMOPrint(bool value)
  {
      dataData->setMOPrintChecked(value);
      updateAdvancedSetup();
  }

  void OrcaInputDialog::setBasisPrint(bool value)
  {
      dataData->setBasisPrintChecked(value);
      updateAdvancedSetup();
  }
//
// Handle PreviewText and Output
//
  void OrcaInputDialog::updatePreviewText ()
  {
      qInfo() << "OrcaInputDialog: updatePreviewText start";
      if (m_initializing) {
          qInfo() << "OrcaInputDialog: updatePreviewText end";
          return;
      }
      if (!isVisible()) {
          qInfo() << "OrcaInputDialog: updatePreviewText end";
          return;
      }
      if (!basicData || !basisData || !controlData || !dataData || !scfData ||
          !dftData) {
          qInfo() << "OrcaInputDialog: updatePreviewText end";
          return;
      }

      // Generate the input deck and display it

      if (ui.previewText->document()->isModified()) {

          QMessageBox msgBox;

          msgBox.setWindowTitle(tr("OrcaExtension Warning"));
          msgBox.setText(tr("Unsaved changes are made in the actual preview text! \n Generating a new preview will lose all changes! \n Would you like to override them anyway?"));
          msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

          switch (msgBox.exec()) {

          case QMessageBox::Yes:
              // yes was clicked
              ui.previewText->setText(generateInputDeck());
              ui.previewText->document()->setModified(false);
              break;
          case QMessageBox::No:
              // no was clicked
              break;
          default:
              // should never be reached
              break;
          }
      } else {
          ui.previewText->setText(generateInputDeck());
          ui.previewText->document()->setModified(false);
      }
      qInfo() << "OrcaInputDialog: updatePreviewText end";
  }

  QString OrcaInputDialog::generateInputDeck()
  {
      qInfo() << "OrcaInputDialog: generateInputDeck start";
      // Generate an input deck based on the settings of the dialog

      QString buffer;
      QTextStream mol(&buffer);

      if (!basicData || !basisData || !controlData || !dataData || !scfData ||
          !dftData) {
          qInfo() << "OrcaInputDialog: generateInputDeck end";
          return buffer;
      }

      int charge, multiplicity;
      QString comment;
      QStringList tokens;

      if (m_basic){
          charge = basicData->getCharge();
          multiplicity = basicData->getMultiplicity();
          comment = basicData->getComment();
          if (basicData->getMethod() == DFT) {
              tokens << "PBE0";
          } else {
              tokens << safeHFReference(multiplicity);
          }
          tokens << basicData->getBasisTxt();
          const QString calc = basicData->getCalculationTxt();
          if (calc != "SP")
              tokens << calc;
          if (basicData->getMethod() == DFT) {
              tokens << "D4";
          }
      } else {
          charge = controlData->getCharge();
          multiplicity = controlData->getMultiplicity();
          comment = dataData->getComment();
          if (controlData->dftEnabled()) {
              tokens << dftData->getDFTFunctionalTxt();
          } else if ( controlData->mp2Enabled()) {
              tokens << "RI-MP2";
          } else if ( controlData->ccsdEnabled()) {
              tokens << "CCSD";
          } else {
              tokens << safeHFReference(multiplicity);
          }
          tokens << basisData->getBasisTxt();
          const QString calc = controlData->getCalculationTxt();
          if (calc != "SP")
              tokens << calc;
          if (scfData->getAccuracy() != NORMALSCF)
              tokens << scfData->getAccuracyTxt();
          if (controlData->dftEnabled()) {
              const QString disp = controlData->getDispersionTxt();
              if (!disp.isEmpty())
                  tokens << disp;
          }
          const QString solv = controlData->getSolvationTxt();
          if (!solv.isEmpty())
              tokens << solv;
          if (shouldEmitDracoToken())
              tokens << "DRACO";
          const bool nmrCompatible = controlData->dftEnabled() ||
            (!controlData->mp2Enabled() && !controlData->ccsdEnabled());
          if (controlData->nmrShieldingEnabled() && nmrCompatible)
              tokens << "NMR";
          if (needsAuxCBasis()) {
              tokens << basisData->getAuxCorrBasisTxt();
          }
      }
      mol << "# avogadro generated ORCA input file\n";
      if (!comment.trimmed().isEmpty())
          mol << "# " << comment << "\n";
      mol << "! " << tokens.join(" ").trimmed() << "\n";

      if (!m_basic) {
          if (shouldEmitPalBlock()) {
              mol << "%pal\n  nprocs " << controlData->getNProcs() << "\nend\n";
          }
          if (shouldEmitMaxCore()) {
              mol << "%maxcore " << controlData->getMaxCore() << "\n";
          }
          if (controlData->tddftEnabled() && controlData->dftEnabled()) {
              mol << "%tddft\n  NRoots " << controlData->getTDDFTRoots() << "\nend\n";
          }
          if (shouldEmitSolvationBlock()) {
              mol << "%cpcm\n";
              if (controlData->getSolvationModel() == SOLV_MODEL_SMD) {
                  mol << "  smd true\n";
                  mol << "  SMDsolvent \"" << controlData->getSolventTokenTxt() << "\"\n";
              }
              if (controlData->dracoEnabled()) {
                  mol << "  draco true\n";
              }
              if (controlData->usesCpcmEpsilon()) {
                  mol << "  epsilon " << controlData->getCpcmEpsilon() << "\n";
              }
              if (controlData->usesCpcmRefrac()) {
                  mol << "  refrac " << controlData->getCpcmRefrac() << "\n";
              }
              if (controlData->usesCpcmRSolv()) {
                  mol << "  rsolv " << controlData->getCpcmRSolv() << "\n";
              }
              const QString surfaceType = controlData->getCpcmSurfaceTypeTxt();
              if (!surfaceType.isEmpty()) {
                  mol << "  surfacetype " << surfaceType << "\n";
              }
              mol << "end\n";
          }
          if (shouldEmitSCFBlock()) {
              mol << "%scf\n  MaxIter " << scfData->getMaxIter() << "\nend\n";
          }
      }
      mol << "\n";

      // Coordinates
      coordType formatCheck;
      if (m_basic) {
          formatCheck = basicData->getFormat();
      } else {
          formatCheck = dataData->getFormat();
      }
      if (m_molecule && (formatCheck == CARTESIAN)) {


          // Cartesian coordinates

          QTextStream mol(&buffer);

          mol << "* xyz " << charge << " " << multiplicity << "\n";

          QList<Atom *> atoms = m_molecule->atoms();

          foreach (Atom *atom, atoms) {
              mol << qSetFieldWidth(4) << Qt::right
                  << QString(OpenBabel::OBElements::GetSymbol(atom->atomicNumber()))
                  << qSetFieldWidth(15) << qSetRealNumberPrecision(5) << Qt::forcepoint
                  << Qt::fixed << Qt::right << atom->pos()->x() << atom->pos()->y()
                  << atom->pos()->z()
                  << qSetFieldWidth(0) << '\n';
          }
          mol << "*\n";

//      } else if (m_molecule && (formatCheck == ZMATRIX)) {

//          // Z-matrix

//          QTextStream mol(&buffer);
//          mol.setFieldAlignment(QTextStream::AlignAccountingStyle);
//          mol << "*int " << charge << " " << multiplicity << "\n";
//          OBAtom *a, *b, *c;
//          double r, w, t;

//          /* Taken from OpenBabel's gzmat file format converter */
//          std::vector<OBInternalCoord*> vic;
//          vic.push_back((OpenBabel::OBInternalCoord*)NULL);
//          OpenBabel::OBMol obmol = m_molecule->OBMol();
//          FOR_ATOMS_OF_MOL(atom, &obmol)
//                  vic.push_back(new OpenBabel::OBInternalCoord);

//          CartesianToInternal(vic, obmol);

//          FOR_ATOMS_OF_MOL(atom, &obmol)
//          {
//              a = vic[atom->GetIdx()]->_a;
//              b = vic[atom->GetIdx()]->_b;
//              c = vic[atom->GetIdx()]->_c;

//              mol << qSetFieldWidth(3) << QString(etab.GetSymbol(atom->GetAtomicNum()));

//              if (atom->GetIdx() > 1)
//                  mol << qSetFieldWidth(0) << "  " << qSetFieldWidth(3) << QString::number(a->GetIdx())
//                      << qSetFieldWidth(0) << "  "<< qSetFieldWidth(4) << QString("r") + QString::number(atom->GetIdx());

//              if (atom->GetIdx() > 2)
//                  mol << qSetFieldWidth(0) << "  " << qSetFieldWidth(3) << QString::number(b->GetIdx())
//                      << qSetFieldWidth(0) << "  "<< qSetFieldWidth(4) << QString("a") + QString::number(atom->GetIdx());

//              if (atom->GetIdx() > 3)
//                  mol << qSetFieldWidth(0) << "  " << qSetFieldWidth(3) << QString::number(c->GetIdx())
//                      << qSetFieldWidth(0) << "  "<< qSetFieldWidth(4) << QString("d") + QString::number(atom->GetIdx());

//              mol << qSetFieldWidth(0) << '\n';
//          }

//          mol << " variables\n";
//          FOR_ATOMS_OF_MOL(atom, &obmol)
//          {
//              r = vic[atom->GetIdx()]->_dst;
//              w = vic[atom->GetIdx()]->_ang;
//              if (w < 0.0)
//                  w += 360.0;
//              t = vic[atom->GetIdx()]->_tor;
//              if (t < 0.0)
//                  t += 360.0;
//              if (atom->GetIdx() > 1)
//                  mol << "   r" << atom->GetIdx() << qSetFieldWidth(15)
//                      << qSetRealNumberPrecision(5) << forcepoint << fixed << right
//                      << r << qSetFieldWidth(0) << '\n';
//              if (atom->GetIdx() > 2)
//                  mol << "   a" << atom->GetIdx() << qSetFieldWidth(15)
//                      << qSetRealNumberPrecision(5) << forcepoint << fixed << right
//                      << w << qSetFieldWidth(0) << '\n';
//              if (atom->GetIdx() > 3)
//                  mol << "   d" << atom->GetIdx() << qSetFieldWidth(15)
//                      << qSetRealNumberPrecision(5) << forcepoint << fixed << right
//                      << t << qSetFieldWidth(0) << '\n';
//          }
//          mol << " end\n";
//          foreach (OpenBabel::OBInternalCoord *c, vic)
//              delete c;
      } else if (m_molecule && (formatCheck == INTERNAL_COORDS)) {
          // Internal coordinates

          QTextStream mol(&buffer);

          mol << "* int " << charge << " " << multiplicity << "\n";

          OBAtom *a, *b, *c;
          double r, w, t;

          /* Taken from OpenBabel's gzmat file format converter */
          std::vector<OBInternalCoord*> vic;
          vic.push_back((OpenBabel::OBInternalCoord*)NULL);
          OpenBabel::OBMol obmol = m_molecule->OBMol();
          FOR_ATOMS_OF_MOL(atom, &obmol)
                  vic.push_back(new OpenBabel::OBInternalCoord);
          CartesianToInternal(vic, obmol);

          FOR_ATOMS_OF_MOL(atom, &obmol)
          {
              a = vic[atom->GetIdx()]->_a;
              b = vic[atom->GetIdx()]->_b;
              c = vic[atom->GetIdx()]->_c;
              r = vic[atom->GetIdx()]->_dst;
              w = vic[atom->GetIdx()]->_ang;
              if (w < 0.0)
                  w += 360.0;
              t = vic[atom->GetIdx()]->_tor;
              if (t < 0.0)
                  t += 360.0;

              mol << qSetFieldWidth(4) << Qt::right
                  << QString(OpenBabel::OBElements::GetSymbol(atom->GetAtomicNum()));

              if (a) {
                  mol << qSetFieldWidth(6) << Qt::right << QString::number(a->GetIdx());
              } else {
                  mol << qSetFieldWidth(6) << Qt::right << "0" ;
              }
              if (b) {
                  mol << qSetFieldWidth(6) << Qt::right << QString::number(b->GetIdx());
              } else {
                  mol << qSetFieldWidth(6) << Qt::right << "0" ;
              }
              if (c) {
                  mol << qSetFieldWidth(6) << Qt::right << QString::number(c->GetIdx());
              } else {
                  mol << qSetFieldWidth(6) << Qt::right << "0" ;
              }
              mol << qSetFieldWidth(15) << qSetRealNumberPrecision(5) << Qt::forcepoint << Qt::fixed << Qt::right << r;
              mol << qSetFieldWidth(15) << qSetRealNumberPrecision(5) << Qt::forcepoint << Qt::fixed << Qt::right << w;
              mol << qSetFieldWidth(15) << qSetRealNumberPrecision(5) << Qt::forcepoint << Qt::fixed << Qt::right << t;

              mol << qSetFieldWidth(0) << '\n';
          }
          mol << "*\n";

          foreach (OpenBabel::OBInternalCoord *c, vic)
              delete c;

      } else if (m_molecule && (formatCheck == ZMATRIX_COMPACT)) {
//          // Compact ZMatrix

          QTextStream mol(&buffer);

          mol << "* gzmt " << charge << " " << multiplicity << "\n";

          OBAtom *a, *b, *c;
          double r, w, t;

          /* Taken from OpenBabel's gzmat file format converter */
          std::vector<OBInternalCoord*> vic;
          vic.push_back((OpenBabel::OBInternalCoord*)NULL);
          OpenBabel::OBMol obmol = m_molecule->OBMol();
          FOR_ATOMS_OF_MOL(atom, &obmol)
                  vic.push_back(new OpenBabel::OBInternalCoord);
          CartesianToInternal(vic, obmol);

          FOR_ATOMS_OF_MOL(atom, &obmol)
          {
              a = vic[atom->GetIdx()]->_a;
              b = vic[atom->GetIdx()]->_b;
              c = vic[atom->GetIdx()]->_c;
              r = vic[atom->GetIdx()]->_dst;
              w = vic[atom->GetIdx()]->_ang;
              if (w < 0.0)
                  w += 360.0;
              t = vic[atom->GetIdx()]->_tor;
              if (t < 0.0)
                  t += 360.0;

              mol << qSetFieldWidth(4) << Qt::right
                  << QString(OpenBabel::OBElements::GetSymbol(atom->GetAtomicNum()));
              if (atom->GetIdx() > 1)
                  mol << qSetFieldWidth(6) << Qt::right
                      << QString::number(a->GetIdx()) << qSetFieldWidth(15)
                      << qSetRealNumberPrecision(5) << Qt::forcepoint << Qt::fixed << Qt::right << r;
              if (atom->GetIdx() > 2)
                  mol << qSetFieldWidth(6) << Qt::right
                      << QString::number(b->GetIdx()) << qSetFieldWidth(15)
                      << qSetRealNumberPrecision(5) << Qt::forcepoint << Qt::fixed << Qt::right << w;
              if (atom->GetIdx() > 3)
                  mol << qSetFieldWidth(6) << Qt::right
                      << QString::number(c->GetIdx()) << qSetFieldWidth(15)
                      << qSetRealNumberPrecision(5) << Qt::forcepoint << Qt::fixed << Qt::right << t;
              mol << qSetFieldWidth(0) << '\n';
          }
          mol << "*\n";

          foreach (OpenBabel::OBInternalCoord *c, vic)
              delete c;
      }


      mol << Qt::endl;

      qInfo() << "OrcaInputDialog: generateInputDeck end";
      return buffer;
  }

  bool OrcaInputDialog::needsAuxCBasis() const
  {
      return !m_basic && controlData->mp2Enabled();
  }

  bool OrcaInputDialog::shouldEmitSCFBlock() const
  {
      return !m_basic && scfData->getMaxIter() != 125;
  }

  bool OrcaInputDialog::shouldEmitPalBlock() const
  {
      return !m_basic && controlData->usesNProcs();
  }

  bool OrcaInputDialog::shouldEmitMaxCore() const
  {
      return !m_basic && controlData->usesMaxCore();
  }

  bool OrcaInputDialog::shouldEmitSolvationBlock() const
  {
      if (m_basic || !controlData->cpcmAdvancedEnabled())
          return false;
      if (controlData->getSolvationModel() == SOLV_MODEL_NONE)
          return false;
      return controlData->usesCpcmEpsilon() || controlData->usesCpcmRefrac() ||
             controlData->usesCpcmRSolv() ||
             controlData->getCpcmSurfaceType() != CPCM_SURFACE_DEFAULT ||
             (controlData->dracoEnabled() && !shouldEmitDracoToken());
  }

  bool OrcaInputDialog::shouldEmitDracoToken() const
  {
      if (m_basic || !controlData->dracoEnabled())
          return false;
      if (controlData->getSolvationModel() == SOLV_MODEL_NONE)
          return false;
      if (!controlData->cpcmAdvancedEnabled())
          return true;
      return !(controlData->usesCpcmEpsilon() || controlData->usesCpcmRefrac() ||
               controlData->usesCpcmRSolv() ||
               controlData->getCpcmSurfaceType() != CPCM_SURFACE_DEFAULT);
  }

  QString OrcaInputDialog::safeHFReference(int multiplicity) const
  {
      if (multiplicity > 1) {
          return "UHF";
      }
      return "RHF";
  }

  QString OrcaInputDialog::saveInputFile(QString inputDeck, QString fileType, QString ext)
  {
    // Try to set default save path for dialog using the next sequence:
    //  1) directory of current file (if any);
    //  2) directory where previous deck was saved;
    //  3) $HOME

    QFileInfo defaultFile;
    if (m_molecule)
      defaultFile.setFile(m_molecule->fileName());
    QString defaultPath = defaultFile.canonicalPath();
    if(m_savePath == "") {
      if (defaultPath.isEmpty())
        defaultPath = QDir::homePath();
    } else {
      defaultPath = m_savePath;
    }

    QString defaultBaseName = defaultFile.baseName();
    if (defaultBaseName.isEmpty())
      defaultBaseName = tr("orca-input");
    QString defaultFileName = defaultPath + '/' + defaultBaseName + "." + ext;
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Input Deck"),
        defaultFileName, fileType + " (*." + ext + ")");

    if(fileName == "")
      return fileName;

    QFile file(fileName);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();

    file.write(inputDeck.toLocal8Bit()); // prevent troubles in Windows
    file.close(); // flush buffer!
    m_savePath = QFileInfo(file).absolutePath();
    return fileName;
  }

  void OrcaInputDialog::readSettings(QSettings& settings)
  {
    m_savePath = settings.value("orca/savepath").toString();
  }

  void OrcaInputDialog::writeSettings(QSettings& settings) const
  {
    settings.setValue("orca/savepath", m_savePath);
  }
}

// This includes the files generated by Qt's moc at compile time to
// ensure that signals/slots work. If you ever see errors about
// missing vtables with gcc, check that you haven't forgotten one of
// these:
//#include "orcainputdialog.moc"
