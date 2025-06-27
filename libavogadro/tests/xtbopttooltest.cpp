#include "config.h"
#include <QtTest>

#include <avogadro/pluginmanager.h>
#include <avogadro/tool.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <Eigen/Core>
#include <xtb.h>
#include <cmath>
#include <vector>
#include <QDir>

using Avogadro::PluginManager;
using Avogadro::Tool;

class XtbOptToolTest : public QObject
{
  Q_OBJECT
private slots:
  void pluginLoaded();
  void optimizeWater();
  void convergence();
};

void XtbOptToolTest::pluginLoaded()
{
  QStringList pluginDirs = PluginManager::instance()->pluginPath();
  bool pluginExists = false;
  foreach (const QString &dir, pluginDirs) {
    QDir d(dir);
    QStringList files = d.entryList(QStringList() << "*xtbopttool*", QDir::Files);
    if (!files.isEmpty()) {
      pluginExists = true;
      break;
    }
  }
  if (!pluginExists)
    QSKIP("xtbopttool plugin not built");

  QList<Tool *> tools = PluginManager::instance()->tools(this);
  bool found = false;
  foreach (Tool *tool, tools) {
    if (tool->objectName() == QLatin1String("XtbOptimization")) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void XtbOptToolTest::optimizeWater()
{
  const double ang2bohr = 1.8897259886;
  const double bohr2ang = 0.52917721092;

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

  for (int s = 0; s < 200; ++s) {
    xtb_updateMolecule(env, mol, coords, NULL);
    xtb_singlepoint(env, mol, calc, res);
    std::vector<double> grad(natoms * 3);
    xtb_getGradient(env, res, grad.data());
    double gradNorm = 0.0;
    for (double g : grad)
      gradNorm += g * g;
    gradNorm = std::sqrt(gradNorm);
    double step = 0.1;
    if (gradNorm > 1.0)
      step /= gradNorm;
    for (int i = 0; i < natoms * 3; ++i)
      coords[i] -= step * grad[i];
    if (gradNorm < 1e-3)
      break;
  }

  Eigen::Vector3d o(coords[0] * bohr2ang, coords[1] * bohr2ang, coords[2] * bohr2ang);
  Eigen::Vector3d h1(coords[3] * bohr2ang, coords[4] * bohr2ang, coords[5] * bohr2ang);
  Eigen::Vector3d h2(coords[6] * bohr2ang, coords[7] * bohr2ang, coords[8] * bohr2ang);
  double d1 = (h1 - o).norm();
  double d2 = (h2 - o).norm();
  double angle = std::acos(((h1 - o).normalized().dot((h2 - o).normalized()))) * 180.0 / M_PI;

  QVERIFY(d1 > 0.9 && d1 < 1.1);
  QVERIFY(d2 > 0.9 && d2 < 1.1);
  QVERIFY(angle > 100.0 && angle < 115.0);

  xtb_delResults(&res);
  xtb_delCalculator(&calc);
  xtb_delMolecule(&mol);
  xtb_delEnvironment(&env);
}

void XtbOptToolTest::convergence()
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

  double gradNorm = 0.0;
  for (int s = 0; s < 200; ++s) {
    xtb_updateMolecule(env, mol, coords, NULL);
    xtb_singlepoint(env, mol, calc, res);
    std::vector<double> grad(natoms * 3);
    xtb_getGradient(env, res, grad.data());
    gradNorm = 0.0;
    for (double g : grad)
      gradNorm += g * g;
    gradNorm = std::sqrt(gradNorm);
    double step = 0.1;
    if (gradNorm > 1.0)
      step /= gradNorm;
    for (int i = 0; i < natoms * 3; ++i)
      coords[i] -= step * grad[i];
    if (gradNorm < 5e-2)
      break;
  }

  QVERIFY(gradNorm < 5e-2);

  xtb_delResults(&res);
  xtb_delCalculator(&calc);
  xtb_delMolecule(&mol);
  xtb_delEnvironment(&env);
}

QTEST_MAIN(XtbOptToolTest)
#include "moc_xtbopttooltest.cpp"
