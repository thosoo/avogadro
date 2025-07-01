/**********************************************************************
  HairEngine - Display hair/fur on atoms

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


// Forward declare the generated UI form to avoid requiring the header in
// consumers of this engine.
namespace Ui {
class HairSettingsWidget;
}

namespace Avogadro {

class HairSettingsWidget;
class Atom;
class HairEngine : public Engine
{
  Q_OBJECT
  AVOGADRO_ENGINE("Hair", tr("Hair"), tr("Renders hair on atoms"))

public:
  HairEngine(QObject *parent = 0);
  ~HairEngine();

  Engine *clone() const;

  bool renderOpaque(PainterDevice *pd);
  bool renderOpaque(PainterDevice *pd, const Atom *a);

  QWidget *settingsWidget();
  bool hasSettings() { return true; }

  double radius(const PainterDevice *, const Primitive *p = 0) const;
  Engine::Layers layers() const;

  void writeSettings(QSettings &settings) const;
  void readSettings(QSettings &settings);

public slots:
  /// Set the hair length in tenths of Angstroms.
  void setLength(int value);
  /// Set the number of hairs drawn per atom.
  void setCount(int value);

private slots:
  void settingsWidgetDestroyed();

private:
  HairSettingsWidget *m_settingsWidget;
  double m_length;
  int m_count;
};


class HairEngineFactory : public QObject, public PluginFactory
{
  Q_OBJECT
  Q_INTERFACES(Avogadro::PluginFactory)
  Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
  AVOGADRO_ENGINE_FACTORY(HairEngine)
};

} // End namespace Avogadro

#endif
