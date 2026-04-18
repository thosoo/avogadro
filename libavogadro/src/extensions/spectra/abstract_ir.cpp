/**********************************************************************
  SpectraDialog - Visualize spectral data from QM calculations

  Copyright (C) 2009 by David Lonie
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

#include "abstract_ir.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QtGlobal>

using namespace std;

namespace Avogadro {
  namespace {
    const double kMinDenominator = 1.0e-12;
    const double kMinPositiveWavenumber = 1.0e-8;
  }

  AbstractIRSpectra::AbstractIRSpectra( SpectraDialog *parent ) :
    SpectraType( parent ), m_scale(0.0), m_fwhm(0.0), m_labelYThreshold(0.0),
    m_scalingType(LINEAR), m_broadeningModel(CONSTANT_WIDTH),
    m_xAxisUnit(WAVENUMBER_CM1), m_lineShape(GAUSSIAN), m_nPoints(10),
    m_temperature(298.15), m_referenceTemperature(298.15), m_modelExponent(1.0),
    m_baselineWidth(0.0), m_anharmonicAmplitude(0.0), m_voigtMix(0.5)
    {
    ui.setupUi(m_tab_widget);

    // Setup signals/slots    
    connect(this, SIGNAL(plotDataChanged()),
            m_dialog, SLOT(regenerateCalculatedSpectra()));
    connect(ui.cb_labelPeaks, SIGNAL(toggled(bool)),
            this, SLOT(toggleLabels(bool)));
    connect(ui.spin_threshold, SIGNAL(valueChanged(double)),
            this, SLOT(updateThreshold(double)));
    connect(ui.spin_scale, SIGNAL(valueChanged(double)),
            this, SLOT(updateScaleSlider(double)));
    connect(ui.hs_scale, SIGNAL(sliderPressed()),
            this, SLOT(scaleSliderPressed()));
    connect(ui.hs_scale, SIGNAL(sliderReleased()),
            this, SLOT(scaleSliderReleased()));
    connect(ui.hs_scale, SIGNAL(valueChanged(int)),
            this, SLOT(updateScaleSpin(int)));
    connect(ui.spin_FWHM, SIGNAL(valueChanged(double)),
            this, SLOT(updateFWHMSlider(double)));
    connect(ui.hs_FWHM, SIGNAL(sliderPressed()),
            this, SLOT(fwhmSliderPressed()));
    connect(ui.hs_FWHM, SIGNAL(sliderReleased()),
            this, SLOT(fwhmSliderReleased()));
    connect(ui.hs_FWHM, SIGNAL(valueChanged(int)),
            this, SLOT(updateFWHMSpin(int)));
    connect(ui.spin_nPoints, SIGNAL(valueChanged(int)),
            this, SIGNAL(plotDataChanged()));
    connect(ui.combo_yaxis, SIGNAL(currentIndexChanged(QString)),
            this, SLOT(updateYAxis(QString)));
    connect(ui.combo_xaxis, SIGNAL(currentIndexChanged(int)),
            this, SLOT(updateXAxis(int)));
    connect(ui.combo_scalingType, SIGNAL(currentIndexChanged(int)),
            this, SLOT(changeScalingType(int)));
    connect(ui.combo_lineShape, SIGNAL(currentIndexChanged(int)),
            this, SLOT(changeLineShape(int)));
    connect(ui.combo_broadeningModel, SIGNAL(currentIndexChanged(int)),
            this, SLOT(changeBroadeningModel(int)));
    connect(ui.spin_temperature, SIGNAL(valueChanged(double)),
            this, SLOT(updateTemperature(double)));
    connect(ui.spin_referenceTemperature, SIGNAL(valueChanged(double)),
            this, SLOT(updateReferenceTemperature(double)));
    connect(ui.spin_modelExponent, SIGNAL(valueChanged(double)),
            this, SLOT(updateModelExponent(double)));
    connect(ui.spin_zeroWidth, SIGNAL(valueChanged(double)),
            this, SLOT(updateBaselineWidth(double)));
    connect(ui.spin_anharmonicAmplitude, SIGNAL(valueChanged(double)),
            this, SLOT(updateAnharmonicAmplitude(double)));
    connect(ui.spin_voigtMix, SIGNAL(valueChanged(double)),
            this, SLOT(updateVoigtMix(double)));
    updateBroadeningControls();
  }

  void AbstractIRSpectra::getCalculatedPlotObject(PlotObject *plotObject) {
    plotObject->clearPoints();

    if (!hasEffectiveBroadening()) { // get singlets
      if (m_xList.isEmpty()) {
        return;
      }
      double minWavenumber = m_xList.at(0);
      double maxWavenumber = m_xList.at(0);
      for (int i = 1; i < m_xList.size(); ++i) {
        if (m_xList.at(i) < minWavenumber) {
          minWavenumber = m_xList.at(i);
        }
        if (m_xList.at(i) > maxWavenumber) {
          maxWavenumber = m_xList.at(i);
        }
      }
      if (std::isfinite(minWavenumber)) {
        plotObject->addPoint(minWavenumber, 0);
      }

      for (int i = 0; i < m_yList.size(); i++) {
        double wavenumber = m_xList.at(i);// already scaled!
        double transmittance = m_yList.at(i);
        if (!std::isfinite(wavenumber) || !std::isfinite(transmittance) ||
            !isNativeXValidForCurrentAxis(wavenumber)) {
          continue;
        }
        plotObject->addPoint ( wavenumber, 0 );
        if (ui.cb_labelPeaks->isChecked()) {
          // %L1 uses localized number format (e.g., 1.023,4 in Europe)
          plotObject->addPoint( wavenumber, transmittance, QString("%L1").arg(wavenumber, 0, 'f', 1) );
        }
        else {
          plotObject->addPoint( wavenumber, transmittance );
        }
        plotObject->addPoint( wavenumber, 0 );
      }
      if (std::isfinite(maxWavenumber)) {
        plotObject->addPoint(maxWavenumber, 0);
      }
    } // End singlets

    else { // Get broadened peaks in native wavenumber / shift space
        m_nPoints = ui.spin_nPoints->value();
        addBroadenedPeaks(plotObject);
      // Normalization is probably screwed up, so renormalize the data
      double min, max;
      if (plotObject->points().isEmpty()) {
        return;
      }
      bool haveFinite = false;
      min = std::numeric_limits<double>::infinity();
      max = -std::numeric_limits<double>::infinity();
      for(int i = 0; i< plotObject->points().size(); i++) {
        double cur = plotObject->points().at(i)->y();
        if (!std::isfinite(cur)) {
          continue;
        }
        if (cur < min) min = cur;
        if (cur > max) max = cur;
        haveFinite = true;
      }
      if (!haveFinite || !std::isfinite(min) || !std::isfinite(max) ||
          !(max > min) || !std::isfinite(max - min)) {
        for(int i = 0; i< plotObject->points().size(); i++) {
          plotObject->points().at(i)->setY(0.0);
        }
      } else {
        const double range = max - min;
        for(int i = 0; i< plotObject->points().size(); i++) {
          double cur = plotObject->points().at(i)->y();
          if (!std::isfinite(cur)) {
            plotObject->points().at(i)->setY(0.0);
            continue;
          }
          const double normalized = (cur - min) * 100.0 / range;
          if (!std::isfinite(normalized)) {
            plotObject->points().at(i)->setY(0.0);
            continue;
          }
          plotObject->points().at(i)->setY(normalized);
        }
      }
    } // End gaussians

    convertPlotObjectXToDisplayUnits(plotObject);
  }

  void AbstractIRSpectra::rescaleFrequencies()
  {
    for (int i=0; i<m_xList_orig.size(); i++) {
      m_xList[i] = scaledWavenumber(m_xList_orig.at(i));
    }
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateScaleSpin(int intScale)
  {
    double scale = intScale*0.01;
    if (scale == m_scale) return;
    m_scale = scale;
    ui.spin_scale->setValue(scale);
    rescaleFrequencies();
  }

  void AbstractIRSpectra::updateScaleSlider(double scale)
  {
    int intScale = static_cast<int>(scale*100);
    disconnect(ui.hs_scale, SIGNAL(valueChanged(int)),
      this, SLOT(updateScaleSpin(int)));
    ui.hs_scale->setValue(intScale);
    connect(ui.hs_scale, SIGNAL(valueChanged(int)),
      this, SLOT(updateScaleSpin(int)));
    m_scale = scale;
    rescaleFrequencies();
  }

  void AbstractIRSpectra::scaleSliderPressed()
  {
      disconnect(ui.spin_scale, SIGNAL(valueChanged(double)),
            this, SLOT(updateScaleSlider(double)));
  }

  void AbstractIRSpectra::scaleSliderReleased()
  {
      connect(ui.spin_scale, SIGNAL(valueChanged(double)),
            this, SLOT(updateScaleSlider(double)));
  }
  
  void AbstractIRSpectra::updateFWHMSpin(int fwhm)
  {
    if (fwhm == m_fwhm) return;
    m_fwhm = fwhm;
    ui.spin_FWHM->setValue(m_fwhm);
    emit plotDataChanged();
  }
  
  void AbstractIRSpectra::updateFWHMSlider(double fwhm)
  {    
    disconnect(ui.hs_FWHM, SIGNAL(valueChanged(int)),
      this, SLOT(updateFWHMSpin(int)));
    ui.hs_FWHM->setValue(fwhm);
    connect(ui.hs_FWHM, SIGNAL(valueChanged(int)),
      this, SLOT(updateFWHMSpin(int)));
    m_fwhm = fwhm;
    emit plotDataChanged();
  }
  
  void AbstractIRSpectra::fwhmSliderPressed()
  {
      disconnect(ui.spin_FWHM, SIGNAL(valueChanged(double)),
            this, SLOT(updateFWHMSlider(double)));
  }
  
  void AbstractIRSpectra::fwhmSliderReleased()
  {
      connect(ui.spin_FWHM, SIGNAL(valueChanged(double)),
            this, SLOT(updateFWHMSlider(double)));
  }

  void AbstractIRSpectra::updateYAxis(QString text) {
    if (m_yaxis == text) {
      return;
    }
    m_yaxis = text;
    updatePlotLabels();
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateXAxis(int index)
  {
    if (index < 0 || index >= ui.combo_xaxis->count()) {
      return;
    }
    XAxisUnit unit = static_cast<XAxisUnit>(index);
    if (m_xAxisUnit == unit) {
      return;
    }
    m_xAxisUnit = unit;
    updatePlotLabels();
    m_dialog->regenerateImportedSpectra();
    emit plotDataChanged();
  }

  void AbstractIRSpectra::changeScalingType(int type)
  {
    m_scalingType = static_cast<ScalingType>(type);
    rescaleFrequencies();
  }
  void AbstractIRSpectra::changeLineShape(int type)
  {
    m_lineShape = static_cast<LineShape>(type);
    updateBroadeningControls();
    emit plotDataChanged();
  }

  void AbstractIRSpectra::changeBroadeningModel(int type)
  {
    m_broadeningModel = static_cast<BroadeningModel>(type);
    updateBroadeningControls();
    emit plotDataChanged();
  }

  void AbstractIRSpectra::toggleLabels(bool enabled)
  {
    ui.spin_threshold->setEnabled(enabled);   
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateThreshold(double t)
  {
    m_labelYThreshold = t;
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateTemperature(double t)
  {
    m_temperature = t;
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateReferenceTemperature(double t)
  {
    m_referenceTemperature = t;
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateModelExponent(double n)
  {
    m_modelExponent = n;
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateBaselineWidth(double w)
  {
    m_baselineWidth = w;
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateAnharmonicAmplitude(double a)
  {
    m_anharmonicAmplitude = a;
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateVoigtMix(double eta)
  {
    m_voigtMix = eta;
    emit plotDataChanged();
  }

  void AbstractIRSpectra::updateBroadeningControls()
  {
    bool needsTemperature = (m_broadeningModel != CONSTANT_WIDTH);
    bool needsReference = (m_broadeningModel == DOPPLER_LIKE ||
                           m_broadeningModel == COLLISIONAL_HOMOGENEOUS);
    bool needsExponent = (m_broadeningModel == COLLISIONAL_HOMOGENEOUS);
    bool needsAnharmonic = (m_broadeningModel == ANHARMONIC_PHONON);
    bool isVoigt = (m_lineShape == VOIGT);

    ui.spin_temperature->setEnabled(needsTemperature);
    ui.label_temperature->setEnabled(needsTemperature);
    ui.spin_referenceTemperature->setEnabled(needsReference);
    ui.label_referenceTemperature->setEnabled(needsReference);
    ui.spin_modelExponent->setEnabled(needsExponent);
    ui.label_modelExponent->setEnabled(needsExponent);
    ui.spin_zeroWidth->setEnabled(needsAnharmonic);
    ui.label_zeroWidth->setEnabled(needsAnharmonic);
    ui.spin_anharmonicAmplitude->setEnabled(needsAnharmonic);
    ui.label_anharmonicAmplitude->setEnabled(needsAnharmonic);
    ui.spin_voigtMix->setEnabled(isVoigt);
    ui.label_voigtMix->setEnabled(isVoigt);
  }

  QStringList AbstractIRSpectra::dataTableHeaders() const
  {
    return QStringList() << xAxisDataTableLabel() << m_yaxis;
  }

  double AbstractIRSpectra::displayXValue(double x) const
  {
    return displayedXFromWavenumber(x);
  }

  QString AbstractIRSpectra::xAxisLabel() const
  {
    switch (m_xAxisUnit) {
      case WAVELENGTH_UM:
        return tr("Wavelength (µm)");
      case WAVELENGTH_NM:
        return tr("Wavelength (nm)");
      case ENERGY_MEV:
        return tr("Energy (meV)");
      case ENERGY_KJMOL:
        return tr("Energy (kJ/mol)");
      case WAVENUMBER_CM1:
      default:
        return tr("Wavenumber (cm<sup>-1</sup>)");
    }
  }

  QString AbstractIRSpectra::xAxisDataTableLabel() const
  {
    switch (m_xAxisUnit) {
      case WAVELENGTH_UM:
        return tr("Wavelength (µm)");
      case WAVELENGTH_NM:
        return tr("Wavelength (nm)");
      case ENERGY_MEV:
        return tr("Energy (meV)");
      case ENERGY_KJMOL:
        return tr("Energy (kJ/mol)");
      case WAVENUMBER_CM1:
      default:
        return tr("Wavenumber (cm^-1)");
    }
  }

  void AbstractIRSpectra::setXAxisUnit(XAxisUnit unit)
  {
    if (unit < WAVENUMBER_CM1 || unit > ENERGY_KJMOL) {
      unit = WAVENUMBER_CM1;
    }
    if (ui.combo_xaxis->currentIndex() == static_cast<int>(unit)) {
      updateXAxis(static_cast<int>(unit));
      return;
    }
    ui.combo_xaxis->setCurrentIndex(static_cast<int>(unit));
  }

  double AbstractIRSpectra::scaledWavenumber(double originalWavenumber) const
  {
    return originalWavenumber * scale(originalWavenumber);
  }

  double AbstractIRSpectra::displayedXFromWavenumber(double scaledWavenumber) const
  {
    if (!std::isfinite(scaledWavenumber)) {
      return 0.0;
    }
    switch (m_xAxisUnit) {
      case WAVELENGTH_UM:
        if (std::abs(scaledWavenumber) < kMinDenominator) {
          return 0.0;
        }
        return 1.0e4 / scaledWavenumber;
      case WAVELENGTH_NM:
        if (std::abs(scaledWavenumber) < kMinDenominator) {
          return 0.0;
        }
        return 1.0e7 / scaledWavenumber;
      case ENERGY_MEV:
        return 0.1239841984 * scaledWavenumber;
      case ENERGY_KJMOL:
        return 0.01196266 * scaledWavenumber;
      case WAVENUMBER_CM1:
      default:
        return scaledWavenumber;
    }
  }

  void AbstractIRSpectra::convertPlotObjectXToDisplayUnits(PlotObject *plotObject) const
  {
    QList<double> xValues;
    QList<double> yValues;
    QStringList labels;
    for (int i = 0; i < plotObject->points().size(); ++i) {
      PlotPoint *point = plotObject->points().at(i);
      const double nativeX = point->x();
      const double y = point->y();
      if (!std::isfinite(nativeX) || !std::isfinite(y) ||
          !isNativeXValidForCurrentAxis(nativeX)) {
        continue;
      }
      const double displayX = displayedXFromWavenumber(nativeX);
      if (!std::isfinite(displayX)) {
        continue;
      }
      xValues.append(displayX);
      yValues.append(y);
      if (!point->label().isEmpty()) {
        labels.append(QString("%L1").arg(displayX, 0, 'f', 1));
      } else {
        labels.append(QString());
      }
    }
    plotObject->clearPoints();
    for (int i = 0; i < xValues.size(); ++i) {
      if (labels.at(i).isEmpty()) {
        plotObject->addPoint(xValues.at(i), yValues.at(i));
      } else {
        plotObject->addPoint(xValues.at(i), yValues.at(i), labels.at(i));
      }
    }
  }

  void AbstractIRSpectra::updatePlotLabels()
  {
    m_dialog->getUi()->plot->axis(PlotWidget::BottomAxis)->setLabel(xAxisLabel());
    m_dialog->getUi()->plot->axis(PlotWidget::LeftAxis)->setLabel(m_yaxis);
  }

  void AbstractIRSpectra::xAxisDefaultLimits(double &xMin, double &xMax) const
  {
    const double highWavenumber = scaledWavenumber(3500.0);
    const double lowWavenumber = scaledWavenumber(400.0);

    if (m_xAxisUnit == WAVENUMBER_CM1) {
      xMin = highWavenumber;
      xMax = lowWavenumber;
      return;
    }

    double lowDisplay = displayedXFromWavenumber(lowWavenumber);
    double highDisplay = displayedXFromWavenumber(highWavenumber);
    if (lowDisplay < highDisplay) {
      xMin = lowDisplay;
      xMax = highDisplay;
    } else {
      xMin = highDisplay;
      xMax = lowDisplay;
    }
  }

  void AbstractIRSpectra::nativeXBounds(double &xMin, double &xMax) const
  {
    xMin = -std::numeric_limits<double>::infinity();
    xMax = std::numeric_limits<double>::infinity();
    if (m_xAxisUnit == WAVELENGTH_UM || m_xAxisUnit == WAVELENGTH_NM) {
      xMin = kMinPositiveWavenumber;
    }
  }

  bool AbstractIRSpectra::isNativeXValidForCurrentAxis(double x) const
  {
    if (!std::isfinite(x)) {
      return false;
    }
    switch (m_xAxisUnit) {
      case WAVELENGTH_UM:
      case WAVELENGTH_NM:
        return x > kMinPositiveWavenumber;
      case ENERGY_MEV:
      case ENERGY_KJMOL:
      case WAVENUMBER_CM1:
      default:
        return true;
    }
  }

  double AbstractIRSpectra::modeledFwhm(double wavenumber) const
  {
    const double t = std::max(1.0, m_temperature);
    const double tRef = std::max(1.0, m_referenceTemperature);
    switch (m_broadeningModel) {
      case DOPPLER_LIKE:
        return m_fwhm * sqrt(t / tRef);
      case COLLISIONAL_HOMOGENEOUS:
        return m_fwhm * pow(tRef / t, m_modelExponent);
      case ANHARMONIC_PHONON: {
        if (wavenumber <= 0.0) {
          return m_baselineWidth + m_anharmonicAmplitude;
        }
        const double c2 = 1.438776877; // K*cm
        const double arg = c2 * (wavenumber * 0.5) / t;
        double nBE = 0.0;
        if (arg < 700.0) {
          nBE = 1.0 / (exp(arg) - 1.0);
        }
        return m_baselineWidth + m_anharmonicAmplitude * (1.0 + 2.0 * nBE);
      }
      case CONSTANT_WIDTH:
      default:
        return m_fwhm;
    }
  }

  double AbstractIRSpectra::effectiveMaxFwhm() const
  {
    double maxFwhm = 0.0;
    for (int i = 0; i < m_xList.size(); ++i) {
      maxFwhm = std::max(maxFwhm, modeledFwhm(m_xList.at(i)));
    }
    return maxFwhm;
  }

  bool AbstractIRSpectra::hasEffectiveBroadening() const
  {
    const double linewidthTolerance = 1.0e-3;
    return effectiveMaxFwhm() > linewidthTolerance;
  }

  void AbstractIRSpectra::addBroadenedPeaks(PlotObject *plotObject)
  {
    if (m_xList.isEmpty()) {
      return;
    }

    double maxFwhm = effectiveMaxFwhm();
    if (maxFwhm <= 1.0e-3) {
      return;
    }

    QList<double> xPoints = getXPoints(maxFwhm, m_nPoints);
    double nativeMin = 0.0, nativeMax = 0.0;
    nativeXBounds(nativeMin, nativeMax);
    const bool useMinBound = std::isfinite(nativeMin);
    const bool useMaxBound = std::isfinite(nativeMax);
    for (int i = 0; i < xPoints.size(); ++i) {
      double x = xPoints.at(i);
      if (!std::isfinite(x)) {
        continue;
      }
      if (useMinBound && x < nativeMin) {
        continue;
      }
      if (useMaxBound && x > nativeMax) {
        continue;
      }
      if (!isNativeXValidForCurrentAxis(x)) {
        continue;
      }
      double y = 0.0;
      bool yValid = true;
      for (int j = 0; j < m_yList.size(); ++j) {
        const double center = m_xList.at(j);
        const double intensity = m_yList.at(j);
        if (!std::isfinite(center) || !std::isfinite(intensity)) {
          continue;
        }
        const double fwhm = std::max(0.01, modeledFwhm(center));
        if (!std::isfinite(fwhm)) {
          continue;
        }
        const double gaussianSigma = fwhm / (2.0 * sqrt(2.0 * log(2.0)));
        if (!std::isfinite(gaussianSigma) || gaussianSigma <= kMinDenominator) {
          continue;
        }
        // Use area-normalized kernels for all shapes so width changes are
        // consistent across Gaussian/Lorentzian/Pseudo-Voigt models.
        static const double kPi = 3.14159265358979323846;
        const double delta = x - center;
        const double exponent = -((delta * delta) /
                                  (2.0 * gaussianSigma * gaussianSigma));
        const double gaussian = (1.0 / (gaussianSigma * sqrt(2.0 * kPi))) *
          exp(exponent);
        const double hwhm = fwhm * 0.5;
        if (!std::isfinite(hwhm) || hwhm <= kMinDenominator) {
          continue;
        }
        const double lorentzDenominator = (delta * delta + hwhm * hwhm);
        if (!std::isfinite(lorentzDenominator) ||
            lorentzDenominator <= kMinDenominator) {
          continue;
        }
        const double lorentz =
          hwhm / (kPi * lorentzDenominator);
        if (!std::isfinite(gaussian) || !std::isfinite(lorentz)) {
          continue;
        }

        double contribution = 0.0;
        if (m_lineShape == LORENTZIAN) {
          contribution = intensity * lorentz;
        } else if (m_lineShape == VOIGT) {
          // Pseudo-Voigt approximation: linear mix of normalized Gaussian and
          // Lorentzian kernels (not a full Voigt convolution).
          contribution = intensity * (m_voigtMix * lorentz +
            (1.0 - m_voigtMix) * gaussian);
        } else {
          contribution = intensity * gaussian;
        }
        if (!std::isfinite(contribution)) {
          yValid = false;
          break;
        }
        y += contribution;
        if (!std::isfinite(y)) {
          yValid = false;
          break;
        }
      }
      if (!yValid || !std::isfinite(y)) {
        continue;
      }
      plotObject->addPoint(x, y);
#ifndef NDEBUG
      Q_ASSERT(std::isfinite(x));
      Q_ASSERT(std::isfinite(y));
#endif
    }
  }

  double AbstractIRSpectra::scale(double w) const
  {
    switch(m_scalingType) {
      case LINEAR:
        return m_scale;
        break;
      case RELATIVE:
        return 1-w*(1-m_scale)/1000;
        break;
    //TODO: add other scaling algorithms
      default:
        return m_scale;
    }
  }
}
