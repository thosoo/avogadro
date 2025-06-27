#include "config.h"
#include <QtTest>
#include <xtb.h>

class XtbPhysicsTest : public QObject
{
  Q_OBJECT
private slots:
  void energy();
};

void XtbPhysicsTest::energy()
{
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

  xtb_TEnvironment env = xtb_newEnvironment();
  xtb_TMolecule mol = xtb_newMolecule(env, &natoms, numbers, coords, &charge, &uhf,
                                      &lattice[0][0], periodic);
  xtb_TCalculator calc = xtb_newCalculator();
  xtb_loadGFN2xTB(env, mol, calc, NULL);
  xtb_TResults res = xtb_newResults();

  xtb_singlepoint(env, mol, calc, res);
  double energy = 0.0;
  xtb_getEnergy(env, res, &energy);

  QVERIFY(energy < 0.0);

  xtb_delResults(&res);
  xtb_delCalculator(&calc);
  xtb_delMolecule(&mol);
  xtb_delEnvironment(&env);
}

QTEST_MAIN(XtbPhysicsTest)
#include "moc_xtbphysicstest.cpp"
