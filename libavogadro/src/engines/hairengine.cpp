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
#include <avogadro/color.h>
#include <avogadro/color3f.h>

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
  m_settings.model = VectorEMA;
  m_settings.alpha = 0.2;
  m_settings.lagFactor = 1.0;
  m_settings.naturalFreq = 15.0;
  m_settings.nodesPerStrand = 5;
  m_settings.stiffness = 0.7;
  m_settings.drag = 0.03;
  m_settings.constraintIters = 3;
  m_timer.start();
  m_frameTimer.start();
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
  engine->m_settings = m_settings;
  engine->m_prevModelView = m_prevModelView;
  engine->m_hasPrevModelView = m_hasPrevModelView;
  return engine;
}

bool HairEngine::renderOpaque(PainterDevice *pd)
{
  double dt = m_frameTimer.restart() / 1000.0;
  if (dt <= 1e-6)
    dt = 1.0 / 60.0;

  Eigen::Projective3d curModelView = pd->camera()->modelview();
  if (!m_hasPrevModelView) {
    m_prevModelView = curModelView;
    m_hasPrevModelView = true;
  }

  struct Context {
    Vector3d origin;
    double baseRadius;
    Vector3d velocity;
    Vector3d acceleration;
    unsigned int id;
    Color3f color;
    std::vector<SpringState>* springs;
    std::vector<Strand>*  mass;
  };

  QList<Atom *> atomList = atoms();
  std::vector<Context> ctxs;
  ctxs.reserve(atomList.size());

  Color *map = colorMap();
  if (!map)
    map = pd->colorMap();

  for (Atom *atom : atomList) {
    Vector3d origin = *atom->pos();
    double baseRadius = pd->radius(atom);

    Vector3d prev = m_prevPos.value(atom->id(), origin);
    Vector3d vel = (origin - prev) / dt;
    Vector3d prevVel = m_prevVel.value(atom->id(), Vector3d::Zero());
    Vector3d acc = (vel - prevVel) / dt;
    m_prevPos[atom->id()] = origin;
    m_prevVel[atom->id()] = vel;

    Eigen::Vector3d curCam = (pd->camera()->modelview() * origin.homogeneous()).head<3>();
    Eigen::Vector3d prevCam = (m_prevModelView * origin.homogeneous()).head<3>();
    Eigen::Vector3d camDelta = curCam - prevCam;
    camDelta = pd->camera()->modelview().linear().inverse() * camDelta;
    vel += camDelta / dt;

    map->setFromPrimitive(atom);
    Color3f col(map->red(), map->green(), map->blue());

    std::vector<SpringState>* sp = nullptr;
    std::vector<Strand>* ms = nullptr;
    if (m_settings.model == SpringLag) {
      sp = &m_springStrands[atom->id()];
      if (sp->size() != static_cast<size_t>(m_count)) {
        sp->clear();
        sp->resize(m_count);
        for (int j = 0; j < m_count; ++j) {
          QRandomGenerator gen(static_cast<quint32>(atom->id() * 100 + j));
          double theta = gen.generateDouble() * M_PI * 2.0;
          double phi = std::acos(2.0 * gen.generateDouble() - 1.0);
          Vector3d d(std::sin(phi) * std::cos(theta),
                     std::sin(phi) * std::sin(theta),
                     std::cos(phi));
          (*sp)[j].baseDir = d;
          (*sp)[j].dir.setZero();
          (*sp)[j].omega.setZero();
        }
      }
    } else if (m_settings.model == MassSpring) {
      ms = &m_massStrands[atom->id()];
      size_t nodeCount = static_cast<size_t>(m_settings.nodesPerStrand - 1);
      if (ms->size() != static_cast<size_t>(m_count) ||
          (ms->size() && ms->front().nodes.size() != nodeCount)) {
        ms->clear();
        ms->resize(m_count);
        double rest = m_length / (m_settings.nodesPerStrand - 1);
        for (int j = 0; j < m_count; ++j) {
          QRandomGenerator gen(static_cast<quint32>(atom->id() * 100 + j));
          double theta = gen.generateDouble() * M_PI * 2.0;
          double phi = std::acos(2.0 * gen.generateDouble() - 1.0);
          Vector3d d(std::sin(phi) * std::cos(theta),
                     std::sin(phi) * std::sin(theta),
                     std::cos(phi));
          (*ms)[j].baseDir = d;
          (*ms)[j].nodes.resize(nodeCount);
          Vector3d pos = origin + d * baseRadius;
          for (size_t n = 0; n < nodeCount; ++n) {
            pos += d * rest;
            (*ms)[j].nodes[n].pos = pos;
            (*ms)[j].nodes[n].prev = pos;
          }
        }
      }
    }

    ctxs.push_back({origin, baseRadius, vel, acc, atom->id(), col, sp, ms});
  }

  struct Segment { Vector3d a; Vector3d b; Color3f color; };
  std::vector<Segment> segs;

  double time = m_timer.elapsed() / 500.0;
  const int segments = 5;

  const int ctxSize = static_cast<int>(ctxs.size());
#pragma omp parallel
  {
    std::vector<Segment> local;
    local.reserve(ctxs.size() * m_count);

#pragma omp for nowait schedule(static)
    for (int i = 0; i < ctxSize; ++i) {
      Context &c = ctxs[i];
      for (int j = 0; j < m_count; ++j) {
        Vector3d dir;
        if (m_settings.model == VectorEMA) {
          QRandomGenerator gen(static_cast<quint32>(c.id * 100 + j));
          double theta = gen.generateDouble() * M_PI * 2.0;
          double phi = std::acos(2.0 * gen.generateDouble() - 1.0);
          dir = Vector3d(std::sin(phi) * std::cos(theta),
                         std::sin(phi) * std::sin(theta),
                         std::cos(phi));
        } else if (m_settings.model == SpringLag) {
          dir = (*(c.springs))[j].baseDir;
        } else {
          dir = (*(c.mass))[j].baseDir;
        }

        Vector3d start = c.origin + dir * c.baseRadius;

        Vector3d curlDir;
        double amp = 0.0;
        if (m_settings.model == VectorEMA) {
          Vector3d prevSmooth = m_smoothVel.value(c.id, Vector3d::Zero());
          Vector3d smooth = prevSmooth * (1.0 - m_settings.alpha) +
                           c.velocity * m_settings.alpha;
          m_smoothVel[c.id] = smooth;
          curlDir = -smooth;
          amp = std::min(smooth.norm() * dt * 20.0, 1.0);
        } else if (m_settings.model == SpringLag) {
          SpringState &st = (*(c.springs))[j];
          Vector3d target = -c.velocity;
          target -= dir * target.dot(dir);
          if (target.norm() < 1e-3)
            target = dir.cross(Vector3d::UnitX());
          if (target.norm() < 1e-3)
            target = dir.cross(Vector3d::UnitY());
          target.normalize();
          double targetAmp = std::min(c.velocity.norm() * dt * 20.0 *
                                      m_settings.lagFactor, 1.0);
          target *= targetAmp;
          Vector3d accel = -2.0 * m_settings.naturalFreq * st.omega -
                           m_settings.naturalFreq * m_settings.naturalFreq *
                           (st.dir - target);
          st.omega += accel * dt;
          st.dir += st.omega * dt;
          curlDir = st.dir;
          amp = curlDir.norm();
        } else {
          Strand &st = (*(c.mass))[j];
          size_t nodeCount = st.nodes.size();
          double rest = m_length / (m_settings.nodesPerStrand - 1);
          Vector3d rootAcc = c.acceleration;
          for (size_t n = 0; n < nodeCount; ++n) {
            Vector3d vel = st.nodes[n].pos - st.nodes[n].prev;
            vel *= (1.0 - m_settings.drag);
            vel += (-rootAcc + Vector3d(0,0,-9.81)) * dt * dt;
            st.nodes[n].prev = st.nodes[n].pos;
            st.nodes[n].pos += vel;
          }
          for (int it = 0; it < m_settings.constraintIters; ++it) {
            Vector3d prevP = start;
            for (size_t n = 0; n < nodeCount; ++n) {
              Vector3d diff = st.nodes[n].pos - prevP;
              double len = diff.norm();
              if (len > 1e-6) {
                Vector3d corr = diff * (1.0 - rest / len) * m_settings.stiffness;
                st.nodes[n].pos -= corr;
              }
              prevP = st.nodes[n].pos;
            }
          }
          Vector3d prevPoint = start;
          for (size_t n = 0; n < nodeCount; ++n) {
            local.push_back({prevPoint, st.nodes[n].pos, c.color});
            prevPoint = st.nodes[n].pos;
          }
          continue;
        }

        curlDir -= dir * curlDir.dot(dir);
        if (curlDir.norm() < 1e-3)
          curlDir = dir.cross(Vector3d::UnitX());
        if (curlDir.norm() < 1e-3)
          curlDir = dir.cross(Vector3d::UnitY());
        curlDir.normalize();

        Vector3d prevPoint = start;
        double phase = j + c.id;
        double baseAmp = m_length * 0.7 * amp;
        double dynAmp = m_length * 0.3;
        for (int s = 1; s <= segments; ++s) {
          double t = static_cast<double>(s) / segments;
          double dyn = std::sin(t * M_PI + phase + time) * dynAmp * t;
          double base = baseAmp * t;
          Vector3d p = start + dir * (m_length * t) + curlDir * (base + dyn);
          local.push_back({prevPoint, p, c.color});
          prevPoint = p;
        }
      }
    }

#pragma omp critical
    segs.insert(segs.end(), local.begin(), local.end());
  }

  for (const Segment &s : segs) {
    pd->painter()->setColor(s.color.red(), s.color.green(), s.color.blue(), 1.0f);
    pd->painter()->drawLine(s.a, s.b, 1.0);
  }

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
    connect(m_settingsWidget->modelComboBox, SIGNAL(currentIndexChanged(int)),
            this, SLOT(setInertiaModel(int)));
    connect(m_settingsWidget->alphaSpinBox, SIGNAL(valueChanged(double)),
            this, SLOT(setAlpha(double)));
    connect(m_settingsWidget->lagSpinBox, SIGNAL(valueChanged(double)),
            this, SLOT(setLagFactor(double)));
    connect(m_settingsWidget->freqSpinBox, SIGNAL(valueChanged(double)),
            this, SLOT(setNaturalFreq(double)));
    connect(m_settingsWidget->nodesSpinBox, SIGNAL(valueChanged(int)),
            this, SLOT(setNodes(int)));
    connect(m_settingsWidget->stiffnessSpinBox, SIGNAL(valueChanged(double)),
            this, SLOT(setStiffness(double)));
    connect(m_settingsWidget->dragSpinBox, SIGNAL(valueChanged(double)),
            this, SLOT(setDrag(double)));
    connect(m_settingsWidget->iterSpinBox, SIGNAL(valueChanged(int)),
            this, SLOT(setConstraintIters(int)));
    connect(m_settingsWidget, SIGNAL(destroyed()),
            this, SLOT(settingsWidgetDestroyed()));
    m_settingsWidget->lengthSpinBox->setValue(m_length);
    m_settingsWidget->countSpinBox->setValue(m_count);
    m_settingsWidget->modelComboBox->setCurrentIndex(m_settings.model);
    m_settingsWidget->alphaSpinBox->setValue(m_settings.alpha);
    m_settingsWidget->lagSpinBox->setValue(m_settings.lagFactor);
    m_settingsWidget->freqSpinBox->setValue(m_settings.naturalFreq);
    m_settingsWidget->nodesSpinBox->setValue(m_settings.nodesPerStrand);
    m_settingsWidget->stiffnessSpinBox->setValue(m_settings.stiffness);
    m_settingsWidget->dragSpinBox->setValue(m_settings.drag);
    m_settingsWidget->iterSpinBox->setValue(m_settings.constraintIters);
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
  clearState();
  emit changed();
}

void HairEngine::setInertiaModel(int value)
{
  m_settings.model = static_cast<InertiaModel>(value);
  clearState();
  emit changed();
}

void HairEngine::setAlpha(double value)
{
  m_settings.alpha = value;
  emit changed();
}

void HairEngine::setLagFactor(double value)
{
  m_settings.lagFactor = value;
  emit changed();
}

void HairEngine::setNaturalFreq(double value)
{
  m_settings.naturalFreq = value;
  emit changed();
}

void HairEngine::setNodes(int value)
{
  m_settings.nodesPerStrand = value;
  clearState();
  emit changed();
}

void HairEngine::setStiffness(double value)
{
  m_settings.stiffness = value;
  emit changed();
}

void HairEngine::setDrag(double value)
{
  m_settings.drag = value;
  emit changed();
}

void HairEngine::setConstraintIters(int value)
{
  m_settings.constraintIters = value;
  emit changed();
}

void HairEngine::settingsWidgetDestroyed()
{
  m_settingsWidget = 0;
}

void HairEngine::clearState()
{
  m_prevPos.clear();
  m_prevVel.clear();
  m_smoothVel.clear();
  m_springStrands.clear();
  m_massStrands.clear();
  m_hasPrevModelView = false;
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
  settings.setValue("model", static_cast<int>(m_settings.model));
  settings.setValue("alpha", m_settings.alpha);
  settings.setValue("lagFactor", m_settings.lagFactor);
  settings.setValue("naturalFreq", m_settings.naturalFreq);
  settings.setValue("nodesPerStrand", m_settings.nodesPerStrand);
  settings.setValue("stiffness", m_settings.stiffness);
  settings.setValue("drag", m_settings.drag);
  settings.setValue("constraintIters", m_settings.constraintIters);
}

void HairEngine::readSettings(QSettings &settings)
{
  Engine::readSettings(settings);
  setLength(settings.value("length", 0.5).toDouble());
  setCount(settings.value("count", 10).toInt());
  setInertiaModel(settings.value("model", 0).toInt());
  setAlpha(settings.value("alpha", 0.2).toDouble());
  setLagFactor(settings.value("lagFactor", 1.0).toDouble());
  setNaturalFreq(settings.value("naturalFreq", 15.0).toDouble());
  setNodes(settings.value("nodesPerStrand", 5).toInt());
  setStiffness(settings.value("stiffness", 0.7).toDouble());
  setDrag(settings.value("drag", 0.03).toDouble());
  setConstraintIters(settings.value("constraintIters", 3).toInt());
  if (m_settingsWidget)
  {
    m_settingsWidget->lengthSpinBox->setValue(m_length);
    m_settingsWidget->countSpinBox->setValue(m_count);
    m_settingsWidget->modelComboBox->setCurrentIndex(m_settings.model);
    m_settingsWidget->alphaSpinBox->setValue(m_settings.alpha);
    m_settingsWidget->lagSpinBox->setValue(m_settings.lagFactor);
    m_settingsWidget->freqSpinBox->setValue(m_settings.naturalFreq);
    m_settingsWidget->nodesSpinBox->setValue(m_settings.nodesPerStrand);
    m_settingsWidget->stiffnessSpinBox->setValue(m_settings.stiffness);
    m_settingsWidget->dragSpinBox->setValue(m_settings.drag);
    m_settingsWidget->iterSpinBox->setValue(m_settings.constraintIters);
  }
}

} // End namespace Avogadro

