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
#include "abstract_orcaspec.h"

#include <QtWidgets/QMessageBox>
#include <QtCore/QDebug>

#include <openbabel/mol.h>

using namespace std;


namespace Avogadro {

AbstractOrcaSpectra::AbstractOrcaSpectra( SpectraDialog *parent ) :
    SpectraType( parent ),m_lineShape(GAUSSIAN), m_nPoints(10)
{
    ui.setupUi(m_tab_widget);
    // Setup signals/slots
    connect(this, SIGNAL(plotDataChanged()),
            m_dialog, SLOT(regenerateCalculatedSpectra()));
    connect(ui.cb_labelPeaks, SIGNAL(toggled(bool)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_FWHM, SIGNAL(valueChanged(double)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_FWHM_Voigt, SIGNAL(valueChanged(double)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_nPoints, SIGNAL(valueChanged(int)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_Xmin, SIGNAL(valueChanged(double)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.spin_Xmax, SIGNAL(valueChanged(double)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.combo_XUnit, SIGNAL(currentIndexChanged(int)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.combo_OrcaSpecType, SIGNAL(currentIndexChanged(QString)),
            this, SLOT(OrcaSpecTypeChanged(QString)));

    connect(ui.combo_lineShape, SIGNAL(currentIndexChanged(int)),
            this, SLOT(changeLineShape(int)));

    connect(ui.hs_EnergyShift, SIGNAL(valueChanged(int)),
            this, SLOT(EnergyShiftSilderValueChanged(int)));
    connect(ui.spin_EnergyShift, SIGNAL(valueChanged(double)),
            this, SLOT(EnergyShiftSpinValueChanged(double)));

    m_EnergyShift = ui.spin_EnergyShift->value();
    m_newShift = false;
}
void AbstractOrcaSpectra::EnergyShiftSpinValueChanged(double value)
{
    m_EnergyShift = value;
    ui.hs_EnergyShift->setValue((int(m_EnergyShift)));
    m_newShift = true;
    emit plotDataChanged();
}
void AbstractOrcaSpectra::EnergyShiftSilderValueChanged(int value)
{
    m_EnergyShift = double(value);
    ui.spin_EnergyShift->setValue(m_EnergyShift);
    m_newShift = true;
    emit plotDataChanged();
}

void AbstractOrcaSpectra::OrcaSpecTypeChanged(const QString & str)
{
     m_yList.clear();
    int tmp_minIdx = m_XminIdx;
    int tmp_maxIdx = m_XmaxIdx;
    if (m_XminIdx>m_XmaxIdx) {
        tmp_minIdx = m_XmaxIdx;
        tmp_maxIdx = m_XminIdx;
    }
    qDebug() << "str = " << str << endl;

     if (str == "Transition Electric dipole") {
         for (int i = tmp_minIdx; i <= tmp_maxIdx; i++){
             m_yList.append(m_edipole.at(i));
         }
     } else if (str == "Electric dipole/total") {
         for (int i = tmp_minIdx; i <= tmp_maxIdx; i++){
             m_yList.append(m_D2.at(i));
         }
     } else if (str == "Magnetic dipole/total") {
         for (int i = tmp_minIdx; i <= tmp_maxIdx; i++){
             m_yList.append(m_M2.at(i));
         }
     } else if (str == "Quadrupole dipole/total") {
         for (int i = tmp_minIdx; i <= tmp_maxIdx; i++){
             m_yList.append(m_Q2.at(i));
         }
     } else if (str == "Combined") {
         for (int i = tmp_minIdx; i <= tmp_maxIdx; i++){
             m_yList.append(m_combined.at(i));
         }
     }
    m_OrcaSpecType = str;
    emit plotDataChanged();
}
//
// choose line shape - gaussian or lorentzian
//
void AbstractOrcaSpectra::changeLineShape(int type)
{
    m_lineShape = static_cast<LineShape>(type);
    if (m_lineShape == GAUSSIAN || m_lineShape == LORENTZIAN) {
        ui.label_FWHM_Voigt->hide();
        ui.spin_FWHM_Voigt->hide();
        ui.label_FWHM_peak->setText("Peak Width");
    } else if (m_lineShape == VOIGT) {
        ui.label_FWHM_Voigt->show();
        ui.spin_FWHM_Voigt->show();
        ui.label_FWHM_peak->setText("Gaussian Width");
    }
    emit plotDataChanged();
}

void AbstractOrcaSpectra::getCalculatedPlotObject(PlotObject *plotObject){
    plotObject->clearPoints();
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
    if (m_newrange || m_resize || m_newShift){
        m_xList.clear();
        switch (m_XUnit) {
        case WAVELENGTH:
            m_XmaxIdx = getXmaxIdx(WAVELENGTH);
            m_XminIdx = getXminIdx(WAVELENGTH);
            if (m_XmaxIdx == m_XminIdx) {         // extend to all points
                m_XminIdx = m_wavelength.size()-1;
                m_XmaxIdx = 0;
            }
            for (int i = m_XmaxIdx; i <= m_XminIdx; i++){
                m_xList.append(m_wavelength.at(i)+m_EnergyShift);
            }
            rearrangeYList(m_XmaxIdx, m_XminIdx);
            break;
        case ENERGY_eV:
            m_XmaxIdx = getXmaxIdx(ENERGY_eV);
            m_XminIdx = getXminIdx(ENERGY_eV);
            if (m_XmaxIdx == m_XminIdx) {         // extend to all points
                m_XminIdx = 0;
                m_XmaxIdx = m_energy.size()-1;
            }
            for (int i = m_XminIdx; i <= m_XmaxIdx; i++){
                m_xList.append(m_energy.at(i)+m_EnergyShift);
            }
            rearrangeYList(m_XminIdx, m_XmaxIdx);
            break;
        case WAVENUMBER:
            m_XmaxIdx = getXmaxIdx(WAVENUMBER);
            m_XminIdx = getXminIdx(WAVENUMBER);
            if (m_XmaxIdx == m_XminIdx) {         // extend to all points
                m_XminIdx = 0;
                m_XmaxIdx = m_wavenumber.size()-1;
            }
            for (int i = m_XminIdx; i <= m_XmaxIdx; i++){
                m_xList.append(m_wavenumber.at(i)+m_EnergyShift);
            }
            rearrangeYList(m_XminIdx, m_XmaxIdx);
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

 // overlay line shapes

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
                    double x0 = m_xList.at(j);

                    y += t * exp( - ((x-x0)*(x-x0)/ (2 * s2) ) )/
                            // Normalization factor: (CP, 224 (1997) 143-155)
                             sqrt(2 * M_PI * s2);
                }
                plotObject->addPoint(x,y*2.87e4);  // Normalization factor: (CP, 224 (1997) 143-155)
            }
        } else if (m_lineShape == LORENTZIAN) {    // lorentzian shape
            double hwhm= FWHM /2.0;
            // create points
            QList<double> xPoints = getXPoints(FWHM, m_nPoints);
            for (int i = 0; i < xPoints.size(); i++) {
                double x = xPoints.at(i);
                double y = 0.0;

                for (int j = 0; j < m_yList.size(); j++) {
                    double t = m_yList.at(j);
                    double x0 = m_xList.at(j);
                    y += t * hwhm / (M_PI * ((x-x0)*(x-x0) + hwhm*hwhm));
                }
                plotObject->addPoint(x,y*2.87e4);   // Normalization factor: (CP, 224 (1997) 143-155)
            }
        }
        else {    // Voigt shape
                    double hwhm= ui.spin_FWHM_Voigt->value() /2.0;
                    double sigma = FWHM / (2.0 * sqrt(2.0 * log(2.0)));
                    double s2	= sigma*sigma;
                    // create points
                    QList<double> xPoints = getXPoints(FWHM, m_nPoints);
                    for (int i = 0; i < xPoints.size(); i++) {
                        double x = xPoints.at(i);
                        double y = 0.0;

                        for (int j = 0; j < m_yList.size(); j++) {
                            double t = m_yList.at(j);
                            double x0 = m_xList.at(j);
                            y += t * exp( - ( (x-x0)*(x-x0) / (2 * s2)) ) /
                                    sqrt(2 * M_PI * s2);

                            y += t * hwhm / (M_PI * ((x-x0)*(x-x0) + hwhm*hwhm));
                        }
                        // 50% gaussian and 50% lorentzian part
                        plotObject->addPoint(x,y*0.5*2.87e4);   // Normalization factor: (CP, 224 (1997) 143-155)
                    }
                }
    } else {  // only points
        for (int i = 0; i < m_yList.size(); i++) {

            wavelength = m_xList.at(i);
            intensity = m_yList.at(i) * 2.87e4;     // Normalization factor:

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
    m_newShift = false;
}
void AbstractOrcaSpectra::rearrangeYList(int min, int max)
{
    m_yList.clear();
    if (m_OrcaSpecType == "Transition Electric dipole") {
        for (int i = min; i <= max; i++){
            m_yList.append(m_edipole.at(i));
        }
    } else if (m_OrcaSpecType == "Electric dipole/total") {
        for (int i = min; i <= max; i++){
            m_yList.append(m_D2.at(i));
        }
    } else if (m_OrcaSpecType == "Magnetic dipole/total") {
        for (int i = min; i <= max; i++){
            m_yList.append(m_M2.at(i));
        }
    } else if (m_OrcaSpecType == "Quadrupole dipole/total") {
        for (int i = min; i <= max; i++){
            m_yList.append(m_Q2.at(i));
        }
    } else if (m_OrcaSpecType == "Combined") {
        for (int i = min; i <= max; i++){
            m_yList.append(m_combined.at(i));
        }
    }
}

int AbstractOrcaSpectra::getXminIdx(XUnits xunit)
{
    int idx=m_wavelength.size()-1;
    //    qDebug() << "m_wavelength = " << m_wavelength.at(0) << "   " << m_wavelength.at(m_wavelength.size()-1);
    //    qDebug() << "m_energy = " << m_energy.at(0) << "   " << m_energy.at(m_energy.size()-1);
    //    qDebug() << "m_wavenumber = " << m_wavenumber.at(0) << "   " << m_wavenumber.at(m_wavenumber.size()-1);

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
int AbstractOrcaSpectra::getXmaxIdx(XUnits xunit)
{
    int idx=m_wavelength.size()-1;
    //    qDebug() << "m_wavelength = " << m_wavelength.at(0) << "   " << m_wavelength.at(m_wavelength.size()-1);
    //    qDebug() << "m_energy = " << m_energy.at(0) << "   " << m_energy.at(m_energy.size()-1);
    //    qDebug() << "m_wavenumber = " << m_wavenumber.at(0) << "   " << m_wavenumber.at(m_wavenumber.size()-1);

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
                idx = i-1;
                break;
            }
        }
        break;
    case WAVENUMBER:
        for (uint i= 0; i < m_wavenumber.size(); i++){
            if (m_xmax < m_wavenumber.at(i)) {
                idx = i-1;
                break;
            }
        }
        break;
    }
    if (idx<0) idx=0;
    return idx;
}

void  AbstractOrcaSpectra::getSortIdx(std::vector<double> &sortData)
{

    int length = sortData.size();
    m_idx.resize(length);
    for (int i = 0; i < length; ++i) {
        m_idx[i] =i;
    }
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
