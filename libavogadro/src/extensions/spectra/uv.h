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

#ifndef SPECTRATYPE_UV_H
#define SPECTRATYPE_UV_H

#include <QHash>
#include <QVariant>

#include "spectradialog.h"
#include "spectratype.h"
#include "ui_tab_uv.h"

namespace Avogadro {


#define cm_1_to_nm  1.e7
#define eV_to_nm  1.e7/8065.54477

  class UVSpectra : public SpectraType
  {
    Q_OBJECT

  public:
    UVSpectra( SpectraDialog *parent = 0 );
    ~UVSpectra();

    void writeSettings();
    void readSettings();

    bool checkForData(Molecule* mol);
    void setupPlot(PlotWidget* plot);
    
    void getCalculatedPlotObject(PlotObject *plotObject);
  //  void setImportedData(const QList<double> & xList, const QList<double> & yList);
  //  void getImportedPlotObject(PlotObject *plotObject);
    QString getTSV();
    QString getDataStream(PlotObject *plotObject);

  private slots:
        void changeLineShape(int);

  private:
    Ui::Tab_UV ui;

    int getXminIdx(XUnits xunit);
    int getXmaxIdx(XUnits xunit);
    void getSortIdx(std::vector<double> &sortData);

    XUnits m_XUnit;
    YUVdata m_uvdata;

    LineShape m_lineShape;          // gaussian or lorentzian peaks
    int m_nPoints;                  // number of points of gaussian/lorentzian pulse

    std::vector<int> m_idx;         // sort index for Y data
    double m_xmin, m_xmax;          // min/ max in current xunit
    double m_xmin_org, m_xmax_org;  // min/max always in nm
    bool m_resize;                  // resize x axis cause of new x unit
    bool m_newrange;                // resise x axis cause of new x range

    std::vector<double> m_wavelength;   // wavelength in nm
    std::vector<double> m_wavenumber;   // wavelength in cm-1
    std::vector<double> m_energy;       // wavelength in eV

        std::vector<double> m_edipole;  // transition electric dipole
    //    std::vector<double> m_vdipole;    // transition velosity dipole
    //    std::vector<double> m_combined;
  };
}

#endif
//#endif
