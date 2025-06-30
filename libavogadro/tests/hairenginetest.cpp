#include "config.h"
#include <QtTest>
#include <QDir>
#include <avogadro/pluginmanager.h>
#include <avogadro/engine.h>
#include <avogadro/atom.h>
#include <QMetaObject>
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

  for (unsigned int i = 0; i < mol.numAtoms(); ++i) {
    int count = 0;
    bool ok = QMetaObject::invokeMethod(engine, "hairCount",
                                        Qt::DirectConnection,
                                        Q_RETURN_ARG(int, count),
                                        Q_ARG(unsigned int, mol.atom(i)->id()));
    QVERIFY(ok);
    QCOMPARE(count, 8);
  }

  delete engine;
}

QTEST_MAIN(HairEngineTest)
#include "moc_hairenginetest.cpp"
