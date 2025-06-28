
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
#include <Eigen/Geometry>

#include <QRandomGenerator>
#include <QtMath>

namespace Avogadro {

HairEngine::HairEngine(QObject *parent)
  : Engine(parent), m_lengthFactor(1.5)
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
}

bool HairEngine::renderOpaque(PainterDevice *pd)
{
  if (!pd->molecule())
    return false;

  double t = m_timer.elapsed() / 1000.0;
  Color *map = colorMap();
  if (!map)
    map = pd->colorMap();

  foreach (const Atom *a, pd->molecule()->atoms()) {
    QVector<HairStrand> hairs = m_hair.value(a->id());
    map->setFromPrimitive(a);
    pd->painter()->setColor(map);
    double radius = pd->radius(a);
    double hairLength = m_lengthFactor * radius;

    foreach (const HairStrand &h, hairs) {
      Eigen::Vector3d base = *a->pos() + h.dir.normalized() * radius;
      Eigen::Vector3d swing = h.dir.cross(Eigen::Vector3d::UnitZ()).normalized();
      Eigen::Vector3d dir = h.dir + swing * 0.2 * qSin(t + h.phase);
      Eigen::Vector3d tip = base + dir.normalized() * hairLength;
      pd->painter()->drawLine(base, tip, 1.0);
    }
  }
  return true;
}

int HairEngine::hairCount(unsigned int atomId) const
{
  return m_hair.value(atomId).size();
}

} // namespace Avogadro

