#include "config.h"
#include <QtTest>

#include "xtb/xtbapi.h"

using Avogadro::XtbApi;

class XtbPhysicsTest : public QObject
{
  Q_OBJECT
private slots:
  void energy();
};

void XtbPhysicsTest::energy()
{
  if (!XtbApi::isAvailable()) {
    QSKIP("xTB runtime library not available");
  }

  const double ang2bohr = 1.8897259886;
  int natoms = 3;
  int numbers[3] = {8, 1, 1};
  double coords[9] = {0.0, 0.0, 0.0,
                       0.0, 0.0, 1.0 * ang2bohr,
                       0.0, 0.1 * ang2bohr, -1.0 * ang2bohr};
  double charge = 0.0;
  int uhf = 0;
  double lattice[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
  bool periodic[3] = {false,false,false};

  xtb_TEnvironment env = XtbApi::xtb_newEnvironment()();
  xtb_TMolecule mol = XtbApi::xtb_newMolecule()(
    env, &natoms, numbers, coords, &charge, &uhf, &lattice[0][0], periodic);
  xtb_TCalculator calc = XtbApi::xtb_newCalculator()();
  XtbApi::xtb_loadGFN2xTB()(env, mol, calc, NULL);
  xtb_TResults res = XtbApi::xtb_newResults()();

  XtbApi::xtb_singlepoint()(env, mol, calc, res);
  double energy = 0.0;
  XtbApi::xtb_getEnergy()(env, res, &energy);

  QVERIFY(energy < 0.0);

  XtbApi::xtb_delResults()(&res);
  XtbApi::xtb_delCalculator()(&calc);
  XtbApi::xtb_delMolecule()(&mol);
  XtbApi::xtb_delEnvironment()(&env);
}

QTEST_MAIN(XtbPhysicsTest)
#include "xtbphysicstest.moc"
