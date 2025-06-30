#include "config.h"
#include <QtTest>
#include <avogadro/molecule.h>
#include <openbabel/mol.h>
#include <openbabel/generic.h>
#include "../src/orca_data_stub.h"
#include "../src/extensions/spectra/spectradialog.h"
#include "../src/extensions/spectra/nearir.h"

using Avogadro::Molecule;
using Avogadro::SpectraDialog;
using Avogadro::NearIRSpectra;
using OpenBabel::OBOrcaNearIRData;

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

  SpectraDialog dialog;
  NearIRSpectra spec(&dialog);
  QVERIFY(spec.checkForData(&mol));
}

QTEST_MAIN(NearIRSpectraTest)
#include "moc_nearirspectratest.cpp"
