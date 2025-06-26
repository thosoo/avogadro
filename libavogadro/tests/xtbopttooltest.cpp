#include "config.h"
#include <QtTest>

#include <avogadro/pluginmanager.h>
#include <avogadro/tool.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <Eigen/Core>
#include <xtb.h>
#include <QDir>

using Avogadro::PluginManager;
using Avogadro::Tool;

class XtbOptToolTest : public QObject
{
  Q_OBJECT
private slots:
  void pluginLoaded();
  void optimizeWater();
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
  Avogadro::Molecule mol;
  Avogadro::Atom *o = mol.addAtom(8, Eigen::Vector3d(0.0, 0.0, 0.0));
  Avogadro::Atom *h1 = mol.addAtom(1, Eigen::Vector3d(2.0, 0.0, 0.0));
  Avogadro::Atom *h2 = mol.addAtom(1, Eigen::Vector3d(-2.0, 0.0, 0.0));
  mol.addBond(o, h1);
  mol.addBond(o, h2);

  double initialDist = (*h1->pos() - *o->pos()).norm();

  xtb_TEnvironment env = xtb_newEnvironment();
  int natoms = mol.numAtoms();
  std::vector<int> nums(natoms);
  std::vector<double> coords(natoms * 3);
  const double ang2bohr = 1.8897259886;
  for (int i = 0; i < natoms; ++i) {
    Avogadro::Atom *a = mol.atom(i);
    nums[i] = a->atomicNumber();
    Eigen::Vector3d p = *a->pos();
    coords[3 * i] = p.x() * ang2bohr;
    coords[3 * i + 1] = p.y() * ang2bohr;
    coords[3 * i + 2] = p.z() * ang2bohr;
  }
  double charge = 0.0;
  int uhf = 0;
  double lattice[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  bool periodic[3] = {false, false, false};
  xtb_TMolecule xmol = xtb_newMolecule(env, &natoms, nums.data(), coords.data(),
                                       &charge, &uhf, &lattice[0][0], periodic);
  xtb_TCalculator calc = xtb_newCalculator();
  xtb_loadGFN2xTB(env, xmol, calc, NULL);
  xtb_TResults res = xtb_newResults();

  for (int s = 0; s < 50; ++s) {
    xtb_updateMolecule(env, xmol, coords.data(), NULL);
    xtb_singlepoint(env, xmol, calc, res);
    std::vector<double> grad(natoms * 3);
    xtb_getGradient(env, res, grad.data());
    double gradNorm = 0.0;
    for (double g : grad)
      gradNorm += g * g;
    gradNorm = std::sqrt(gradNorm);
    double step = 0.05;
    if (gradNorm > 1.0)
      step /= gradNorm;
    for (int i = 0; i < natoms * 3; ++i)
      coords[i] -= step * grad[i];
  }

  xtb_delResults(&res);
  xtb_delCalculator(&calc);
  xtb_delMolecule(&xmol);
  xtb_delEnvironment(&env);

  const double bohr2ang = 0.52917721092;
  double optimizedDist =
      std::sqrt(std::pow(coords[3] * bohr2ang - coords[0] * bohr2ang, 2) +
                std::pow(coords[4] * bohr2ang - coords[1] * bohr2ang, 2) +
                std::pow(coords[5] * bohr2ang - coords[2] * bohr2ang, 2));
  QVERIFY(optimizedDist < initialDist);
  Eigen::Vector3d v1((coords[3] - coords[0]) * bohr2ang,
                     (coords[4] - coords[1]) * bohr2ang,
                     (coords[5] - coords[2]) * bohr2ang);
  Eigen::Vector3d v2((coords[6] - coords[0]) * bohr2ang,
                     (coords[7] - coords[1]) * bohr2ang,
                     (coords[8] - coords[2]) * bohr2ang);
  double angle = std::acos(v1.dot(v2) / (v1.norm() * v2.norm())) * 180.0 / M_PI;
  QVERIFY(angle < 150.0);
}

QTEST_MAIN(XtbOptToolTest)
#include "moc_xtbopttooltest.cpp"
