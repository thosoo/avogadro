#include "config.h"
#include <QtTest>
#include <QDir>
#include <avogadro/pluginmanager.h>
#include <avogadro/engine.h>
#include <avogadro/atom.h>
#include "../src/engines/hairengine.h"
#include <avogadro/molecule.h>
#include <Eigen/Geometry>

using namespace Avogadro;

class HairEngineTest : public QObject
{
  Q_OBJECT
private slots:
  void pluginLoaded();
};

void HairEngineTest::pluginLoaded()
{
  QStringList pluginDirs = PluginManager::instance()->pluginPath();
  bool pluginExists = false;
  foreach (const QString &dir, pluginDirs) {
    QDir d(dir);
    QStringList files = d.entryList(QStringList() << "*hairengine*", QDir::Files);
    if (!files.isEmpty()) {
      pluginExists = true;
      break;
    }
  }
  if (!pluginExists)
    QSKIP("hairengine plugin not built");

  Engine *engine = PluginManager::instance()->engine("Hair", this);
  QVERIFY(engine != nullptr);

  Molecule mol;
  mol.addAtom(6, Eigen::Vector3d(0, 0, 0));
  mol.addAtom(1, Eigen::Vector3d(1, 0, 0));
  engine->setMolecule(&mol);

  HairEngine *hair = qobject_cast<HairEngine *>(engine);
  QVERIFY(hair != nullptr);
  for (unsigned int i = 0; i < mol.numAtoms(); ++i)
    QCOMPARE(hair->hairCount(mol.atom(i)->id()), 8);

  delete hair;
}

QTEST_MAIN(HairEngineTest)
#include "moc_hairenginetest.cpp"
