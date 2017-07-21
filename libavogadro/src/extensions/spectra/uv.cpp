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

#include "uv.h"
#include "spectradialog.h"

#include <QtGui/QMessageBox>
#include <QtCore/QDebug>

#include <openbabel/mol.h>

using namespace std;

namespace Avogadro {

UVSpectra::UVSpectra( SpectraDialog *parent ) :
    SpectraType( parent ), m_lineShape(GAUSSIAN) , m_nPoints(10)
{
    ui.setupUi(m_tab_widget);
    // Setup signals/slots
    connect(this, SIGNAL(plotDataChanged()),
            m_dialog, SLOT(regenerateCalculatedSpectra()));
    connect(ui.cb_labelPeaks, SIGNAL(toggled(bool)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_FWHM, SIGNAL(valueChanged(double)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_nPoints, SIGNAL(valueChanged(int)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_Xmin, SIGNAL(valueChanged(double)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_Xmax, SIGNAL(valueChanged(double)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.combo_XUnit, SIGNAL(currentIndexChanged(int)),
            this, SIGNAL(plotDataChanged()));

    connect(ui.combo_lineShape, SIGNAL(currentIndexChanged(int)),
            this, SLOT(changeLineShape(int)));
    readSettings();
}

UVSpectra::~UVSpectra() {
    // TODO: Anything to delete?
    writeSettings();
}

void UVSpectra::writeSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    settings.setValue("spectra/UV/gaussianWidth", ui.spin_FWHM->value());
    settings.setValue("spectra/UV/labelPeaks", ui.cb_labelPeaks->isChecked());
    settings.setValue("spectra/UV/XUnits", ui.combo_XUnit->currentIndex());
    settings.setValue("spectra/UV/lineShape", ui.combo_lineShape->currentIndex());
    settings.setValue("spectra/UV/nPoints", ui.spin_nPoints->value());
}

void UVSpectra::readSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    ui.spin_FWHM->setValue(settings.value("spectra/UV/gaussianWidth",0.0).toDouble());
    ui.cb_labelPeaks->setChecked(settings.value("spectra/UV/labelPeaks",false).toBool());
    ui.spin_nPoints->setValue(settings.value("spectra/UV/nPoints",10).toInt());
    ui.combo_lineShape->setCurrentIndex(settings.value("spectra/UV/lineShape", GAUSSIAN).toInt());
    m_lineShape = LineShape(ui.combo_lineShape->currentIndex());
    ui.combo_XUnit->setCurrentIndex(settings.value("spectra/UV/XUnits", WAVELENGTH).toInt());
    m_XUnit = XUnits(ui.combo_XUnit->currentIndex());
}

bool UVSpectra::checkForData(Molecule * mol) {

    OpenBabel::OBMol obmol = mol->OBMol();
    OpenBabel::OBElectronicTransitionData *etd = static_cast<OpenBabel::OBElectronicTransitionData*>(obmol.GetData("ElectronicTransitionData"));

    if (!etd) return false;
    if (etd->GetEDipole().size() == 0) return false;

    m_wavelength.resize(0);
    m_wavenumber.resize(0);
    m_energy.resize(0);
    m_edipole.resize(0);
    // OK, we have valid data, so store them for later
    m_wavelength = etd->GetWavelengths();
    // sort for ascending wavelength
    getSortIdx(m_wavelength);

    std::vector<double> tmp_edipole = etd->GetEDipole();
    for (uint i = 0; i < tmp_edipole.size(); i++){
        m_edipole.push_back(tmp_edipole[m_idx[i]]);
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
        m_xmin = m_xmin_org = m_wavelength.at(m_wavelength.size()-1);
        m_xmax = m_xmax_org = m_wavelength.at(0);
        for (uint i = 0; i < m_wavelength.size(); i++){
            m_xList.append(m_wavelength.at(i));
            m_yList.append(m_edipole.at(i));
        }
        break;
    case ENERGY_eV:
        m_xmin = m_energy.at(0);
        m_xmax = m_energy.at(m_wavelength.size()-1);
        m_xmin_org = eV_to_nm / m_xmin;
        m_xmax_org = eV_to_nm / m_xmax;
        for (uint i = 0; i < m_wavelength.size(); i++){
            m_xList.append(m_energy.at(i));
            m_yList.append(m_edipole.at(i));
        }
        break;
    case WAVENUMBER:
        m_xmin = m_wavenumber.at(0);
        m_xmax = m_wavenumber.at(m_wavelength.size()-1);
        m_xmin_org = cm_1_to_nm/m_xmin;
        m_xmax_org = cm_1_to_nm/m_xmax;

        for (uint i = 0; i < m_wavelength.size(); i++){
            m_xList.append(m_wavenumber.at(i));
            m_yList.append(m_edipole.at(i));
        }
        break;
    default:
        break; // never hit here
    }
    ui.spin_Xmin->setValue(m_xmin);
    ui.spin_Xmax->setValue(m_xmax);
    return true;
}

void UVSpectra::setupPlot(PlotWidget * plot) {
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
    //    plot->axis(PlotWidget::BottomAxis)->setLabel(tr("Wavelength (nm)"));
    plot->axis(PlotWidget::LeftAxis)->setLabel(tr("<HTML>&epsilon; (cm<sup>2</sup>/mmol)</HTML>"));
}

void UVSpectra::getCalculatedPlotObject(PlotObject *plotObject){
    plotObject->clearPoints();
    int i_xmin=0, i_xmax=0;
    double conv_unit[3]= {1., eV_to_nm, cm_1_to_nm};

    if (ui.spin_FWHM->value() != 0.0 && ui.cb_labelPeaks->isEnabled()) {
        ui.cb_labelPeaks->setEnabled(false);
        ui.cb_labelPeaks->setChecked(false);
    }
    if (ui.spin_FWHM->value() == 0.0 && !ui.cb_labelPeaks->isEnabled()) {
        ui.cb_labelPeaks->setEnabled(true);
    }
    if (!ui.cb_labelPeaks->isEnabled()) {
        ui.cb_labelPeaks->setChecked(false);
    }
    // Get X units
    m_resize = false;
    m_newrange = false;
    if (m_XUnit != XUnits(ui.combo_XUnit->currentIndex())) {
        m_XUnit = XUnits(ui.combo_XUnit->currentIndex());
        m_resize = true;
    }
    // Get X min
    if (m_xmin != ui.spin_Xmin->value()) {
        m_xmin = ui.spin_Xmin->value();
        m_xmin_org = conv_unit[int(m_XUnit)]/m_xmin;
        m_newrange = true;
    }
    // Get X max
    if (m_xmax != ui.spin_Xmax->value()) {
        m_xmax = ui.spin_Xmax->value();
        m_xmax_org = conv_unit[int(m_XUnit)]/m_xmax;
        m_newrange = true;
    }

    //    if (!m_newrange && m_resize) {
    //        switch (m_XUnit) {
    //        case WAVELENGTH:
    //            for (uint i = 0; i < m_wavelength.size(); i++){
    //                m_xList.replace(i,m_wavelength.at(i));
    //            }
    ////            m_xmax = getMax(WAVELENGTH);
    ////            m_xmin = getMin(WAVELENGTH);
    //            break;
    //        case ENERGY_eV:
    //            for (uint i = 0; i < m_wavelength.size(); i++){
    //                m_xList.replace(i,m_energy.at(i));
    //            }
    ////            m_xmax = getMax(ENERGY_eV);
    ////            m_xmin = getMin(ENERGY_eV);
    //            break;
    //        case WAVENUMBER:
    //            for (uint i = 0; i < m_wavelength.size(); i++){
    //                m_xList.replace(i,m_wavenumber.at(i));
    //            }
    //            //            m_xmax = getMax(WAVENUMBER);
    //            //            m_xmin = getMin(WAVENUMBER);
    //            break;
    //        default:
    //            break; // never hit here
    //        }
    //    } else
    if (m_newrange || m_resize){
        m_xList.clear();
        m_yList.clear();
        switch (m_XUnit) {
        case WAVELENGTH:
            i_xmax = getXmaxIdx(WAVELENGTH);
            i_xmin = getXminIdx(WAVELENGTH);
            if (i_xmax == i_xmin) {         // extend to all points
                i_xmin = m_wavelength.size();
                i_xmax = 0;
            }
            for (int i = i_xmax; i < i_xmin; i++){
                m_xList.append(m_wavelength.at(i));
                m_yList.append(m_edipole.at(i));
            }
            break;
        case ENERGY_eV:
            i_xmax = getXmaxIdx(ENERGY_eV);
            i_xmin = getXminIdx(ENERGY_eV);
            if (i_xmax == i_xmin) {         // extend to all points
                i_xmin = 0;
                i_xmax = m_energy.size();
            }
            for (int i = i_xmin; i < i_xmax; i++){
                m_xList.append(m_energy.at(i));
                m_yList.append(m_edipole.at(i));
            }
            break;
        case WAVENUMBER:
            i_xmax = getXmaxIdx(WAVENUMBER);
            i_xmin = getXminIdx(WAVENUMBER);
            if (i_xmax == i_xmin) {         // extend to all points
                i_xmin = 0;
                i_xmax = m_wavenumber.size();
            }
            for (int i = i_xmin; i < i_xmax; i++){
                m_xList.append(m_wavenumber.at(i));
                m_yList.append(m_edipole.at(i));
            }
            break;
        default:
            break; // never hit here
        }
    }

    if (m_xList.size() < 1 && m_yList.size() < 1) return;

    double wavelength, intensity;
    double FWHM = ui.spin_FWHM->value();
    m_nPoints = ui.spin_nPoints->value();

    bool use_widening = (FWHM == 0) ? false : true;

    if (use_widening) {
        if (m_lineShape == GAUSSIAN) {  // gaussian shape

            // convert FWHM to sigma squared

            double sigma = FWHM / (2.0 * sqrt(2.0 * log(2.0)));
            double s2	= sigma*sigma;

            // create points

            QList<double> xPoints = getXPoints(FWHM, m_nPoints);

            for (int i = 0; i < xPoints.size(); i++) {
                double x = xPoints.at(i);
                double y = 0.0;

                for (int j = 0; j < m_yList.size(); j++) {
                    double t = m_yList.at(j);
                    double w = m_xList.at(j);

                    y += t * exp( - ( pow( (x - w), 2 ) ) / (2 * s2) ) *
                            // Normalization factor: (CP, 224 (1997) 143-155)
                            2.87e4 / sqrt(2 * M_PI * s2);
                }

                plotObject->addPoint(x,y);
            }
        } else {     // lorentzian shape
            double hwhm= FWHM /2.0;

            // create points

            QList<double> xPoints = getXPoints(FWHM, m_nPoints);

            for (int i = 0; i < xPoints.size(); i++) {
                double x = xPoints.at(i);
                double y = 0.0;

                for (int j = 0; j < m_yList.size(); j++) {
                    double t = m_yList.at(j);
                    double x0 = m_xList.at(j);

                    y += 2.87e4 * t * hwhm / (M_PI * ((x-x0)*(x-x0) + hwhm*hwhm));  // Normalization factor: (CP, 224 (1997) 143-155)
                }
                plotObject->addPoint(x,y);
            }
        }
    }
    else {  // only points
        for (int i = 0; i < m_yList.size(); i++) {

            wavelength = m_xList.at(i);
            intensity = m_yList.at(i) * 2.87e4; // Normalization factor:

            plotObject->addPoint ( wavelength, 0 );
            if (ui.cb_labelPeaks->isChecked()) {
                // %L1 uses localized number format (e.g., 1.023,4 in Europe)
                plotObject->addPoint( wavelength, intensity, QString("%L1").arg(wavelength, 0, 'f', 1) );
            } else {
                plotObject->addPoint ( wavelength, intensity );
            }
            plotObject->addPoint ( wavelength, 0 );
        }
    }
}
int UVSpectra::getXminIdx(XUnits xunit)
{
    int idx=m_wavelength.size();
    switch (xunit) {
    case WAVELENGTH:
        for (uint i = 0; i < m_wavelength.size(); i++){
            if ( m_wavelength.at(i) < m_xmin) {
                idx = i;
                break;
            }
        }
        break;
    case ENERGY_eV:
        for (uint i = 0; i < m_energy.size(); i++){
            if ( m_energy.at(i) > m_xmin) {
                idx = i;
                break;
            }
        }
        break;
    case WAVENUMBER:
        for (uint i = 0; i < m_wavenumber.size(); i++){
            if ( m_wavenumber.at(i) > m_xmin) {
                idx = i;
                break;
            }
        }
        break;
    default:
        break;
    }
    return idx;
}
int UVSpectra::getXmaxIdx(XUnits xunit)
{
    int idx=m_wavelength.size();

    switch (xunit) {
    case WAVELENGTH:
        for (uint i= 0; i < m_wavelength.size(); i++){
            if (m_xmax > m_wavelength.at(i)) {
                idx = i;
                break;
            }
        }
        break;
    case ENERGY_eV:
        for (uint i= 0; i < m_energy.size(); i++){
            if (m_xmax < m_energy.at(i)) {
                idx = i;
                break;
            }
        }
        break;
    case WAVENUMBER:
        for (uint i= 0; i < m_wavenumber.size(); i++){
            if (m_xmax < m_wavenumber.at(i)) {
                idx = i;
                break;
            }
        }
        break;
    }
    return idx;
}
//
// choose line shape - gaussian or lorentzian
//
void UVSpectra::changeLineShape(int type)
{
    m_lineShape = static_cast<LineShape>(type);
//    if (m_lineShape == GAUSSIAN || m_lineShape == LORENTZIAN) {
//        ui.label_FWHM_Voigt->hide();
//        ui.spin_FWHM_Voigt->hide();
//        ui.label_FWHM_peak->setText("Peak Width");
//    } else if (m_lineShape == VOIGT) {
//        ui.label_FWHM_Voigt->show();
//        ui.spin_FWHM_Voigt->show();
//        ui.label_FWHM_peak->setText("Gaussian Width");
//    }
    emit plotDataChanged();
}

QString UVSpectra::getTSV() {
    return SpectraType::getTSV("Wavelength (nm)", "Intensity (arb)");
}
QString UVSpectra::getDataStream(PlotObject *plotObject)
{
    switch (m_XUnit) {
    case ENERGY_eV:
        return SpectraType::getDataStream ( plotObject, "Energy (eV)" ,"Intensity (arb)");
        break;
    case WAVELENGTH:
        return SpectraType::getDataStream ( plotObject, "Wavelength (nm)" ,"Intensity (arb)");
        break;
    case WAVENUMBER:
        return SpectraType::getDataStream ( plotObject, "Wavenumber cm-1" ,"Intensity (arb)");
        break;
    default:    // never hit here
        return SpectraType::getDataStream ( plotObject, "Wavelength (nm)" ,"Intensity (arb)");
        break;
    }
}

void  UVSpectra::getSortIdx(std::vector<double> &sortData)
{

        int length = sortData.size();
        m_idx.resize(length);
        for (int i = 0; i < length; ++i) m_idx[i] =i;
        for (int i = 0; i < length; ++i)
        {
            bool swapped = false;
            for (int j = 0; j < length - (i+1); ++j)
            {
                if (sortData[j] < sortData[j+1])
                {
                    swap(sortData[j], sortData[j+1]);
                    swap(m_idx[j], m_idx[j+1]);
                    swapped = true;
                }
            }

            if (!swapped) break;
        }
        return;
}

}
