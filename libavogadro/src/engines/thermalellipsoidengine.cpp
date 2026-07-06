/**********************************************************************
  ThermalEllipsoidEngine - Engine for ADP / thermal ellipsoid display
 **********************************************************************/

#include "thermalellipsoidengine.h"

#include <avogadro/atom.h>
#include <avogadro/color.h>
#include <avogadro/painterdevice.h>
#include <avogadro/primitive.h>

#include <Eigen/Eigenvalues>

#include <QSettings>

#include <cmath>

namespace Avogadro {

  namespace {
    const double TinyNegativeTolerance = -1.0e-8;

    bool readDoubleProperty(const Atom *atom, const char *name, double &value)
    {
      bool ok = false;
      value = atom->property(name).toDouble(&ok);
      return ok && std::isfinite(value);
    }
  }

  ThermalEllipsoidEngine::ThermalEllipsoidEngine(QObject *parent)
    : Engine(parent), m_settingsWidget(0), m_probability(Probability50), m_scale(1.0)
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
    if (!ellipsoidForAtom(a, m_probability, m_scale, axes, radii))
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
    if (!ellipsoidForAtom(static_cast<const Atom *>(p), m_probability, m_scale, axes, radii))
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
    m_probability = static_cast<Probability>(index);
    emit changed();
  }

  void ThermalEllipsoidEngine::setScale(double scale)
  {
    if (!std::isfinite(scale) || scale <= 0.0)
      scale = 1.0;
    m_scale = scale;
    emit changed();
  }

  double ThermalEllipsoidEngine::probabilityScale(Probability probability)
  {
    switch (probability) {
      case Probability90: return 2.500278;
      case Probability99: return 3.368214;
      case Probability50:
      default: return 1.538172;
    }
  }

  bool ThermalEllipsoidEngine::readUcart(const Atom *atom, Eigen::Matrix3d &ucart)
  {
    if (!atom || atom->property("adp_valid").toString().toLower() != QLatin1String("true"))
      return false;

    double u11, u22, u33, u12, u13, u23;
    if (!readDoubleProperty(atom, "adp_Ucart_11", u11) ||
        !readDoubleProperty(atom, "adp_Ucart_22", u22) ||
        !readDoubleProperty(atom, "adp_Ucart_33", u33) ||
        !readDoubleProperty(atom, "adp_Ucart_12", u12) ||
        !readDoubleProperty(atom, "adp_Ucart_13", u13) ||
        !readDoubleProperty(atom, "adp_Ucart_23", u23))
      return false;

    ucart << u11, u12, u13,
             u12, u22, u23,
             u13, u23, u33;
    return true;
  }

  bool ThermalEllipsoidEngine::diagonalizeUcart(const Eigen::Matrix3d &ucart,
                                                Eigen::Matrix3d &axes,
                                                Eigen::Vector3d &eigenvalues)
  {
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(ucart);
    if (solver.info() != Eigen::Success)
      return false;

    for (int i = 0; i < 3; ++i) {
      double value = solver.eigenvalues()(i);
      if (!std::isfinite(value) || value < TinyNegativeTolerance)
        return false;
    }

    for (int i = 0; i < 3; ++i) {
      const int source = 2 - i; // Eigen returns ascending; render descending.
      eigenvalues(i) = solver.eigenvalues()(source);
      if (eigenvalues(i) < 0.0)
        eigenvalues(i) = 0.0;
      axes.col(i) = solver.eigenvectors().col(source);
    }

    return true;
  }

  bool ThermalEllipsoidEngine::ellipsoidForAtom(const Atom *atom, Probability probability,
                                                double userScale, Eigen::Matrix3d &axes,
                                                Eigen::Vector3d &radii)
  {
    Eigen::Matrix3d ucart;
    Eigen::Vector3d eigenvalues;
    if (!readUcart(atom, ucart) || !diagonalizeUcart(ucart, axes, eigenvalues))
      return false;

    const double scale = probabilityScale(probability) * userScale;
    for (int i = 0; i < 3; ++i)
      radii(i) = std::sqrt(eigenvalues(i)) * scale;

    return radii.allFinite();
  }

}
