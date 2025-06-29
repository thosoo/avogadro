
/**********************************************************************
  HairEngine - Engine for rendering hair on atoms

  Copyright (C) 2025 OpenAI

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
**********************************************************************/
#ifndef HAIRENGINE_H
#define HAIRENGINE_H

#include <avogadro/global.h>
#include <avogadro/engine.h>
#include <Eigen/Core>
#include <QElapsedTimer>
#include <QMap>
#include <QVector>
#include <QSettings>
#include <QWidget>
#include <QObject>

#include "ui_hairsettingswidget.h"

class HairSettingsWidget;

namespace Avogadro {

class Atom;
class Molecule;

class HairEngine : public Engine
{
  Q_OBJECT
  AVOGADRO_ENGINE("Hair", tr("Hair"), tr("Render animated hair on atoms"))

public:
  explicit HairEngine(QObject *parent = 0);
  ~HairEngine();

  Engine *clone() const;

  bool renderOpaque(PainterDevice *pd);
  void setMolecule(Molecule *molecule);
  int hairCount(unsigned int atomId) const;

  QWidget *settingsWidget();
  bool hasSettings() { return true; }
  void writeSettings(QSettings &settings) const;
  void readSettings(QSettings &settings);

  void setLength(int value);

private Q_SLOTS:
  void settingsWidgetDestroyed();

private:
  struct HairStrand {
    Eigen::Vector3d dir;
    double phase;
  };

  QMap<unsigned int, QVector<HairStrand> > m_hair;
  QElapsedTimer m_timer;
  double m_lengthFactor;
  HairSettingsWidget *m_settingsWidget;
};

class HairSettingsWidget : public QWidget, public Ui::HairSettingsWidget
{
public:
  HairSettingsWidget(QWidget *parent = 0) : QWidget(parent)
  {
    setupUi(this);
  }
};

class HairEngineFactory : public QObject, public PluginFactory
{
  Q_OBJECT
  Q_INTERFACES(Avogadro::PluginFactory)
  Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
  AVOGADRO_ENGINE_FACTORY(HairEngine)
};

} // namespace Avogadro

#endif
