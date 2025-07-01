#include "config.h"

#include <QtTest>
#include <QtCore/QTemporaryDir>
#include "../src/engines/hairengine.h"

using Avogadro::HairEngine;

class HairEngineTest : public QObject
{
  Q_OBJECT
private slots:
  void saveLoadSettings();
};

void HairEngineTest::saveLoadSettings()
{
  QTemporaryDir dir;
  QString ini = dir.filePath("hair.ini");
  {
    HairEngine engine;
    engine.setLength(7); // 0.7 units
    QSettings settings(ini, QSettings::IniFormat);
    engine.writeSettings(settings);
    settings.sync();
  }
  {
    HairEngine engine;
    QSettings settings(ini, QSettings::IniFormat);
    engine.readSettings(settings);
    QString outFile = dir.filePath("out.ini");
    QSettings out(outFile, QSettings::IniFormat);
    engine.writeSettings(out);
    out.sync();
    QCOMPARE(out.value("length").toInt(), 7);
  }
}

QTEST_MAIN(HairEngineTest)
#include "moc_hairenginetest.cpp"
