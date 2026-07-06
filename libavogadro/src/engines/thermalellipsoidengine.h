/**********************************************************************
  ThermalEllipsoidEngine - Engine for ADP / thermal ellipsoid display
 **********************************************************************/

#ifndef THERMALELLIPSOIDENGINE_H
#define THERMALELLIPSOIDENGINE_H

#include <avogadro/global.h>
#include <avogadro/engine.h>

#include "ui_thermalellipsoidsettingswidget.h"

#include <Eigen/Core>

namespace Avogadro {

  class Atom;
  class ThermalEllipsoidSettingsWidget;

  class ThermalEllipsoidEngine : public Engine
  {
    Q_OBJECT
    AVOGADRO_ENGINE("Thermal Ellipsoids", tr("Thermal Ellipsoids"),
                    tr("Renders anisotropic displacement parameters as thermal ellipsoids"))

    public:
      ThermalEllipsoidEngine(QObject *parent=0);
      ~ThermalEllipsoidEngine();

      Engine *clone() const;

      bool renderOpaque(PainterDevice *pd);
      bool renderQuick(PainterDevice *pd);

      Layers layers() const;
      PrimitiveTypes primitiveTypes() const;
      double radius(const PainterDevice *pd, const Primitive *p = 0) const;

      QWidget* settingsWidget();
      bool hasSettings() { return true; }

      void writeSettings(QSettings &settings) const;
      void readSettings(QSettings &settings);

      enum Probability { Probability50 = 0, Probability90 = 1, Probability99 = 2 };

      static double probabilityScale(Probability probability);
      static bool ellipsoidForAtom(const Atom *atom, Probability probability,
                                   double userScale, Eigen::Matrix3d &axes,
                                   Eigen::Vector3d &radii);

    private:
      bool render(PainterDevice *pd, const Atom *a);
      static bool readUcart(const Atom *atom, Eigen::Matrix3d &ucart);
      static bool diagonalizeUcart(const Eigen::Matrix3d &ucart,
                                   Eigen::Matrix3d &axes,
                                   Eigen::Vector3d &eigenvalues);

      ThermalEllipsoidSettingsWidget *m_settingsWidget;
      Probability m_probability;
      double m_scale;

    private Q_SLOTS:
      void settingsWidgetDestroyed();
      void setProbability(int index);
      void setScale(double scale);
  };

  class ThermalEllipsoidSettingsWidget : public QWidget, public Ui::ThermalEllipsoidSettingsWidget
  {
    public:
      ThermalEllipsoidSettingsWidget(QWidget *parent=0) : QWidget(parent) {
        setupUi(this);
      }
  };

  class ThermalEllipsoidEngineFactory : public QObject, public PluginFactory
  {
    Q_OBJECT
    Q_INTERFACES(Avogadro::PluginFactory)
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
    AVOGADRO_ENGINE_FACTORY(ThermalEllipsoidEngine)
  };

} // end namespace Avogadro

#endif
