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
  GNU General Public icense for more details.
 ***********************************************************************/

//#ifdef OPENBABEL_IS_NEWER_THAN_2_2_99

#ifndef SPECTRATYPE_ABSTRACTXRAY_H
#define SPECTRATYPE_ABSTRACTXRAY_H

#include <QtCore/QHash>
#include <QtCore/QVariant>

#include "spectradialog.h"
#include "spectratype.h"
#include "ui_tab_xray.h"

namespace Avogadro {


#define cm_1_to_nm  1.e7
#define eV_to_nm  1.e7/8065.54477

  class AbstractXRaySpectra : public SpectraType
  {
    Q_OBJECT

  public:
    AbstractXRaySpectra( SpectraDialog *parent = 0 );

    ~AbstractXRaySpectra(){}

    virtual void setupPlot(PlotWidget * plot) = 0 ;
    
    void getCalculatedPlotObject(PlotObject *plotObject);
    //  void setImportedData(const QList<double> & xList, const QList<double> & yList);
    //  void getImportedPlotObject(PlotObject *plotObject);

    int getXminIdx(XUnits xunit);
    int getXmaxIdx(XUnits xunit);
    void getSortIdx(std::vector<double> &sortData);
    void rearrangeYList(int min, int max);

  protected slots:
    void XRayTypeChanged(const QString & str);
    void EnergyShiftSilderValueChanged(int);
    void EnergyShiftSpinValueChanged(double);
    void changeLineShape(int);

  protected:
    Ui::Tab_XRay ui;
    std::vector<int> m_idx;

    XUnits m_XUnit;
    QString m_XRayType;

    LineShape m_lineShape;         // gaussian or lorentzian peaks
    int m_nPoints;                  // number of points of gaussian/lorentzian pulse
    double m_xmin, m_xmax;          // min/ max in current xunit
    int m_XminIdx, m_XmaxIdx;       // idx of current xmin,xmax
    double m_xmin_org, m_xmax_org;  // min/max always in nm

    double m_EnergyShift;           // shift in x-axis for compatibility in experiments
    bool m_newShift;                // rescale x axis because of shift change
    bool m_resize;                  // resize x axis cause of new x unit
    bool m_newrange;                // resise x axis cause of new x range
    std::vector<double> m_wavelength;
    std::vector<double> m_wavenumber;
    std::vector<double> m_energy;

    std::vector<double> m_edipole;
    std::vector<double> m_velosity;
    std::vector<double> m_combined;
    std::vector<double> m_D2;
    std::vector<double> m_M2;
    std::vector<double> m_Q2;
  };
}

#endif
//#endif
