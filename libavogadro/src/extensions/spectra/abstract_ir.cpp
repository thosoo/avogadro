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

#include "broadening.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace Avogadro {
  AbstractIRSpectra::AbstractIRSpectra( SpectraDialog *parent ) :
    SpectraType( parent ), m_scale(0.0), m_fwhm(0.0), m_labelYThreshold(0.0),
    m_temperature(298.15), m_pressure(1.0), m_gammaRef(0.0), m_tempExponent(0.0),
    m_referenceTemperature(296.0), m_nPoints(10), m_molecularMassKg(0.0)
    {
    ui.setupUi(m_tab_widget);

    ui.spin_gammaRef->setToolTip(tr("Pressure-broadening HWHM at the reference temperature (cm⁻¹/atm). Leave at 0 to use Doppler-only broadening."));
    ui.spin_tempExponent->setToolTip(tr("Temperature exponent n for the pressure broadening law."));
    ui.spin_referenceTemperature->setToolTip(tr("Reference temperature T₀ for the collisional width (K)."));

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
    connect(ui.spin_temperature, SIGNAL(valueChanged(double)),
            this, SLOT(updateTemperature(double)));
    connect(ui.spin_pressure, SIGNAL(valueChanged(double)),
            this, SLOT(updatePressure(double)));
    connect(ui.spin_gammaRef, SIGNAL(valueChanged(double)),
            this, SLOT(updateGammaRef(double)));
    connect(ui.spin_tempExponent, SIGNAL(valueChanged(double)),
            this, SLOT(updateTemperatureExponent(double)));
    connect(ui.spin_referenceTemperature, SIGNAL(valueChanged(double)),
            this, SLOT(updateReferenceTemperature(double)));
    connect(ui.combo_scalingType, SIGNAL(currentIndexChanged(int)),
            this, SLOT(changeScalingType(int)));
  }

  void AbstractIRSpectra::getCalculatedPlotObject(PlotObject *plotObject) {
    plotObject->clearPoints();

    if (m_xList.isEmpty())
      return;

    m_nPoints = ui.spin_nPoints->value();

    QList<double> sigmas;
    QList<double> gammaL;
    sigmas.reserve(m_xList.size());
    gammaL.reserve(m_xList.size());

    const double instrument = instrumentSigma();
    const double mass = molecularMass();

    double maxFwhm = 0.0;
    for (int i = 0; i < m_xList.size(); ++i) {
      const double nu0 = m_xList.at(i);
      const double sigmaD = SpectraBroadening::dopplerSigma(nu0, m_temperature,
                                                            mass);
      const double totalSigma = std::sqrt(sigmaD * sigmaD +
                                          instrument * instrument);
      sigmas.append(totalSigma);
      const double gamma = SpectraBroadening::collisionalHalfWidth(
        m_gammaRef, m_referenceTemperature, m_tempExponent, m_temperature,
        m_pressure);
      gammaL.append(gamma);
      maxFwhm = std::max(maxFwhm,
                         SpectraBroadening::pseudoVoigtFwhm(totalSigma,
                                                            gamma));
    }

    const double fwhmForGrid = (maxFwhm > 0.0) ? maxFwhm : 1.0;
    QList<double> xPoints = getXPoints(fwhmForGrid, m_nPoints);
    for (int i = 0; i < xPoints.size(); i++) {
      double x = xPoints.at(i);// already scaled!
      double y = 0;
      for (int j = 0; j < m_yList.size(); j++) {
        const double t = m_yList.at(j);
        const double w = m_xList.at(j);// already scaled!
        y += t * SpectraBroadening::pseudoVoigtValue(x, w, sigmas.at(j),
                                                     gammaL.at(j));
      }
      plotObject->addPoint(x,y);
    }

    // Normalization is probably screwed up, so renormalize the data
    double min, max;
    min = max = plotObject->points().first()->y();
    for(int i = 0; i< plotObject->points().size(); i++) {
      double cur = plotObject->points().at(i)->y();
      if (cur < min) min = cur;
      if (cur > max) max = cur;
    }
    if (max - min == 0.0)
      return;
    for(int i = 0; i< plotObject->points().size(); i++) {
      double cur = plotObject->points().at(i)->y();
      plotObject->points().at(i)->setY( (cur - min) * 100 / (max - min));
    }
  }

  void AbstractIRSpectra::rescaleFrequencies()
  {
    for (int i=0; i<m_xList_orig.size(); i++) {
      m_xList[i] = m_xList_orig.at(i) * scale(m_xList.at(i));
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

  void AbstractIRSpectra::updateTemperature(double value)
  {
    if (m_temperature == value)
      return;
    m_temperature = value;
    updateBroadeningParameters();
  }

  void AbstractIRSpectra::updatePressure(double value)
  {
    if (m_pressure == value)
      return;
    m_pressure = value;
    updateBroadeningParameters();
  }

  void AbstractIRSpectra::updateGammaRef(double value)
  {
    if (m_gammaRef == value)
      return;
    m_gammaRef = value;
    updateBroadeningParameters();
  }

  void AbstractIRSpectra::updateTemperatureExponent(double value)
  {
    if (m_tempExponent == value)
      return;
    m_tempExponent = value;
    updateBroadeningParameters();
  }

  void AbstractIRSpectra::updateReferenceTemperature(double value)
  {
    if (m_referenceTemperature == value)
      return;
    m_referenceTemperature = value;
    updateBroadeningParameters();
  }

  void AbstractIRSpectra::updateYAxis(QString text) {
    if (m_yaxis == text) {
      return;
    }
    m_dialog->getUi()->plot->axis(PlotWidget::LeftAxis)->setLabel(text);
    m_yaxis = text;
    emit plotDataChanged();
  }

  void AbstractIRSpectra::changeScalingType(int type)
  {
    m_scalingType = static_cast<ScalingType>(type);
    rescaleFrequencies();
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

  double AbstractIRSpectra::instrumentSigma() const
  {
    return SpectraBroadening::instrumentSigma(m_fwhm);
  }

  double AbstractIRSpectra::molecularMass() const
  {
    // Default to 1 amu if we failed to gather a valid molecular mass.
    const double protonMassKg = 1.66053906660e-27;
    return (m_molecularMassKg > 0.0) ? m_molecularMassKg : protonMassKg;
  }

  void AbstractIRSpectra::updateBroadeningParameters()
  {
    emit plotDataChanged();
  }

  double AbstractIRSpectra::scale(double w)
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
