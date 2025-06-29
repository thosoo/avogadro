#include "config.h"
#include <QtTest>
#include <avogadro/molecule.h>
#include <avogadro/extensions/spectra/nearir.h>
#include <openbabel/mol.h>
#include <openbabel/generic.h>

using Avogadro::Molecule;
using Avogadro::NearIRSpectra;

class TestNearIRSpectra : public NearIRSpectra
{
public:
  TestNearIRSpectra() : NearIRSpectra(nullptr) {}
  const QList<double>& xs() const { return m_xList; }
  const QList<double>& ys() const { return m_yList; }
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
  auto* data = new OpenBabel::OBOrcaNearIRData;
  data->SetNearIRData(true);
  std::vector<double> freq{1500.0, 1600.0};
  std::vector<double> inten{1.0, 0.5};
  data->SetFrequencies(freq);
  data->SetIntensities(inten);
  obmol.SetData(data);

  Molecule mol;
  mol.setOBMol(&obmol);

  TestNearIRSpectra spec;
  QVERIFY(spec.checkForData(&mol));
  QCOMPARE(spec.xs().size(), 2);
  QCOMPARE(spec.ys().size(), 2);
}

QTEST_MAIN(NearIRSpectraTest)
#include "moc_nearirspectratest.cpp"
