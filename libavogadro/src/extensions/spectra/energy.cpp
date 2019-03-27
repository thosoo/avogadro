/**********************************************************************
  SpectraDialog - Visualize spectral data from QM calculations

  Copyright (C) 2009 by David Lonie

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU Library General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#include <openbabel/generic.h>

#include "energy.h"

#include <QtGui/QMessageBox>
#include <QtCore/QDebug>

#include <openbabel/mol.h>

#define KCAL_TO_KJ	4.1868
#define _eV_kcal    23.0605             // eV into kcal/mol
#define _au_eV      27.2113834          // change of a.u. to eV

using namespace std;

namespace Avogadro {

  EnergySpectra::EnergySpectra( SpectraDialog *parent ) :
    SpectraType( parent )
  {
    ui.setupUi(m_tab_widget);

    m_yListEnergykJ = new QList<double>;
    m_yListEnergyeV = new QList<double>;
    m_yListEnergykcal = new QList<double>;
    m_energyYUnit = Y_ENERGY_kJ;
    // Setup signals/slots
    connect(this, SIGNAL(plotDataChanged()),
            m_dialog, SLOT(regenerateCalculatedSpectra()));
    connect(ui.energy_labelPeaks, SIGNAL(toggled(bool)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.combo_energyType, SIGNAL(currentIndexChanged(int)),
            this, SLOT(energyTypeChanged(int)));
    readSettings();
  }

  EnergySpectra::~EnergySpectra() {
    // TODO: Anything to delete?
    writeSettings();
  }

  void EnergySpectra::writeSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    settings.setValue("spectra/energy/energyType", ui.combo_energyType->currentIndex());
  }

  void EnergySpectra::readSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp

    ui.combo_energyType->setCurrentIndex(settings.value("spectra/energy/energyType", Y_ENERGY_kJ).toInt());

    m_energyYUnit = YEnergyUnit(ui.combo_energyType->currentIndex());
  }

  bool EnergySpectra::checkForData(Molecule * mol) {
    OpenBabel::OBMol obmol = mol->OBMol();
//    cout << obmol.HasData(OpenBabel::OBGenericDataType::ConformerData) << endl;
    OpenBabel::OBConformerData *confData = static_cast<OpenBabel::OBConformerData*>(obmol.GetData(OpenBabel::OBGenericDataType::ConformerData));

    if (!confData) return false;
    m_energy = confData->GetEnergies();

    m_numbers.clear();
    if (m_energy.size() <= 1) return false;

    // OK, we have valid data, so store them for later use

//    m_energy = confData->GetEnergies();
    m_nEnergies = m_energy.size();
    for (uint i = 0; i < m_nEnergies; i++){
        m_numbers.push_back(i+1);
    }

    // find energy minimum
    double min_energy=9.e30;

    for (uint i = 0; i < m_nEnergies; i++) {
      if (m_energy.at(i) < min_energy) min_energy = m_energy.at(i);
    }

    // Store in member vars
    m_xList.clear();
    m_yList.clear();
    m_yListEnergykJ->clear();
    m_yListEnergyeV->clear();
    m_yListEnergykcal->clear();

    for (uint i = 0; i < m_numbers.size(); i++)
      m_xList.append(m_numbers.at(i));

    for (uint i = 0; i < m_nEnergies; i++) {
      m_yListEnergykcal->append((m_energy.at(i)-min_energy));
      m_yListEnergyeV->append((m_energy.at(i)-min_energy)/_eV_kcal);
      m_yListEnergykJ->append((m_energy.at(i)-min_energy)*KCAL_TO_KJ);
    }

    switch (m_energyYUnit) {
    case Y_ENERGY_kJ:
       for (uint i = 0; i < m_nEnergies; i++)
        m_yList.append(m_yListEnergykJ->at(i));
      break;
    case Y_ENERGY_kcal:
     for (uint i = 0; i < m_nEnergies; i++)
      m_yList.append(m_yListEnergykcal->at(i));
      break;
    case Y_ENERGY_eV:
     for (uint i = 0; i < m_nEnergies; i++)
      m_yList.append(m_yListEnergyeV->at(i));
      break;
    default:
      break;    // never hit here
    }
    return true;
  }

  void EnergySpectra::setupPlot(PlotWidget * plot) {
    plot->scaleLimits();
    switch (m_energyYUnit) {
    case Y_ENERGY_eV:
      plot->axis(PlotWidget::LeftAxis)->setLabel(tr("<HTML>&Delta; Energy (eV)</HTML>"));
      break;
    case Y_ENERGY_kcal:
      plot->axis(PlotWidget::LeftAxis)->setLabel(tr("<HTML>&Delta; Energy (kcal)</HTML>"));
      break;
    case Y_ENERGY_kJ:
      plot->axis(PlotWidget::LeftAxis)->setLabel(tr("<HTML>&Delta; Energy (kJ)</HTML>"));
      break;
    default:
      break;
    }
    plot->axis(PlotWidget::BottomAxis)->setLabel(tr("Number"));
  }

//  QWidget * EnergySpectra::getTabWidget() {return m_tab_widget;}

  void EnergySpectra::getCalculatedPlotObject(PlotObject *plotObject)
  {
    plotObject->clearPoints();

    if (!ui.energy_labelPeaks->isEnabled()) {
        ui.energy_labelPeaks->setChecked(false);
    }

    if (m_xList.size() < 1 && m_yList.size() < 1) return;

    double number, energy;

    for (int i = 0; i < m_yList.size(); i++) {

      number = m_xList.at(i);
      energy = m_yList.at(i);

      if (ui.energy_labelPeaks->isChecked()) {
        // %L1 uses localized number format (e.g., 1.023,4 in Europe)
        plotObject->addPoint( number, energy, QString("%L1").arg(number, 0, 'd', 1) );
      } else {
        plotObject->addPoint ( number, energy );
      }
    }
  }

  /*void EnergySpectra::setImportedData(const QList<double> & xList, const QList<double> & yList) {
    m_xList_imp = new QList<double> (xList);
    m_yList_imp = new QList<double> (yList);
  }*/

  /*void EnergySpectra::getImportedPlotObject(PlotObject *plotObject) {
    plotObject->clearPoints();
    for (int i = 0; i < m_xList_imp.size(); i++)
      plotObject->addPoint(m_xList_imp.at(i), m_yList_imp.at(i));
  }*/

  QString EnergySpectra::getTSV() {
    return SpectraType::getTSV("Number", "Energy");
  }


  QString EnergySpectra::getDataStream(PlotObject *plotObject)
  {
      return SpectraType::getDataStream(plotObject, " Number", "Energy");
  }

  void EnergySpectra::energyTypeChanged(int energyType)
  {

    switch (YEnergyUnit (energyType)) {
    case Y_ENERGY_eV:
          m_yList = (*m_yListEnergyeV);
      break;
    case Y_ENERGY_kcal:
      m_yList = (*m_yListEnergykcal);
      break;
    case Y_ENERGY_kJ:
      m_yList = (*m_yListEnergykJ);
      break;
    default:
      break;
    }
    m_energyYUnit = YEnergyUnit(energyType);

    emit plotDataChanged();
  }
}
