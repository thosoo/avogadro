#include "config.h"

#include <QtTest>

#include <cmath>

#include <extensions/spectra/broadening.h>

using namespace Avogadro::SpectraBroadening;

class IRBroadeningTest : public QObject
{
  Q_OBJECT

private slots:
  void dopplerWidths();
  void collisionalWidths();
};

namespace {
double nitrogenMassKg()
{
  return 28.0 / (6.02214076e23 * 1000.0);
}
}

void IRBroadeningTest::dopplerWidths()
{
  const double center = 2000.0; // cm^-1
  const double mass = nitrogenMassKg();

  const double sigma77 = dopplerSigma(center, 77.0, mass);
  const double sigma296 = dopplerSigma(center, 296.0, mass);
  const double sigma2000 = dopplerSigma(center, 2000.0, mass);

  QVERIFY(fabs(sigma77 - 1.0087713332543857e-05) < 1e-10);
  QVERIFY(fabs(sigma296 - 1.9778494237972572e-05) < 1e-10);
  QVERIFY(fabs(sigma2000 - 5.1411747679588665e-05) < 1e-10);
  QVERIFY(sigma77 < sigma296);
  QVERIFY(sigma296 < sigma2000);
}

void IRBroadeningTest::collisionalWidths()
{
  const double gammaRef = 0.1;
  const double exponent = 0.7;
  const double refTemp = 296.0;

  QCOMPARE(collisionalHalfWidth(gammaRef, refTemp, exponent, refTemp, 0.0), 0.0);

  const double gammaLowTemp = collisionalHalfWidth(gammaRef, refTemp, exponent,
                                                   77.0, 1.0);
  QVERIFY(fabs(gammaLowTemp - 0.25666147757047403) < 1e-10);

  const double gammaRefTemp = collisionalHalfWidth(gammaRef, refTemp, exponent,
                                                   refTemp, 10.0);
  QVERIFY(fabs(gammaRefTemp - 1.0) < 1e-12);

  const double gammaHighTemp = collisionalHalfWidth(gammaRef, refTemp, exponent,
                                                    2000.0, 1.0);
  QVERIFY(fabs(gammaHighTemp - 0.026253257651190548) < 1e-10);
}

QTEST_MAIN(IRBroadeningTest)
#include "moc_irbroadeningtest.cpp"
