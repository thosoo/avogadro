/**********************************************************************
  ThermalEllipsoidEngine - Engine for ADP / thermal ellipsoid display
 **********************************************************************/

#include "thermalellipsoidengine.h"

#include <avogadro/atom.h>
#include <avogadro/color.h>
#include <avogadro/painterdevice.h>
#include <avogadro/primitive.h>

#include <QSettings>

#include <cmath>

namespace Avogadro {

  ThermalEllipsoidEngine::ThermalEllipsoidEngine(QObject *parent)
    : Engine(parent), m_settingsWidget(0), m_probability(ThermalEllipsoidGeometry::Probability50), m_scale(1.0)
  {
  }

  ThermalEllipsoidEngine::~ThermalEllipsoidEngine()
  {
    if (m_settingsWidget)
      m_settingsWidget->deleteLater();
  }

  Engine* ThermalEllipsoidEngine::clone() const
  {
    ThermalEllipsoidEngine* engine = new ThermalEllipsoidEngine(parent());
    engine->setAlias(alias());
    engine->m_probability = m_probability;
    engine->m_scale = m_scale;
    engine->setEnabled(isEnabled());
    return engine;
  }

  bool ThermalEllipsoidEngine::renderOpaque(PainterDevice *pd)
  {
    QList<Atom *> allAtoms = atoms() + atomImages();
    foreach(Atom *a, allAtoms)
      render(pd, a);
    return true;
  }

  bool ThermalEllipsoidEngine::renderQuick(PainterDevice *pd)
  {
    foreach(Atom *a, atoms())
      render(pd, a);
    return true;
  }

  bool ThermalEllipsoidEngine::render(PainterDevice *pd, const Atom *a)
  {
    Eigen::Matrix3d axes;
    Eigen::Vector3d radii;
    if (!ThermalEllipsoidGeometry::ellipsoidForAtom(a, m_probability, m_scale, axes, radii))
      return false;

    Color *map = colorMap();
    if (!map)
      map = pd->colorMap();

    map->setFromPrimitive(a);
    pd->painter()->setColor(map);
    pd->painter()->setName(a);
    pd->painter()->drawEllipsoid(*a->pos(), axes, radii);
    return true;
  }

  Engine::Layers ThermalEllipsoidEngine::layers() const
  {
    return Engine::Opaque;
  }

  Engine::PrimitiveTypes ThermalEllipsoidEngine::primitiveTypes() const
  {
    return Engine::Atoms;
  }

  double ThermalEllipsoidEngine::radius(const PainterDevice *, const Primitive *p) const
  {
    if (!p || p->type() != Primitive::AtomType || !primitives().contains(p))
      return 0.;

    Eigen::Matrix3d axes;
    Eigen::Vector3d radii;
    if (!ThermalEllipsoidGeometry::ellipsoidForAtom(static_cast<const Atom *>(p), m_probability, m_scale, axes, radii))
      return 0.;
    return radii.maxCoeff();
  }

  QWidget* ThermalEllipsoidEngine::settingsWidget()
  {
    if (!m_settingsWidget) {
      m_settingsWidget = new ThermalEllipsoidSettingsWidget();
      connect(m_settingsWidget->probabilityCombo, SIGNAL(currentIndexChanged(int)),
              this, SLOT(setProbability(int)));
      connect(m_settingsWidget->scaleSpin, SIGNAL(valueChanged(double)),
              this, SLOT(setScale(double)));
      connect(m_settingsWidget, SIGNAL(destroyed()), this, SLOT(settingsWidgetDestroyed()));
      m_settingsWidget->probabilityCombo->setCurrentIndex(static_cast<int>(m_probability));
      m_settingsWidget->scaleSpin->setValue(m_scale);
    }
    return m_settingsWidget;
  }

  void ThermalEllipsoidEngine::writeSettings(QSettings &settings) const
  {
    Engine::writeSettings(settings);
    settings.setValue("probability", static_cast<int>(m_probability));
    settings.setValue("scale", m_scale);
  }

  void ThermalEllipsoidEngine::readSettings(QSettings &settings)
  {
    Engine::readSettings(settings);
    setProbability(settings.value("probability", 0).toInt());
    setScale(settings.value("scale", 1.0).toDouble());
  }

  void ThermalEllipsoidEngine::settingsWidgetDestroyed()
  {
    m_settingsWidget = 0;
  }

  void ThermalEllipsoidEngine::setProbability(int index)
  {
    if (index < 0 || index > 2)
      index = 0;
    m_probability = static_cast<ThermalEllipsoidGeometry::Probability>(index);
    emit changed();
  }

  void ThermalEllipsoidEngine::setScale(double scale)
  {
    if (!std::isfinite(scale) || scale <= 0.0)
      scale = 1.0;
    m_scale = scale;
    emit changed();
  }

}
