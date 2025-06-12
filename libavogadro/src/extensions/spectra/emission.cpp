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

#include "emission.h"
#include "spectradialog.h"

#include <QtWidgets/QMessageBox>
#include <QtCore/QDebug>

#include <openbabel/mol.h>

using namespace std;

namespace Avogadro {

OrcaEmissionSpectra::OrcaEmissionSpectra( SpectraDialog *parent ) :
    AbstractOrcaSpectra( parent )
{
    readSettings();
    if (m_lineShape == GAUSSIAN || m_lineShape == LORENTZIAN) {
        ui.label_FWHM_Voigt->hide();
        ui.spin_FWHM_Voigt->hide();
        ui.label_FWHM_peak->setText("Peak Width");
    } else if (m_lineShape == VOIGT) {
        ui.label_FWHM_Voigt->show();
        ui.spin_FWHM_Voigt->show();
        ui.label_FWHM_peak->setText("Gaussian Width");
    }
}

OrcaEmissionSpectra::~OrcaEmissionSpectra() {
    // TODO: Anything to delete?
    writeSettings();
}

void OrcaEmissionSpectra::writeSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    settings.setValue("spectra/Orca/Emission/gaussianWidth", ui.spin_FWHM->value());
    settings.setValue("spectra/Orca/Emission/labelPeaks", ui.cb_labelPeaks->isChecked());
    settings.setValue("spectra/Orca/Emission/XUnits", ui.combo_XUnit->currentIndex());
    settings.setValue("spectra/Orca/Emission/nPoints", ui.spin_nPoints->value());
    settings.setValue("spectra/Orca/Emission/lineShape", ui.combo_lineShape->currentIndex());
}

void OrcaEmissionSpectra::readSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    ui.spin_FWHM->setValue(settings.value("spectra/Orca/Emission/gaussianWidth",0.0).toDouble());
    ui.cb_labelPeaks->setChecked(settings.value("spectra/Orca/Emission/labelPeaks",false).toBool());
    ui.spin_nPoints->setValue(settings.value("spectra/Orca/Emission/nPoints",10).toInt());
    ui.combo_lineShape->setCurrentIndex(settings.value("spectra/Orca/Emission/lineShape", GAUSSIAN).toInt());
    m_lineShape = LineShape(ui.combo_lineShape->currentIndex());

    ui.combo_XUnit->setCurrentIndex(settings.value("spectra/Orca/Emission/XUnits", WAVELENGTH).toInt());
    m_XUnit = XUnits(ui.combo_XUnit->currentIndex());
//    m_EnergyShift = ui.spin_EnergyShift;
}

bool OrcaEmissionSpectra::checkForData(Molecule * mol)
{
    Q_UNUSED(mol);
    return false;
}

void OrcaEmissionSpectra::setupPlot(PlotWidget * plot) {
    plot->scaleLimits();
    switch (m_XUnit) {
    case ENERGY_eV:
        plot->axis(PlotWidget::BottomAxis)->setLabel(tr("Energy (eV)"));
        break;
    case WAVELENGTH:
        plot->axis(PlotWidget::BottomAxis)->setLabel(tr("Wavelength (nm)"));
        break;
    case WAVENUMBER:
        plot->axis(PlotWidget::BottomAxis)->setLabel(tr("<HTML>Wavenumber (cm<sup>-1</sup>)</HTML>"));
        break;
    default:
        break;
    }
    plot->axis(PlotWidget::LeftAxis)->setLabel(tr("Intensity"));
}

void OrcaEmissionSpectra::getCalculatedPlotObject(PlotObject *plotObject){
    AbstractOrcaSpectra::getCalculatedPlotObject(plotObject);
}

QString OrcaEmissionSpectra::getTSV()
{
    switch (m_XUnit) {
    case ENERGY_eV:
        return SpectraType::getTSV ( "Energy (eV)" ,"Intensity");
        break;
    case WAVELENGTH:
        return SpectraType::getTSV ( "Wavelength (nm)" ,"Intensity");
        break;
    case WAVENUMBER:
        return SpectraType::getTSV ( "Wavenumber (cm-1)" ,"Intensity");
        break;
    default:
        return SpectraType::getTSV ( "Wavelength (nm)" ,"Intensity");
        break;
    }
}
QString OrcaEmissionSpectra::getDataStream(PlotObject *plotObject)
{
    switch (m_XUnit) {
    case ENERGY_eV:
        return SpectraType::getDataStream ( plotObject, "Energy (eV)" ,"Intensity");
        break;
    case WAVELENGTH:
        return SpectraType::getDataStream ( plotObject, "Wavelength (nm)" ,"Intensity");
        break;
    case WAVENUMBER:
        return SpectraType::getDataStream ( plotObject, "Wavenumber cm-1" ,"Intensity");
        break;
    default:
        return SpectraType::getDataStream ( plotObject, "Wavelength (nm)" ,"Intensity");
        break;
    }
}



}
