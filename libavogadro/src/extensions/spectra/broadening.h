/**********************************************************************
  SpectraDialog - Visualize spectral data from QM calculations

  Copyright (C) 2024

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

#ifndef AVOGADRO_SPECTRA_BROADENING_H
#define AVOGADRO_SPECTRA_BROADENING_H

namespace Avogadro {

namespace SpectraBroadening {

double dopplerSigma(double centerWavenumber, double temperature,
                    double molecularMassKg);
double instrumentSigma(double instrumentFwhm);
double collisionalHalfWidth(double gammaRef, double referenceTemperature,
                            double exponent, double temperature,
                            double pressure);
double pseudoVoigtEta(double sigma, double gammaL);
double pseudoVoigtValue(double x, double center, double sigma,
                        double gammaL);
double pseudoVoigtFwhm(double sigma, double gammaL);

} // namespace SpectraBroadening

} // namespace Avogadro

#endif // AVOGADRO_SPECTRA_BROADENING_H
