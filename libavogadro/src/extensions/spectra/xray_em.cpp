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

#include "xray_em.h"
#include "spectradialog.h"

#include <QtGui/QMessageBox>
#include <QtCore/QDebug>

#include <openbabel/mol.h>

using namespace std;

namespace Avogadro {

XRayEmissionSpectra::XRayEmissionSpectra( SpectraDialog *parent ) :
    AbstractXRaySpectra( parent )
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

XRayEmissionSpectra::~XRayEmissionSpectra() {
    // TODO: Anything to delete?
    writeSettings();
}

void XRayEmissionSpectra::writeSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    settings.setValue("spectra/XRay/Emission/gaussianWidth", ui.spin_FWHM->value());
    settings.setValue("spectra/XRay/Emission/labelPeaks", ui.cb_labelPeaks->isChecked());
    settings.setValue("spectra/XRay/Emission/XUnits", ui.combo_XUnit->currentIndex());
    settings.setValue("spectra/XRay/Emission/nPoints", ui.spin_nPoints->value());
    settings.setValue("spectra/XRay/Emission/lineShape", ui.combo_lineShape->currentIndex());
}

void XRayEmissionSpectra::readSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    ui.spin_FWHM->setValue(settings.value("spectra/XRay/Emission/gaussianWidth",0.0).toDouble());
    ui.cb_labelPeaks->setChecked(settings.value("spectra/XRay/Emission/labelPeaks",false).toBool());
    ui.spin_nPoints->setValue(settings.value("spectra/XRay/Emission/nPoints",10).toInt());
    ui.combo_lineShape->setCurrentIndex(settings.value("spectra/XRay/Emission/lineShape", GAUSSIAN).toInt());
    m_lineShape = LineShape(ui.combo_lineShape->currentIndex());

    ui.combo_XUnit->setCurrentIndex(settings.value("spectra/XRay/Emission/XUnits", WAVELENGTH).toInt());
    m_XUnit = XUnits(ui.combo_XUnit->currentIndex());
//    m_EnergyShift = ui.spin_EnergyShift;
}

bool XRayEmissionSpectra::checkForData(Molecule * mol) {

    OpenBabel::OBMol obmol = mol->OBMol();
    OpenBabel::OBXrayORCAData *osd = static_cast<OpenBabel::OBXrayORCAData*>(obmol.GetData("ORCASpectraData"));

    if (!osd) return false;
    if (!osd->GetXRayData()) return false;
    if (osd->GetEmEDipole().size() == 0) return false;

    m_wavelength.resize(0);
    m_wavenumber.resize(0);
    m_energy.resize(0);
    m_edipole.resize(0);

    std::vector<double> tmp_edipole, tmp_velosity, tmp_combined, tmp_D2, tmp_M2, tmp_Q2;

    // OK, we have valid data, so store them for later
    m_wavelength = osd->GetEmWavelengths();
    // sort for ascending wavelength
    getSortIdx(m_wavelength);

    tmp_edipole = osd->GetEmEDipole();
    if (osd->GetEmVelosity().size() != 0)  tmp_velosity = osd->GetEmVelosity();
    if (osd->GetEmCombined().size() != 0) {
        tmp_combined = osd->GetEmCombined();
        tmp_D2 = osd->GetEmD2();
        tmp_M2 = osd->GetEmM2();
        tmp_Q2 = osd->GetEmQ2();
    }

    // resort data
    for (uint i = 0; i < tmp_edipole.size(); i++){
        m_edipole.push_back(tmp_edipole[m_idx[i]]);
    }
    for (uint i = 0; i < tmp_edipole.size(); i++){
        if (osd->GetEmCombined().size() != 0) {
            m_combined.push_back(tmp_combined[m_idx[i]]);
            m_D2.push_back(tmp_D2[m_idx[i]]);
            m_M2.push_back(tmp_M2[m_idx[i]]);
            m_Q2.push_back(tmp_Q2[m_idx[i]]);
        }
    }

    // convert nm to cm-1 and eV
    for (uint i = 0; i < m_wavelength.size(); i++){
        m_wavenumber.push_back(1.e7/m_wavelength.at(i));
        m_energy.push_back(1.e7/(8065.54477*m_wavelength.at(i)));
    }

    // Store in member vars
    m_xList.clear();
    m_yList.clear();

    //    m_XUnit =  XUnits(ui.combo_XUnit->currentIndex());
    switch (m_XUnit) {

    case WAVELENGTH:
        m_XminIdx = m_wavelength.size()-1;
        m_xmin = m_xmin_org = m_wavelength.at(m_XminIdx);
        m_XmaxIdx = 0;
        m_xmax = m_xmax_org = m_wavelength.at(m_XmaxIdx);
        for (int i = m_XmaxIdx; i <= m_XminIdx; i++){
            m_xList.append(m_wavelength.at(i));
        }
        break;
    case ENERGY_eV:
        m_XminIdx = 0;
        m_xmin = m_energy.at(m_XminIdx);
        m_XmaxIdx = m_energy.size()-1;
        m_xmax = m_energy.at(m_XmaxIdx);
        m_xmin_org = eV_to_nm / m_xmin;
        m_xmax_org = eV_to_nm / m_xmax;
        for (int i = m_XminIdx; i <= m_XmaxIdx; i++){
            m_xList.append(m_energy.at(i));
        }
        break;
    case WAVENUMBER:
        m_XminIdx = 0;
        m_xmin = m_wavenumber.at(m_XminIdx);
        m_XmaxIdx = m_wavenumber.size()-1;
        m_xmax = m_wavenumber.at(m_XmaxIdx);
        m_xmin_org = cm_1_to_nm/m_xmin;
        m_xmax_org = cm_1_to_nm/m_xmax;

        for (int i = m_XminIdx; i <= m_XmaxIdx; i++){
            m_xList.append(m_wavenumber.at(i));
        }
        break;
    default:
        break; // never hit here
    }
    ui.spin_Xmin->setValue(m_xmin);
    ui.spin_Xmax->setValue(m_xmax);

    ui.combo_XRayType->clear();
    if (m_edipole.size() != 0) ui.combo_XRayType->addItem("Electric dipole");
    if (m_velosity.size() != 0) ui.combo_XRayType->addItem("Electric velosity");
    if (m_D2.size() != 0) ui.combo_XRayType->addItem("Electric dipole/total");
    if (m_M2.size() != 0) ui.combo_XRayType->addItem("Magnetic dipole/total");
    if (m_Q2.size() != 0) ui.combo_XRayType->addItem("Quadrupole dipole/total");
    if (m_combined.size() != 0) ui.combo_XRayType->addItem("Combined");
    XRayTypeChanged(ui.combo_XRayType->currentText());
    return true;
}

void XRayEmissionSpectra::setupPlot(PlotWidget * plot) {
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

void XRayEmissionSpectra::getCalculatedPlotObject(PlotObject *plotObject){
    AbstractXRaySpectra::getCalculatedPlotObject(plotObject);
}

QString XRayEmissionSpectra::getTSV()
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
QString XRayEmissionSpectra::getDataStream(PlotObject *plotObject)
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
