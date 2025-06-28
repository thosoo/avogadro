#include "config.h"
#include <QtTest>
#include <QDir>
#include <avogadro/pluginmanager.h>
#include <avogadro/engine.h>

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
  bool found = false;
  foreach (Engine *engine, engines) {
    if (engine->objectName() == QLatin1String("Hair")) {
      found = true;
    }
    delete engine;
  }
  QVERIFY(found);
}

QTEST_MAIN(HairEngineTest)
#include "moc_hairenginetest.cpp"
