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
  : Engine(parent), m_hairLength(1.0)
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

  foreach(const Atom *a, pd->molecule()->atoms()) {
    QVector<HairStrand> hairs = m_hair.value(a->id());
    Eigen::Vector3d base = *a->pos();
    map->setFromPrimitive(a);
    pd->painter()->setColor(map);

    foreach(const HairStrand &h, hairs) {
      Eigen::Vector3d swing = h.dir.cross(Eigen::Vector3d::UnitZ()).normalized();
      Eigen::Vector3d dir = h.dir + swing * 0.2 * qSin(t + h.phase);
      Eigen::Vector3d tip = base + dir.normalized() * m_hairLength;
      pd->painter()->drawLine(base, tip, 1.0);
    }
  }
  return true;
}

} // namespace Avogadro

