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

#include "broadening.h"

#include <algorithm>
#include <cmath>

namespace Avogadro {

namespace {
static const double boltzmannConstant = 1.380649e-23; // J K^-1
static const double speedOfLight = 2.99792458e10;     // cm s^-1

} // namespace

namespace SpectraBroadening {

double dopplerSigma(double centerWavenumber, double temperature,
                    double molecularMassKg)
{
  if (centerWavenumber <= 0.0 || temperature <= 0.0 || molecularMassKg <= 0.0)
    return 0.0;

  const double velocityScale =
    std::sqrt(boltzmannConstant * temperature / (molecularMassKg *
                                                 speedOfLight * speedOfLight));
  return centerWavenumber * velocityScale;
}

double instrumentSigma(double instrumentFwhm)
{
  if (instrumentFwhm <= 0.0)
    return 0.0;
  return instrumentFwhm / (2.0 * std::sqrt(2.0 * std::log(2.0)));
}

double collisionalHalfWidth(double gammaRef, double referenceTemperature,
                            double exponent, double temperature,
                            double pressure)
{
  if (gammaRef <= 0.0 || referenceTemperature <= 0.0 ||
      temperature <= 0.0 || pressure <= 0.0)
    return 0.0;

  const double scaledGamma = gammaRef *
                             std::pow(referenceTemperature / temperature,
                                       exponent) * pressure;
  return scaledGamma;
}

double pseudoVoigtEta(double sigma, double gammaL)
{
  if (sigma <= 0.0 && gammaL <= 0.0)
    return 0.0;

  if (gammaL <= 0.0)
    return 0.0;

  const double a = gammaL / (gammaL + 2.35482 * sigma);
  const double eta = 1.36603 * a - 0.47719 * a * a + 0.11116 * a * a * a;
  return std::min(std::max(eta, 0.0), 1.0);
}

double pseudoVoigtValue(double x, double center, double sigma, double gammaL)
{
  if (sigma <= 0.0 && gammaL <= 0.0)
    return 0.0;

  const double dx = x - center;
  const double gaussian =
    sigma > 0.0 ? std::exp(-(dx * dx) / (2.0 * sigma * sigma)) /
                    (sigma * std::sqrt(2.0 * M_PI))
                : 0.0;
  const double lorentzian =
    gammaL > 0.0 ? gammaL / (M_PI * (dx * dx + gammaL * gammaL)) : 0.0;
  const double eta = pseudoVoigtEta(sigma, gammaL);
  return eta * lorentzian + (1.0 - eta) * gaussian;
}

double pseudoVoigtFwhm(double sigma, double gammaL)
{
  if (sigma <= 0.0 && gammaL <= 0.0)
    return 0.0;

  const double gaussianFwhm = 2.0 * std::sqrt(2.0 * std::log(2.0)) * sigma;
  const double lorentzianFwhm = 2.0 * gammaL;
  return 0.5346 * lorentzianFwhm +
         std::sqrt(0.2166 * lorentzianFwhm * lorentzianFwhm +
                   gaussianFwhm * gaussianFwhm);
}

} // namespace SpectraBroadening

} // namespace Avogadro
