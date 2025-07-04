/**********************************************************************
  LammpsInputDialog - Dialog for generating LAMMPS input files

  Copyright (C) 2012 Albert DeFusco

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

#include "lammpsinputdialog.h"

#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/bond.h>

#include <openbabel/mol.h>
#include <openbabel/atom.h>
#include <openbabel/elements.h>

#include <QString>
#include <QTextStream>
#include <QMessageBox>
#include <QDebug>
#include <QHash>


using namespace OpenBabel;
using namespace std;

namespace Avogadro
{
  LammpsInputDialog::LammpsInputDialog(QWidget *parent, Qt::WindowFlags f)
    : InputDialog(parent, f),

    m_unitType(real),
    m_dimensionType(d3),
    m_xBoundaryType(p),
    m_yBoundaryType(p),
    m_zBoundaryType(p),

    m_atomStyle(full),

    m_waterPotential(NONE),

    m_ensemble(NVT),
    m_temperature(298.15),
    m_nhChain(1),

    m_timeStep(2.0),
    m_runSteps(50),
    m_xReplicate(1),
    m_yReplicate(1),
    m_zReplicate(1),

    m_dumpStep(1),

    m_velocityDist(gaussian),
    m_velocityTemp(298.15),
    m_zeroMOM(true),
    m_zeroL(true),
    m_thermoStyle(one),
    m_thermoInterval(50),

    m_output(),  m_dirty(false), m_warned(false), readData(false)
  {
    ui.setupUi(this);
    // Connect the GUI elements to the correct slots
    connect(ui.titleLine, SIGNAL(editingFinished()),
        this, SLOT(setTitle()));

    //now for something useful
    connect(ui.unitsCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setUnits(int)));
    connect(ui.atomStyleCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setAtomStyle(int)));
    connect(ui.dimensionCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setDimensionType(int)));
    connect(ui.xBoundaryCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setXBoundaryType(int)));
    connect(ui.yBoundaryCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setYBoundaryType(int)));
    connect(ui.zBoundaryCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setZBoundaryType(int)));
    connect(ui.waterPotentialCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setWaterPotential(int)));
    connect(ui.readDataLine, SIGNAL(editingFinished()),
        this, SLOT(setReadData()));
    connect(ui.ensembleCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setEnsemble(int)));
    connect(ui.tempSpin, SIGNAL(valueChanged(double)),
        this, SLOT(setTemperature(double)));
    connect(ui.nhChainSpin, SIGNAL(valueChanged(int)),
        this, SLOT(setNHChain(int)));
    connect(ui.stepSpin, SIGNAL(valueChanged(double)),
        this, SLOT(setTimeStep(double)));
    connect(ui.runSpin, SIGNAL(valueChanged(int)),
        this, SLOT(setRunSteps(int)));
    connect(ui.xReplicateSpin, SIGNAL(valueChanged(int)),
        this, SLOT(setXReplicate(int)));
    connect(ui.yReplicateSpin, SIGNAL(valueChanged(int)),
        this, SLOT(setYReplicate(int)));
    connect(ui.zReplicateSpin, SIGNAL(valueChanged(int)),
        this, SLOT(setZReplicate(int)));
    connect(ui.dumpXYZEdit, SIGNAL(editingFinished()),
        this, SLOT(setDumpXYZ()));
    connect(ui.dumpStepSpin, SIGNAL(valueChanged(int)),
        this, SLOT(setDumpStep(int)));
    connect(ui.velocityDistCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setVelocityDist(int)));
    connect(ui.velocityTempSpin, SIGNAL(valueChanged(double)),
        this, SLOT(setVelocityTemp(double)));
    connect(ui.zeroMOMCheck, SIGNAL(toggled(bool)),
        this, SLOT(setZeroMOM(bool)));
    connect(ui.zeroLCheck, SIGNAL(toggled(bool)),
        this, SLOT(setZeroL(bool)));
    connect(ui.thermoStyleCombo, SIGNAL(currentIndexChanged(int)),
        this, SLOT(setThermoStyle(int)));
    connect(ui.thermoSpin, SIGNAL(valueChanged(int)),
        this, SLOT(setThermoInterval(int)));

    connect(ui.previewText, SIGNAL(cursorPositionChanged()),
        this, SLOT(previewEdited()));
    connect(ui.generateButton, SIGNAL(clicked()),
        this, SLOT(generateClicked()));
    connect(ui.resetButton, SIGNAL(clicked()),
        this, SLOT(resetClicked()));

    connect(ui.moreButton, SIGNAL(clicked()),
        this, SLOT(moreClicked()));
    connect(ui.enableFormButton, SIGNAL(clicked()),
        this, SLOT(enableFormClicked()));

    QSettings settings;
    readSettings(settings);

    // Generate an initial preview of the input deck
    updatePreviewText();
  }

  LammpsInputDialog::~LammpsInputDialog()
  {
    QSettings settings;
    writeSettings(settings);
  }


  void LammpsInputDialog::showEvent(QShowEvent *)
  {
    updatePreviewText();
  }

  void LammpsInputDialog::updatePreviewText()
  {
    if (!isVisible())
      return;
    // Generate the input deck and display it
    if (m_dirty && !m_warned) {
      m_warned = true;
      QMessageBox msgBox;

      msgBox.setWindowTitle(tr("Lammps Input Deck Generator Warning"));
      msgBox.setText(tr("Would you like to update the preview text, losing all changes made in the Lammps input deck preview pane?"));
      msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

      switch (msgBox.exec()) {
        case QMessageBox::Yes:
          // yes was clicked
          deckDirty(false);
          ui.previewText->setText(generateInputDeck());
          ui.previewText->document()->setModified(false);
          m_warned = false;
          break;
        case QMessageBox::No:
          // no was clicked
          m_warned = false;
          break;
        default:
          // should never be reached
          break;
      }
    }
    else if (!m_dirty) {
      ui.previewText->setText(generateInputDeck());
      ui.previewText->document()->setModified(false);
    }
  }

  void LammpsInputDialog::resetClicked()
  {
    // Reset the form to defaults
    deckDirty(false);

    ui.unitsCombo->setCurrentIndex(1);
    ui.atomStyleCombo->setCurrentIndex(7);
    ui.dimensionCombo->setCurrentIndex(1);
    ui.xBoundaryCombo->setCurrentIndex(0);
    ui.yBoundaryCombo->setCurrentIndex(0);
    ui.zBoundaryCombo->setCurrentIndex(0);
    ui.waterPotentialCombo->setCurrentIndex(0);
    ui.ensembleCombo->setCurrentIndex(0);
    ui.tempSpin->setValue(298.15);
    ui.nhChainSpin->setValue(1);
    ui.stepSpin->setValue(2.0);
    ui.runSpin->setValue(50);
    ui.xReplicateSpin->setValue(1);
    ui.yReplicateSpin->setValue(1);
    ui.zReplicateSpin->setValue(1);
    ui.dumpStepSpin->setValue(1);
    ui.thermoStyleCombo->setCurrentIndex(0);
    ui.thermoSpin->setValue(50);


    ui.previewText->setText(generateInputDeck());
    ui.previewText->document()->setModified(false);
  }

  void LammpsInputDialog::generateClicked()
  {
    saveInputFile(ui.previewText->toPlainText(), tr("Lammps Input"), QString("lmp"));
  }

  void LammpsInputDialog::moreClicked()
  {
    // If the more button is clicked hide/show the preview text
    if (ui.previewText->isVisible()) {
      ui.previewText->hide();
      ui.moreButton->setText(tr("Show Preview"));
    }
    else {
      ui.previewText->show();
      ui.moreButton->setText(tr("Hide Preview"));
    }
  }

  void LammpsInputDialog::enableFormClicked()
  {
    updatePreviewText();
  }

  void LammpsInputDialog::previewEdited()
  {
    // Determine if the preview text has changed from the form generated
    if(ui.previewText->document()->isModified())
      deckDirty(true);
  }

  void LammpsInputDialog::setTitle()
  {
    m_title = ui.titleLine->text();
    updatePreviewText();
  }

  void LammpsInputDialog::setUnits(int n)
  {
    m_unitType = (LammpsInputDialog::unitType) n;
    ui.unitsCombo->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setAtomStyle(int n)
  {
    m_atomStyle = (LammpsInputDialog::atomStyle) n;
    ui.atomStyleCombo->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setDimensionType(int n)
  {
    m_dimensionType = static_cast<LammpsInputDialog::dimensionType>(n);
    ui.dimensionCombo->setEnabled(true);
    if (n == 0) {
      setZBoundaryType(0);
      ui.zBoundaryCombo->setCurrentIndex(0);
      ui.zBoundaryCombo->setEnabled(false);
      ui.zReplicateSpin->setValue(1);
      ui.zReplicateSpin->setEnabled(false);
    }
    if (n == 1) {
      ui.zBoundaryCombo->setEnabled(true);
      ui.zReplicateSpin->setEnabled(true);
    }
    updatePreviewText();
  }

  void LammpsInputDialog::setXBoundaryType(int n)
  {
    m_xBoundaryType = static_cast<LammpsInputDialog::boundaryType>(n);
    ui.xBoundaryCombo->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setYBoundaryType(int n)
  {
    m_yBoundaryType = static_cast<LammpsInputDialog::boundaryType>(n);
    ui.yBoundaryCombo->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setZBoundaryType(int n)
  {
    m_zBoundaryType = static_cast<LammpsInputDialog::boundaryType>(n);
    //should be careful here
    //z boundary must be p for 2d!!!
    ui.zBoundaryCombo->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setWaterPotential(int n)
  {
    m_waterPotential = static_cast<LammpsInputDialog::waterPotential>(n);
    ui.waterPotentialCombo->setEnabled(true);
    if (n == 1) {
      setAtomStyle(7);
      ui.atomStyleCombo->setCurrentIndex(7);
      ui.atomStyleCombo->setEnabled(false);
    }
    if (n == 0) {
      ui.atomStyleCombo->setEnabled(true);
    }
    updatePreviewText();
  }

  void LammpsInputDialog::setReadData()
  {
    m_readData = ui.readDataLine->text();
    if (m_readData != "" )
      readData = true;
    else
      readData = false;
    updatePreviewText();
  }

  void LammpsInputDialog::setMolecule(Molecule *molecule)
  {
    // Disconnect the old molecule first...
    if (m_molecule) {
      disconnect(m_molecule, 0, this, 0);
    }

    m_molecule = molecule;
    // Update the preview text whenever primitives are changed
    connect(m_molecule, SIGNAL(atomRemoved(Atom *)),
        this, SLOT(updatePreviewText()));
    connect(m_molecule, SIGNAL(atomAdded(Atom *)),
        this, SLOT(updatePreviewText()));
    connect(m_molecule, SIGNAL(atomUpdated(Atom *)),
        this, SLOT(updatePreviewText()));
    updatePreviewText();
  }

  void LammpsInputDialog::setEnsemble(int n)
  {
    m_ensemble = static_cast<LammpsInputDialog::ensemble>(n);
    ui.ensembleCombo->setEnabled(true);
    if (n == 1) {
      ui.tempSpin->setValue(0.0);
      ui.tempSpin->setEnabled(false);
      ui.nhChainSpin->setValue(0);
      ui.nhChainSpin->setEnabled(false);
    }
    else if (n == 0) {
      ui.tempSpin->setEnabled(true);
      ui.nhChainSpin->setEnabled(true);
      ui.nhChainSpin->setValue(1);
    }
    updatePreviewText();
  }

  void LammpsInputDialog::setTemperature(double n)
  {
    m_temperature = n;
    ui.tempSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setNHChain(int n)
  {
    m_nhChain = n;
    ui.nhChainSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setTimeStep(double n)
  {
    m_timeStep = n;
    ui.stepSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setRunSteps(int n)
  {
    m_runSteps = n;
    ui.runSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setXReplicate(int n)
  {
    m_xReplicate = n;
    ui.xReplicateSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setYReplicate(int n)
  {
    m_yReplicate = n;
    ui.yReplicateSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setZReplicate(int n)
  {
    m_zReplicate = n;
    ui.zReplicateSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setDumpStep(int n)
  {
    m_dumpStep = n;
    ui.dumpStepSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setDumpXYZ()
  {
    m_dumpXYZ = ui.dumpXYZEdit->text();
    updatePreviewText();
  }

  void LammpsInputDialog::setVelocityDist(int n)
  {
    m_velocityDist = static_cast<LammpsInputDialog::velocityDist>(n);
    ui.velocityDistCombo->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setVelocityTemp(double n)
  {
    m_velocityTemp = n;
    ui.velocityTempSpin->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setZeroMOM(bool state)
  {
    m_zeroMOM = state;
    ui.zeroMOMCheck->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setZeroL(bool state)
  {
    m_zeroL = state;
    ui.zeroLCheck->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setThermoStyle(int n)
  {
    m_thermoStyle = static_cast<LammpsInputDialog::thermoStyle>(n);
    ui.thermoStyleCombo->setEnabled(true);
    updatePreviewText();
  }

  void LammpsInputDialog::setThermoInterval(int n)
  {
    m_thermoInterval = n;
    ui.thermoSpin->setEnabled(true);
    updatePreviewText();
  }

  QString LammpsInputDialog::generateInputDeck()
  {
    // Generate an input deck based on the settings of the dialog
    QString buffer;
    QTextStream mol(&buffer);

    mol << "#LAMMPS Input file generated by Avogadro\n";
    mol << "# " << m_title << "\n\n";

    mol << "# Intialization\n";
    mol << "units          " << getUnitType(m_unitType) << "\n";
    mol << "dimension      " << getDimensionType(m_dimensionType) << "\n";
    mol << "boundary       "
      << getXBoundaryType(m_xBoundaryType) << " "
      << getYBoundaryType(m_yBoundaryType) << " "
      << getZBoundaryType(m_zBoundaryType) << "\n";
    mol << "atom_style     " << getAtomStyle(m_atomStyle) << "\n";
    mol << "\n";

    mol << "# Atom Definition\n";
    if (readData)
      mol << "read_data      " << m_readData << "\n";
    mol << "replicate      "
      << m_xReplicate << " "
      << m_yReplicate << " "
      << m_zReplicate << "\n";

    mol << "\n" << getWaterPotential(m_waterPotential) << "\n";

    mol << "# Settings\n";
    mol << "velocity       all create "
      << fixed << qSetRealNumberPrecision(2) << m_velocityTemp << " "
      << "4928459 "
      << "rot " << getZeroL() << " "
      << "mom " << getZeroMOM() << " "
      << "dist " << getVelocityDist(m_velocityDist) << "\n";
    mol << getEnsemble(m_ensemble) << "\n";
    mol << "timestep       "
      << fixed << qSetRealNumberPrecision(1) << m_timeStep << "\n";
    mol << "\n";

    mol << "# Output\n";
    if (m_dumpXYZ != "") {
      mol << "dump           dumpXYZ all xyz "
        << m_dumpStep << " " << m_dumpXYZ << "\n";
    }
    mol << "thermo_style   " << getThermoStyle(m_thermoStyle) << "\n";
    mol << "thermo         " << m_thermoInterval << "\n";
    mol << "\n";

    mol << "# Run the simulation\n";
    mol << "run            " << m_runSteps << "\n";
    mol << "\n";

    return buffer;
  }

  QString LammpsInputDialog::getUnitType(unitType t)
  {
    // Translate the enum to text for the output generation
    switch (t)
    {
      case lj:
        return "lj";
      case real:
        return "real";
      case metal:
        return "metal";
      case si:
        return "si";
      case cgs:
        return "cgs";
      case u_electron:
        return "electron";
      default:
        return "lj";
    }
  }

  QString LammpsInputDialog::getAtomStyle(atomStyle t)
  {
    switch (t)
    {
      case angle:
        return "angle";
      case atomic:
        return "atomic";
      case bond:
        return "bond";
      case charge:
        return "charge";
      case dipole:
        return "dipole";
      case a_electron:
        return "electron";
      case ellipsoid:
        return "ellipsoid";
      case full:
        return "full";
      case line:
        return "line";
      case meso:
        return "meso";
      case molecular:
        return "molecular";
      case peri:
        return "peri";
      case sphere:
        return "sphere";
      case tri:
        return "tri";
      case wavepacket:
        return "wavepacket";
      default:
        return "full";
    }
  }

  QString LammpsInputDialog::getDimensionType(dimensionType t)
  {
    switch(t)
    {
      case d2:
        return "2d";
      case d3:
        return "3d";
      default:
        return "3d";
    }
  }

  QString LammpsInputDialog::getXBoundaryType(boundaryType t)
  {
    switch(t)
    {
      case p:
        return "p";
      case f:
        return "f";
      case s:
        return "s";
      case m:
        return "m";
      case fs:
        return "fs";
      case fm:
        return "fm";
      default:
        return "p";
    }
  }

  QString LammpsInputDialog::getYBoundaryType(boundaryType t)
  {
    switch(t)
    {
      case p:
        return "p";
      case f:
        return "f";
      case s:
        return "s";
      case m:
        return "m";
      case fs:
        return "fs";
      case fm:
        return "fm";
      default:
        return "p";
    }
  }

  QString LammpsInputDialog::getZBoundaryType(boundaryType t)
  {
    switch(t)
    {
      case p:
        return "p";
      case f:
        return "f";
      case s:
        return "s";
      case m:
        return "m";
      case fs:
        return "fs";
      case fm:
        return "fm";
      default:
        return "p";
    }
  }

  QString LammpsInputDialog::getWaterPotential(waterPotential t)
  {
    switch(t)
    {
      case NONE:
        {
          QString     waterPotentialInput;
          QTextStream water(&waterPotentialInput);
          water << "";
          return waterPotentialInput;
        }
      case SPC:
        {
          QString     waterPotentialInput;
          QTextStream water(&waterPotentialInput);
          int Hydrogen;
          int Oxygen;
          determineAtomTypesSPC(Hydrogen, Oxygen);
          water
            << "#The SPC water potential\n"
            << "pair_style      lj/cut/coul/cut 9.8 9.8\n"
            << "pair_coeff      "
            << Oxygen << " " << Oxygen
            << " 0.15535 3.5533\n"
            << "pair_coeff      "
            << "* " << Hydrogen << " 0.00000 0.0000\n"
            << "bond_style      harmonic\n"
            << "angle_style     harmonic\n"
            << "dihedral_style  none\n"
            << "improper_style  none\n"
            << "bond_coeff      1 100.00   1.000\n"
            << "angle_coeff     1 100.00 109.47\n"
            << "special_bonds   lj/coul 0.0 0.0 0.5\n"
            << "fix             RigidOHBonds all shake 0.0001 20 0 b 1 a 1\n";
          return waterPotentialInput;
        }
      case SPCE:
        {
          QString     waterPotentialInput;
          QTextStream water(&waterPotentialInput);
          int Hydrogen;
          int Oxygen;
          determineAtomTypesSPC(Hydrogen, Oxygen);
          water
            << "#The SPC/E water potential\n"
            << "pair_style      lj/cut/coul/long 9.8 9.8\n"
            << "kspace_style    pppm 1.0e-4\n"
            << "pair_coeff      "
            << Oxygen << " " << Oxygen
            << " 0.15535 3.5533\n"
            << "pair_coeff      "
            << "* " << Hydrogen << " 0.00000 0.0000\n"
            << "bond_style      harmonic\n"
            << "angle_style     harmonic\n"
            << "dihedral_style  none\n"
            << "improper_style  none\n"
            << "bond_coeff      1 100.00   1.000\n"
            << "angle_coeff     1 100.00 109.47\n"
            << "special_bonds   lj/coul 0.0 0.0 0.5\n"
            << "fix             RigidOHBonds all shake 0.0001 20 0 b 1 a 1\n";
          return waterPotentialInput;
        }
      default:
        {
          QString     waterPotentialInput;
          QTextStream water(&waterPotentialInput);
          water << "\n";
          return waterPotentialInput;
        }
    }
  }

  QString LammpsInputDialog::getEnsemble(ensemble t)
  {
    switch(t)
    {
      case NVT:
        {
          QString     ensembleInput;
          QTextStream fix(&ensembleInput);
          fix << "fix            ensemble all nvt"
            << " temp "
            << fixed << qSetRealNumberPrecision(2) << m_temperature << " "
            << fixed << qSetRealNumberPrecision(2) << m_temperature
            << " 100 "
            << "tchain " << m_nhChain << "\n";
          return ensembleInput;
        }
      case NVE:
        {
          QString     ensembleInput;
          QTextStream fix(&ensembleInput);
          fix << "fix            ensemble all nve\n";
          return ensembleInput;
        }
      default:
        {
          QString     ensembleInput;
          QTextStream fix(&ensembleInput);
          fix << "fix            ensemble all nvt"
            << " temp "
            << fixed << qSetRealNumberPrecision(2) << m_temperature << " "
            << fixed << qSetRealNumberPrecision(2) << m_temperature
            << " 100 "
            << "tchain " << m_nhChain << "\n";
          return ensembleInput;
        }
    }
  }

  QString LammpsInputDialog::getVelocityDist(velocityDist t)
  {
    switch(t)
    {
      case gaussian:
        return "gaussian";
      case uniform:
        return "uniform";
      default:
        return "gaussian";
    }
  }

  QString LammpsInputDialog::getZeroMOM()
  {
    if(m_zeroMOM)
      return "yes";
    else
      return "no";
  }

  QString LammpsInputDialog::getZeroL()
  {
    if(m_zeroL)
      return "yes";
    else
      return "no";
  }

  QString LammpsInputDialog::getThermoStyle(thermoStyle t)
  {
    switch(t)
    {
      case one:
        return "one";
      case multi:
        return "multi";
      default:
        return "one";
    }
  }

  void LammpsInputDialog::deckDirty(bool dirty)
  {
    m_dirty = dirty;
    ui.titleLine->setEnabled(!dirty);
    //ui.calculationCombo->setEnabled(!dirty);
    //ui.theoryCombo->setEnabled(!dirty);
    //ui.basisCombo->setEnabled(!dirty);
    //ui.multiplicitySpin->setEnabled(!dirty);
    //ui.chargeSpin->setEnabled(!dirty);
    ui.enableFormButton->setEnabled(dirty);
  }

  void LammpsInputDialog::readSettings(QSettings& settings)
  {
    m_savePath = settings.value("lammps/savepath").toString();
  }

  void LammpsInputDialog::writeSettings(QSettings& settings) const
  {
    settings.setValue("lammps/savepath", m_savePath);
  }

  void LammpsInputDialog::determineAtomTypesSPC(int &hyd, int &oxy)
  {
    double ThisMass;
    QString ThisAtom;

    QList<Atom *> atoms = m_molecule->atoms();
    foreach (Atom *atom, atoms) {
      ThisMass = atom->OBAtom().GetAtomicMass();
      ThisAtom = OpenBabel::OBElements::GetSymbol(atom->atomicNumber());
      AtomMass[ThisAtom] = ThisMass;
    }
    int AtomIndex=0;
    //Set AtomType integer
    for (itr = AtomMass.begin(); itr != AtomMass.end(); ++itr) {
      AtomIndex++;
      AtomType[itr.key()] = AtomIndex;
    }
    //this is on purpose due to the use of
    //unordered_map in OpenBabel, which
    //returns a different order for O and H.
    hyd = AtomType.value("O");
    oxy = AtomType.value("H");
  }
}
