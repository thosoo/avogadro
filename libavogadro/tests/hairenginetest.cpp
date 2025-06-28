#include "config.h"
#include <QtTest>
#include <QDir>
#include <avogadro/pluginmanager.h>
#include <avogadro/engine.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include "libavogadro/src/engines/hairengine.h"
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

  QList<Engine *> engines = PluginManager::instance()->engines(this);
  HairEngine *hair = nullptr;
  foreach (Engine *engine, engines) {
    if (engine->objectName() == QLatin1String("Hair")) {
      hair = qobject_cast<HairEngine *>(engine);
    } else {
      delete engine;
    }
  }
  QVERIFY(hair != nullptr);

  Molecule mol;
  Atom *a1 = mol.addAtom(6, Eigen::Vector3d(0, 0, 0));
  Atom *a2 = mol.addAtom(1, Eigen::Vector3d(1, 0, 0));
  hair->setMolecule(&mol);
  QCOMPARE(hair->hairCount(a1->id()), 8);
  QCOMPARE(hair->hairCount(a2->id()), 8);
  delete hair;
}

QTEST_MAIN(HairEngineTest)
#include "moc_hairenginetest.cpp"
