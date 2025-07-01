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

#include "hairengine.h"
#include "ui_hairsettingswidget.h"

#include <avogadro/painterdevice.h>
#include <avogadro/atom.h>
#include <avogadro/molecule.h>

#include <QtCore/QRandomGenerator>
#include <cmath>

using Eigen::Vector3d;

namespace Avogadro {

// Simple widget wrapping the settings UI for the hair engine.
class HairSettingsWidget : public QWidget, public Ui::HairSettingsWidget
{
public:
  explicit HairSettingsWidget(QWidget *parent = 0)
    : QWidget(parent)
  {
    setupUi(this);
  }
};

HairEngine::HairEngine(QObject *parent) : Engine(parent),
  m_settingsWidget(0), m_length(0.5)
{
}

HairEngine::~HairEngine()
{
  if (m_settingsWidget)
    m_settingsWidget->deleteLater();
}

Engine *HairEngine::clone() const
{
  HairEngine *engine = new HairEngine(parent());
  engine->setAlias(alias());
  engine->setEnabled(isEnabled());
  engine->m_length = m_length;
  return engine;
}

bool HairEngine::renderOpaque(PainterDevice *pd)
{
  foreach(Atom *atom, atoms())
    renderOpaque(pd, atom);
  return true;
}

bool HairEngine::renderOpaque(PainterDevice *pd, const Atom *atom)
{
  Vector3d origin = *atom->pos();
  double baseRadius = pd->radius(atom);

  for (int i = 0; i < 10; ++i) {
    QRandomGenerator gen(static_cast<quint32>(atom->id() * 100 + i));
    double theta = gen.generateDouble() * M_PI * 2.0;
    double phi = std::acos(2.0 * gen.generateDouble() - 1.0);
    Vector3d dir(std::sin(phi) * std::cos(theta),
                 std::sin(phi) * std::sin(theta),
                 std::cos(phi));
    Vector3d start = origin + dir * baseRadius;
    Vector3d end = start + dir * m_length;
    pd->painter()->drawLine(start, end, 1.0);
  }

  return true;
}

QWidget *HairEngine::settingsWidget()
{
  if (!m_settingsWidget) {
    m_settingsWidget = new HairSettingsWidget();
    connect(m_settingsWidget->lengthSlider, SIGNAL(valueChanged(int)),
            this, SLOT(setLength(int)));
    connect(m_settingsWidget, SIGNAL(destroyed()),
            this, SLOT(settingsWidgetDestroyed()));
    m_settingsWidget->lengthSlider->setValue(int(m_length * 10));
  }
  return m_settingsWidget;
}

void HairEngine::setLength(int value)
{
  m_length = value / 10.0;
  emit changed();
}

void HairEngine::settingsWidgetDestroyed()
{
  m_settingsWidget = 0;
}

double HairEngine::radius(const PainterDevice *, const Primitive *) const
{
  return 0.0;
}

Engine::Layers HairEngine::layers() const
{
  return Engine::Overlay;
}

void HairEngine::writeSettings(QSettings &settings) const
{
  Engine::writeSettings(settings);
  settings.setValue("length", int(m_length * 10));
}

void HairEngine::readSettings(QSettings &settings)
{
  Engine::readSettings(settings);
  setLength(settings.value("length", 5).toInt());
  if (m_settingsWidget)
    m_settingsWidget->lengthSlider->setValue(int(m_length * 10));
}

} // End namespace Avogadro

