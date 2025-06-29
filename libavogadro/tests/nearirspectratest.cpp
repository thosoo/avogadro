#include "config.h"
#include <QtTest>
#include <avogadro/molecule.h>
#include "../src/extensions/spectra/oborcanearir_stub.h"
#include <openbabel/mol.h>
#include <openbabel/generic.h>

using Avogadro::Molecule;
using OpenBabel::OBOrcaNearIRData;

class StubNearIR
{
public:
  QList<double> xList;
  QList<double> yList;
  bool checkForData(Molecule* mol) {
    OpenBabel::OBMol obmol = mol->OBMol();
    OBOrcaNearIRData* ond = static_cast<OBOrcaNearIRData*>(obmol.GetData("OrcaNearIRSpectraData"));
    if (!ond || !ond->GetNearIRData())
      return false;
    std::vector<double> wavenumbers = ond->GetFrequencies();
    std::vector<double> intensities = ond->GetIntensities();
    if (wavenumbers.size() > 0 && intensities.empty())
      intensities.assign(wavenumbers.size(), 1.0);
    double maxIntensity = 0.0;
    for (double v : intensities)
      if (v > maxIntensity) maxIntensity = v;
    std::vector<double> absorbances;
    for (double t : intensities) {
      if (maxIntensity != 0)
        t /= maxIntensity;
      t *= 100.0;
      absorbances.push_back(t);
    }
    xList.clear();
    yList.clear();
    for (size_t i = 0; i < wavenumbers.size(); ++i) {
      xList.append(wavenumbers[i]);
      yList.append(absorbances[i]);
    }
    return true;
  }
};

class NearIRSpectraTest : public QObject
{
  Q_OBJECT
private slots:
  void readData();
};

void NearIRSpectraTest::readData()
{
  OpenBabel::OBMol obmol;
  auto* data = new OBOrcaNearIRData;
  data->SetNearIRData(true);
  std::vector<double> freq{1500.0, 1600.0};
  std::vector<double> inten{1.0, 0.5};
  data->SetFrequencies(freq);
  data->SetIntensities(inten);
  obmol.SetData(data);

  Molecule mol;
  mol.setOBMol(&obmol);

  StubNearIR spec;
  QVERIFY(spec.checkForData(&mol));
  QCOMPARE(spec.xList.size(), 2);
  QCOMPARE(spec.yList.size(), 2);
}

QTEST_MAIN(NearIRSpectraTest)
#include "moc_nearirspectratest.cpp"
