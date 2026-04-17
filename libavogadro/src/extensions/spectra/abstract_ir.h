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

#ifndef SPECTRATYPE_ABSTRACT_IR_H
#define SPECTRATYPE_ABSTRACT_IR_H

#include "spectradialog.h"
#include "spectratype.h"
#include "ui_tab_ir_raman.h"

namespace Avogadro {
    
  enum ScalingType { LINEAR, RELATIVE };
  enum BroadeningModel {
    CONSTANT_WIDTH = 0,
    DOPPLER_LIKE,
    COLLISIONAL_HOMOGENEOUS,
    ANHARMONIC_PHONON
  };
  enum XAxisUnit {
    WAVENUMBER_CM1 = 0,
    WAVELENGTH_UM,
    WAVELENGTH_NM,
    ENERGY_MEV,
    ENERGY_KJMOL
  };


  // Abstract data type - no instance of it can be created
  class AbstractIRSpectra : public SpectraType
  {
    Q_OBJECT

  public:
    AbstractIRSpectra( SpectraDialog *parent = 0 );
    virtual void setupPlot(PlotWidget * plot) = 0;
    void getCalculatedPlotObject(PlotObject *plotObject);
    QStringList dataTableHeaders() const override;
    double displayXValue(double x) const override;
    virtual QString xAxisLabel() const;
    virtual QString xAxisDataTableLabel() const;
    void setXAxisUnit(XAxisUnit unit);
    XAxisUnit xAxisUnit() const { return m_xAxisUnit; }
    
  protected slots:
    void toggleLabels(bool);
    void updateThreshold(double);
    void updateScaleSpin(int);
    void updateScaleSlider(double);
    void scaleSliderPressed();
    void scaleSliderReleased();
    void updateFWHMSpin(int);
    void updateFWHMSlider(double);
    void fwhmSliderPressed();
    void fwhmSliderReleased();    
    void changeScalingType(int);
    void changeLineShape(int);
    void changeBroadeningModel(int);
    void updateYAxis(QString);
    void updateXAxis(int);
    void rescaleFrequencies();
    void updateTemperature(double);
    void updateReferenceTemperature(double);
    void updateModelExponent(double);
    void updateBaselineWidth(double);
    void updateAnharmonicAmplitude(double);
    void updateVoigtMix(double);
    void updateBroadeningControls();

  protected:
    double scale(double w) const;
    double scaledWavenumber(double originalWavenumber) const;
    double displayedXFromWavenumber(double scaledWavenumber) const;
    void convertPlotObjectXToDisplayUnits(PlotObject *plotObject) const;
    void updatePlotLabels();
    virtual void xAxisDefaultLimits(double &xMin, double &xMax) const;
    double modeledFwhm(double wavenumber) const;
    double effectiveMaxFwhm() const;
    bool hasEffectiveBroadening() const;
    void addBroadenedPeaks(PlotObject *plotObject);
    
    Ui::Tab_IR_Raman ui;
    double m_scale;
    double m_fwhm;
    double m_labelYThreshold;
    QString m_yaxis;
    QList<double> m_xList_orig;
    ScalingType m_scalingType;
    BroadeningModel m_broadeningModel;
    XAxisUnit m_xAxisUnit;
    LineShape m_lineShape;         // gaussian or lorentzian peaks
    int m_nPoints;                 // number of points of gaussian/lorentzian pulse
    double m_temperature;
    double m_referenceTemperature;
    double m_modelExponent;
    double m_baselineWidth;
    double m_anharmonicAmplitude;
    double m_voigtMix;
  };
}

#endif
