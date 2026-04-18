/**********************************************************************
  SpectraDialog - Visualize spectral data from QM calculations

  Copyright (C) 2010 by Konstantin Tokarev

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

#include "raman.h"

#include <QtWidgets/QMessageBox>
#include <QtCore/QDebug>

#include <openbabel/mol.h>
#include <algorithm>
#include <cmath>

const double k=1.3806504e-23,
             h=6.62606896e-34,
             c=2.99792458e10; // speed of light (cm/s!)

using namespace std;

namespace Avogadro {
  namespace {
    const double kMinDenominator = 1.0e-12;
    const double kMinPositiveShift = 1.0e-8;
  }

  RamanSpectra::RamanSpectra( SpectraDialog *parent ) :
    AbstractIRSpectra( parent )
  {
    // Setup signals/slots
    connect(ui.spin_T, SIGNAL(valueChanged(double)),
            this, SLOT(updateT(double)));
    connect(ui.spin_W, SIGNAL(valueChanged(double)),
            this, SLOT(updateW(double)));
    ui.combo_yaxis->addItem(tr("Activity"));// (A<sup>4</sup>/amu)"));
    ui.combo_yaxis->addItem(tr("Intensity"));
    ui.combo_xaxis->setItemText(WAVENUMBER_CM1, tr("Raman Shift (cm^-1)"));
    ui.combo_xaxis->setItemText(WAVELENGTH_UM, tr("Scattered Wavelength (µm)"));
    ui.combo_xaxis->setItemText(WAVELENGTH_NM, tr("Scattered Wavelength (nm)"));
    ui.combo_xaxis->setItemText(ENERGY_MEV, tr("Shift Energy (meV)"));
    ui.combo_xaxis->setItemText(ENERGY_KJMOL, tr("Shift Energy (kJ/mol)"));
    readSettings();
  }

   RamanSpectra::~RamanSpectra() {
     // TODO: Anything to delete?
     writeSettings();
   }

  void RamanSpectra::writeSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp

    settings.setValue("spectra/Raman/scale", m_scale);
    settings.setValue("spectra/Raman/gaussianWidth", m_fwhm);
    settings.setValue("spectra/Raman/experimentTemperature", m_T);
    settings.setValue("spectra/Raman/laserWavenumber", m_W);
    settings.setValue("spectra/Raman/labelPeaks", ui.cb_labelPeaks->isChecked());
    settings.setValue("spectra/Raman/yAxisUnits", ui.combo_yaxis->currentText());
    settings.setValue("spectra/Raman/xAxisUnits", static_cast<int>(xAxisUnit()));
    settings.setValue("spectra/Raman/lineShape", ui.combo_lineShape->currentIndex());
    settings.setValue("spectra/Raman/broadeningModel", ui.combo_broadeningModel->currentIndex());
    settings.setValue("spectra/Raman/temperature", ui.spin_temperature->value());
    settings.setValue("spectra/Raman/referenceTemperature", ui.spin_referenceTemperature->value());
    settings.setValue("spectra/Raman/modelExponent", ui.spin_modelExponent->value());
    settings.setValue("spectra/Raman/baselineWidth", ui.spin_zeroWidth->value());
    settings.setValue("spectra/Raman/anharmonicAmplitude", ui.spin_anharmonicAmplitude->value());
    settings.setValue("spectra/Raman/voigtMix", ui.spin_voigtMix->value());
  }

  void RamanSpectra::readSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    m_scale = settings.value("spectra/Raman/scale", 1.0).toDouble();
    ui.spin_scale->setValue(m_scale);
    updateScaleSlider(m_scale);
    m_fwhm = settings.value("spectra/Raman/gaussianWidth",0.0).toDouble();
    ui.spin_FWHM->setValue(m_fwhm);
    updateFWHMSlider(m_fwhm);
    m_T = settings.value("spectra/Raman/experimentTemperature", 298.15).toDouble();
    ui.spin_T->setValue(m_T);
    m_W = settings.value("spectra/Raman/laserWavenumber", 9398.5).toDouble();
    ui.spin_W->setValue(m_W);
    ui.cb_labelPeaks->setChecked(settings.value("spectra/Raman/labelPeaks",false).toBool());
    QString yunit = settings.value("spectra/Raman/yAxisUnits",tr("Activity")).toString();
    updateYAxis(yunit);
    if (yunit == "Intensity")
      ui.combo_yaxis->setCurrentIndex(1);
    setXAxisUnit(static_cast<XAxisUnit>(
      settings.value("spectra/Raman/xAxisUnits", static_cast<int>(WAVENUMBER_CM1)).toInt()));

    ui.combo_lineShape->setCurrentIndex(settings.value("spectra/Raman/lineShape", GAUSSIAN).toInt());
    m_lineShape = LineShape(ui.combo_lineShape->currentIndex());
    ui.combo_broadeningModel->setCurrentIndex(settings.value("spectra/Raman/broadeningModel", CONSTANT_WIDTH).toInt());
    ui.spin_temperature->setValue(settings.value("spectra/Raman/temperature", 298.15).toDouble());
    ui.spin_referenceTemperature->setValue(settings.value("spectra/Raman/referenceTemperature", 298.15).toDouble());
    ui.spin_modelExponent->setValue(settings.value("spectra/Raman/modelExponent", 1.0).toDouble());
    ui.spin_zeroWidth->setValue(settings.value("spectra/Raman/baselineWidth", m_fwhm).toDouble());
    ui.spin_anharmonicAmplitude->setValue(settings.value("spectra/Raman/anharmonicAmplitude", 0.0).toDouble());
    ui.spin_voigtMix->setValue(settings.value("spectra/Raman/voigtMix", 0.5).toDouble());
    emit plotDataChanged();
  }

  bool RamanSpectra::checkForData(Molecule * mol) {
    OpenBabel::OBMol obmol = mol->OBMol();
    OpenBabel::OBVibrationData *vibrations = static_cast<OpenBabel::OBVibrationData*>(obmol.GetData(OpenBabel::OBGenericDataType::VibrationData));
    if (!vibrations) return false;

    // OK, we have valid vibrations, so store them for later
    vector<double> wavenumbers = vibrations->GetFrequencies();
    vector<double> intensities = vibrations->GetRamanActivities();

    if (wavenumbers.size() == 0 || intensities.size() == 0)
      return false;

    /* Case where there are no intensities, set all intensities to an arbitrary value, i.e. 1.0
    if (wavenumbers.size() > 0 && intensities.size() == 0) {
      // Warn user
      //QMessageBox::information(m_dialog, tr("No intensities"), tr("The vibration data in the molecule you have loaded does not have any intensity data. Intensities have been set to an arbitrary value for visualization."));
      for (uint i = 0; i < wavenumbers.size(); i++) {
        intensities.push_back(1.0);
      }
    }*/

    //
    double maxIntensity=0;
    for (unsigned int i = 0; i < intensities.size(); i++) {
      if (intensities.at(i) >= maxIntensity) {
        maxIntensity = intensities.at(i);
      }
    }

    /*vector<double> transmittances;*/

    for (unsigned int i = 0; i < intensities.size(); i++) {
      intensities[i] = intensities.at(i) / maxIntensity; 	// Normalize
    }

    // Store in member vars
    m_xList.clear();
    m_xList_orig.clear();
    m_yList.clear();
    m_yList_orig.clear();
    for (uint i = 0; i < wavenumbers.size(); i++){
      double w = wavenumbers.at(i);
      m_xList.append(w*scale(w));
      m_xList_orig.append(w);
      m_yList.append(intensities.at(i));
      m_yList_orig.append(intensities.at(i));
    }

    return true;
  }

  void RamanSpectra::setupPlot(PlotWidget * plot) {
    double xMin, xMax;
    xAxisDefaultLimits(xMin, xMax);
    plot->setDefaultLimits(xMin, xMax, 0.0, 1.0);
    plot->axis(PlotWidget::BottomAxis)->setLabel(xAxisLabel());
    plot->axis(PlotWidget::LeftAxis)->setLabel(m_yaxis);
  }

  void RamanSpectra::getCalculatedPlotObject(PlotObject *plotObject) {
    for(int i = 0; i< m_yList.size(); i++) {
      // Convert to intensities?
      if (ui.combo_yaxis->currentIndex() == 1) {
        const double shift = m_xList.at(i);
        const double activity = m_yList_orig.at(i);
        if (!std::isfinite(shift) || !std::isfinite(activity) ||
            !std::isfinite(m_W) || !std::isfinite(m_T) ||
            shift <= kMinPositiveShift || m_T <= kMinDenominator) {
          m_yList[i] = 0.0;
          continue;
        }
        const double scattered = m_W - shift;
        if (!std::isfinite(scattered)) {
          m_yList[i] = 0.0;
          continue;
        }
        const double boltzArg = -h * c * shift / (k * m_T);
        double boltz = 1.0;
        if (std::isfinite(boltzArg) && boltzArg > -700.0 && boltzArg < 700.0) {
          boltz += exp(boltzArg);
        }
        const double intensity = activity * 1e-8 / shift * pow(scattered, 4) * boltz;
        m_yList[i] = std::isfinite(intensity) ? intensity : 0.0;
      } else {
        m_yList[i] = m_yList_orig.at(i);
      }
    }

    AbstractIRSpectra::getCalculatedPlotObject(plotObject);

    // Add labels for gaussians?
    if (hasEffectiveBroadening() && ui.cb_labelPeaks->isChecked()) {
      assignGaussianLabels(plotObject, true, m_labelYThreshold);
      m_dialog->labelsUp(true);
    }
  }

  /*void RamanSpectra::setImportedData(const QList<double> & xList, const QList<double> & yList) {
    m_xList_imp = new QList<double> (xList);
    m_yList_imp = new QList<double> (yList);
    SpectraType::setImportedData(xList, yList);

    // Convert y values to percents from fraction, if necessary...
    bool convert = true;
    for (int i = 0; i < m_yList_imp.size(); i++) {
      if (m_yList_imp.at(i) > 1.5) { // If transmittances exist greater than this, they're already in percent.
        convert = false;
        break;
      }
    }
    if (convert) {
      for (int i = 0; i < m_yList.size(); i++) {
        double tmp = m_yList.at(i);
        tmp *= 100;
        m_yList.replace(i, tmp);
      }
    }
  }*/

  QString RamanSpectra::getTSV() {
      return SpectraType::getTSV("Frequencies", "Activities");
  }

  QString RamanSpectra::getDataStream(PlotObject *plotObject)
  {
      return SpectraType::getDataStream(plotObject, xAxisDataTableLabel(), m_yaxis);
  }

  QString RamanSpectra::xAxisLabel() const
  {
    switch (xAxisUnit()) {
      case WAVELENGTH_UM:
        return tr("Scattered Wavelength (µm)");
      case WAVELENGTH_NM:
        return tr("Scattered Wavelength (nm)");
      case ENERGY_MEV:
        return tr("Shift Energy (meV)");
      case ENERGY_KJMOL:
        return tr("Shift Energy (kJ/mol)");
      case WAVENUMBER_CM1:
      default:
        return tr("Raman Shift (cm<sup>-1</sup>)");
    }
  }

  QString RamanSpectra::xAxisDataTableLabel() const
  {
    switch (xAxisUnit()) {
      case WAVELENGTH_UM:
        return tr("Scattered Wavelength (µm)");
      case WAVELENGTH_NM:
        return tr("Scattered Wavelength (nm)");
      case ENERGY_MEV:
        return tr("Shift Energy (meV)");
      case ENERGY_KJMOL:
        return tr("Shift Energy (kJ/mol)");
      case WAVENUMBER_CM1:
      default:
        return tr("Raman Shift (cm^-1)");
    }
  }

  double RamanSpectra::displayedXFromWavenumber(double scaledWavenumber) const
  {
    if (!std::isfinite(scaledWavenumber)) {
      return 0.0;
    }
    switch (xAxisUnit()) {
      case WAVELENGTH_UM:
      case WAVELENGTH_NM: {
        // Raman wavelengths are scattered-light wavelengths: 1/lambda_s = W - shift.
        const double scatteredWavenumber = m_W - scaledWavenumber;
        if (!std::isfinite(scatteredWavenumber) ||
            scatteredWavenumber <= kMinDenominator) {
          return 0.0;
        }
        if (xAxisUnit() == WAVELENGTH_UM) {
          return 1.0e4 / scatteredWavenumber;
        }
        return 1.0e7 / scatteredWavenumber;
      }
      case ENERGY_MEV:
        return 0.1239841984 * scaledWavenumber;
      case ENERGY_KJMOL:
        return 0.01196266 * scaledWavenumber;
      case WAVENUMBER_CM1:
      default:
        return scaledWavenumber;
    }
  }

  void RamanSpectra::nativeXBounds(double &xMin, double &xMax) const
  {
    AbstractIRSpectra::nativeXBounds(xMin, xMax);
    if (xAxisUnit() == WAVELENGTH_UM || xAxisUnit() == WAVELENGTH_NM) {
      xMin = std::max(xMin, kMinPositiveShift);
      const double scatteredLimit = m_W - kMinDenominator;
      if (std::isfinite(scatteredLimit)) {
        xMax = std::min(xMax, scatteredLimit);
      }
    }
  }

  bool RamanSpectra::isNativeXValidForCurrentAxis(double x) const
  {
    if (!AbstractIRSpectra::isNativeXValidForCurrentAxis(x)) {
      return false;
    }
    if (xAxisUnit() == WAVELENGTH_UM || xAxisUnit() == WAVELENGTH_NM) {
      const double scatteredWavenumber = m_W - x;
      return std::isfinite(scatteredWavenumber) &&
             scatteredWavenumber > kMinDenominator;
    }
    return true;
  }

  void RamanSpectra::xAxisDefaultLimits(double &xMin, double &xMax) const
  {
    const double highShift = scaledWavenumber(3500.0);
    const double lowShift = scaledWavenumber(0.0);

    if (xAxisUnit() == WAVENUMBER_CM1) {
      xMin = highShift;
      xMax = lowShift;
      return;
    }

    double lowReference = lowShift;
    if (xAxisUnit() == WAVELENGTH_UM || xAxisUnit() == WAVELENGTH_NM) {
      bool foundPositive = false;
      for (int i = 0; i < m_xList.size(); ++i) {
        double shift = m_xList.at(i);
        if (shift > 0.0) {
          if (!foundPositive || shift < lowReference) {
            lowReference = shift;
            foundPositive = true;
          }
        }
      }
      if (!foundPositive) {
        lowReference = scaledWavenumber(1.0);
      }
    }

    double lowDisplay = displayedXFromWavenumber(lowReference);
    double highDisplay = displayedXFromWavenumber(highShift);
    if (lowDisplay < highDisplay) {
      xMin = lowDisplay;
      xMax = highDisplay;
    } else {
      xMin = highDisplay;
      xMax = lowDisplay;
    }
  }

  void RamanSpectra::updateT(double T)
  {
    m_T = T;
    emit plotDataChanged();
  }

  void RamanSpectra::updateW(double W)
  {
    m_W = W;
    emit plotDataChanged();
  }
}
