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

#ifndef SPECTRATYPE_ENERGY_H
#define SPECTRATYPE_ENERGY_H

#include <QHash>
#include <QVariant>

#include "spectradialog.h"
#include "spectratype.h"
#include "ui_tab_energy.h"

enum  YEnergyUnit {Y_ENERGY_kJ, Y_ENERGY_kcal, Y_ENERGY_eV};

namespace Avogadro {

  class EnergySpectra : public SpectraType
  {
    Q_OBJECT

  public:
    EnergySpectra( SpectraDialog *parent = 0 );
    ~EnergySpectra();

    void writeSettings();
    void readSettings();

    bool checkForData(Molecule* mol);
    void setupPlot(PlotWidget * plot);

    void getCalculatedPlotObject(PlotObject *plotObject);

    QString getTSV();
    QString getDataStream(PlotObject *plotObject);

  private slots:
    void energyTypeChanged(int);

  private:
    Ui::Tab_Energy ui;
    YEnergyUnit m_energyYUnit;

    QList<double> *m_yListEnergykJ, *m_yListEnergyeV, *m_yListEnergykcal;
    double m_fermi;

    std::vector<double> m_numbers;
    std::vector<double> m_energy;
    uint m_nEnergies;

  };
}

#endif
//#endif
