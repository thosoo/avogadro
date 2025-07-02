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
#include <avogadro/camera.h>
#include <avogadro/atom.h>
#include <avogadro/molecule.h>

#include <QtCore/QRandomGenerator>
#include <QtCore/QElapsedTimer>
#include <Eigen/Geometry>
#ifdef _OPENMP
#  include <omp.h>
#endif
#include <vector>
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
  m_settingsWidget(0), m_length(0.5), m_count(10), m_hasPrevModelView(false)
{
  m_timer.start();
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
  engine->m_count = m_count;
  engine->m_prevModelView = m_prevModelView;
  engine->m_hasPrevModelView = m_hasPrevModelView;
  return engine;
}

bool HairEngine::renderOpaque(PainterDevice *pd)
{
  Eigen::Projective3d curModelView = pd->camera()->modelview();
  if (!m_hasPrevModelView) {
    m_prevModelView = curModelView;
    m_hasPrevModelView = true;
  }

  struct Context {
    Vector3d origin;
    double baseRadius;
    Vector3d movement;
    unsigned int id;
  };

  QList<Atom *> atomList = atoms();
  std::vector<Context> ctxs;
  ctxs.reserve(atomList.size());

  for (Atom *atom : atomList) {
    Vector3d origin = *atom->pos();
    double baseRadius = pd->radius(atom);

    Vector3d prev = m_prevPos.value(atom->id(), origin);
    Vector3d movement = origin - prev;
    m_prevPos[atom->id()] = origin;

    Eigen::Vector3d curCam = (pd->camera()->modelview() * origin.homogeneous()).head<3>();
    Eigen::Vector3d prevCam = (m_prevModelView * origin.homogeneous()).head<3>();
    Eigen::Vector3d camDelta = curCam - prevCam;
    camDelta = pd->camera()->modelview().linear().inverse() * camDelta;
    movement += camDelta;

    ctxs.push_back({origin, baseRadius, movement, atom->id()});
  }

  struct Segment { Vector3d a; Vector3d b; };
  std::vector<Segment> segs;

  double time = m_timer.elapsed() / 500.0;
  const int segments = 5;

#pragma omp parallel
  {
    std::vector<Segment> local;
    local.reserve(ctxs.size() * m_count);

#pragma omp for nowait schedule(static)
    for (size_t i = 0; i < ctxs.size(); ++i) {
      const Context &c = ctxs[i];
      for (int j = 0; j < m_count; ++j) {
        QRandomGenerator gen(static_cast<quint32>(c.id * 100 + j));
        double theta = gen.generateDouble() * M_PI * 2.0;
        double phi = std::acos(2.0 * gen.generateDouble() - 1.0);
        Vector3d dir(std::sin(phi) * std::cos(theta),
                     std::sin(phi) * std::sin(theta),
                     std::cos(phi));

        Vector3d start = c.origin + dir * c.baseRadius;

        Vector3d curlDir = -c.movement;
        curlDir -= dir * curlDir.dot(dir);
        if (curlDir.norm() < 1e-3)
          curlDir = dir.cross(Vector3d::UnitX());
        if (curlDir.norm() < 1e-3)
          curlDir = dir.cross(Vector3d::UnitY());
        curlDir.normalize();

        Vector3d prevPoint = start;
        double phase = j + c.id;
        double baseAmp = m_length * 0.7 * std::min(c.movement.norm() * 20.0, 1.0);
        double dynAmp = m_length * 0.3;
        for (int s = 1; s <= segments; ++s) {
          double t = static_cast<double>(s) / segments;
          double dyn = std::sin(t * M_PI + phase + time) * dynAmp * t;
          double base = baseAmp * t;
          Vector3d p = start + dir * (m_length * t) + curlDir * (base + dyn);
          local.push_back({prevPoint, p});
          prevPoint = p;
        }
      }
    }

#pragma omp critical
    segs.insert(segs.end(), local.begin(), local.end());
  }

  for (const Segment &s : segs)
    pd->painter()->drawLine(s.a, s.b, 1.0);

  m_prevModelView = curModelView;
  return true;
}



QWidget *HairEngine::settingsWidget()
{
  if (!m_settingsWidget) {
    m_settingsWidget = new HairSettingsWidget();
    connect(m_settingsWidget->lengthSpinBox, SIGNAL(valueChanged(double)),
            this, SLOT(setLength(double)));
    connect(m_settingsWidget->countSpinBox, SIGNAL(valueChanged(int)),
            this, SLOT(setCount(int)));
    connect(m_settingsWidget, SIGNAL(destroyed()),
            this, SLOT(settingsWidgetDestroyed()));
    m_settingsWidget->lengthSpinBox->setValue(m_length);
    m_settingsWidget->countSpinBox->setValue(m_count);
  }
  return m_settingsWidget;
}

void HairEngine::setLength(double value)
{
  m_length = value;
  emit changed();
}

void HairEngine::setCount(int value)
{
  m_count = value;
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
  settings.setValue("length", m_length);
  settings.setValue("count", m_count);
}

void HairEngine::readSettings(QSettings &settings)
{
  Engine::readSettings(settings);
  setLength(settings.value("length", 0.5).toDouble());
  setCount(settings.value("count", 10).toInt());
  if (m_settingsWidget)
  {
    m_settingsWidget->lengthSpinBox->setValue(m_length);
    m_settingsWidget->countSpinBox->setValue(m_count);
  }
}

} // End namespace Avogadro

