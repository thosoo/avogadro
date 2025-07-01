
/**********************************************************************
  HairEngine - Engine to render animated hair on atoms

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
#include "hairengine.h"

#include <avogadro/atom.h>
#include <avogadro/molecule.h>
#include <avogadro/painterdevice.h>
#include <avogadro/color.h>
#include <openbabel/elements.h>
#include "ui_hairsettingswidget.h"
#include <Eigen/Geometry>

#include <QRandomGenerator>
#include <QtMath>
#include <QSettings>

namespace Avogadro {

HairSettingsWidget::HairSettingsWidget(QWidget *parent)
  : QWidget(parent), ui(new Ui::HairSettingsWidget)
{
  ui->setupUi(this);
}

HairSettingsWidget::~HairSettingsWidget()
{
  delete ui;
}

HairEngine::HairEngine(QObject *parent)
  : Engine(parent), m_lengthFactor(1.5), m_settingsWidget(0)
{
  m_timer.start();
}

Engine *HairEngine::clone() const
{
  HairEngine *engine = new HairEngine(parent());
  engine->setAlias(alias());
  engine->setEnabled(isEnabled());
  return engine;
}

HairEngine::~HairEngine()
{
  if (m_settingsWidget)
    m_settingsWidget->deleteLater();
}

void HairEngine::setMolecule(Molecule *mol)
{
  Engine::setMolecule(mol);
  m_hair.clear();
  if (!mol)
    return;
  foreach(const Atom *a, mol->atoms()) {
    QVector<HairStrand> hairs;
    for (int i = 0; i < 8; ++i) {
      HairStrand h;
      h.dir = Eigen::Vector3d::Random().normalized();
      h.phase = QRandomGenerator::global()->generateDouble() * 2.0 * M_PI;
      hairs.append(h);
    }
    m_hair.insert(a->id(), hairs);
  }
  emit changed();
}

bool HairEngine::renderOpaque(PainterDevice *pd)
{
  if (!pd->molecule())
    return false;

  glDisable(GL_LIGHTING);
  glLineWidth(2.0);

  double t = m_timer.elapsed() / 1000.0;
  Color *map = colorMap();
  if (!map)
    map = pd->colorMap();

  foreach (const Atom *a, pd->molecule()->atoms()) {
    QVector<HairStrand> hairs = m_hair.value(a->id());
    map->setFromPrimitive(a);
    pd->painter()->setColor(map);
    double radius = pd->radius(a);
    if (radius <= 0.0)
      radius = OpenBabel::OBElements::GetVdwRad(a->atomicNumber());
    double hairLength = m_lengthFactor * radius;

    foreach (const HairStrand &h, hairs) {
      // Offset slightly outside the sphere so lines aren't hidden
      Eigen::Vector3d base = *a->pos() + h.dir.normalized() * (radius * 1.05);
      Eigen::Vector3d swing = h.dir.cross(Eigen::Vector3d::UnitZ()).normalized();
      Eigen::Vector3d dir = h.dir + swing * 0.2 * qSin(t + h.phase);
      Eigen::Vector3d tip = base + dir.normalized() * hairLength;
      pd->painter()->drawLine(base, tip, 1.0);
    }
  }
  glLineWidth(1.0);
  glEnable(GL_LIGHTING);
  return true;
}

int HairEngine::hairCount(unsigned int atomId) const
{
  return m_hair.value(atomId).size();
}

QWidget *HairEngine::settingsWidget()
{
  if (!m_settingsWidget) {
    m_settingsWidget = new HairSettingsWidget();
    connect(m_settingsWidget->ui->lengthSlider, SIGNAL(valueChanged(int)),
            this, SLOT(setLength(int)));
    connect(m_settingsWidget, SIGNAL(destroyed()),
            this, SLOT(settingsWidgetDestroyed()));
    m_settingsWidget->ui->lengthSlider->setValue(static_cast<int>(m_lengthFactor));
  }
  return m_settingsWidget;
}

void HairEngine::settingsWidgetDestroyed()
{
  m_settingsWidget = 0;
}

void HairEngine::setLength(int value)
{
  m_lengthFactor = static_cast<double>(value);
  if (m_settingsWidget)
    m_settingsWidget->ui->lengthSlider->setValue(value);
  emit changed();
}

void HairEngine::writeSettings(QSettings &settings) const
{
  Engine::writeSettings(settings);
  settings.setValue("length", static_cast<int>(m_lengthFactor));
}

void HairEngine::readSettings(QSettings &settings)
{
  Engine::readSettings(settings);
  int length = settings.value("length", 2).toInt();
  setLength(length);
}

} // namespace Avogadro

