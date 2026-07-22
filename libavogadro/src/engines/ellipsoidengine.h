/**********************************************************************
  EllipsoidEngine - Engine for rendering atoms as displacement ellipsoids

  Copyright (C) 2024 Avogadro Developers

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

#ifndef ELLIPSOIDENGINE_H
#define ELLIPSOIDENGINE_H

#include <avogadro/global.h>
#include <avogadro/engine.h>

#include "ui_ellipsoidssettingswidget.h"

namespace Avogadro {

  //! Ellipsoid Engine class - renders atoms as displacement ellipsoids
  class Atom;
  class Bond;
  class EllipsoidSettingsWidget;
  class EllipsoidEngine : public Engine
  {
    Q_OBJECT
    AVOGADRO_ENGINE("Crystallographic Ellipsoids", tr("Crystallographic Ellipsoids"),
                    tr("Renders atoms as anisotropic displacement ellipsoids"))

  public:
    //! Constructor
    EllipsoidEngine(QObject *parent = 0);
    //! Destructor
    ~EllipsoidEngine();

    //! Copy
    Engine *clone() const;

    //! \name Render Methods
    //@{
    bool renderOpaque(PainterDevice *pd);
    bool renderQuick(PainterDevice *pd);
    bool renderPick(PainterDevice *pd);
    //@}

    double transparencyDepth() const;
    Layers layers() const;
    PrimitiveTypes primitiveTypes() const;
    ColorTypes colorTypes() const;

    double radius(const PainterDevice *pd, const Primitive *p = 0) const;

    //! \\name Settings access
    //@{
    double scale() const { return m_scale; }
    void setScale(double scale) { m_scale = scale; m_cacheValid = false; }
    bool drawIsotropicSpheres() const { return m_drawIsotropicSpheres; }
    void setDrawIsotropicSpheres(bool draw) { m_drawIsotropicSpheres = draw; }
    //@}>

    QWidget *settingsWidget();
    bool hasSettings() { return true; }

    /**
     * Write the engine settings so that they can be saved between sessions.
     */
    void writeSettings(QSettings &settings) const;

    /**
     * Read in the settings that have been saved for the engine instance.
     */
    void readSettings(QSettings &settings);

  private:
    //! Ensure cached diagonalized U data is up to date
    void ensureCache(PainterDevice *pd) const;

    //! Get cached eigenvectors for an atom
    const Eigen::Matrix3d &cachedEigenvectors(const Atom *a) const;

    //! Get cached semi-axes for an atom
    const Eigen::Vector3d &cachedSemiAxes(const Atom *a) const;

    //! Render an Atom as an ellipsoid
    bool renderAtom(PainterDevice *pd, const Atom *a);

    //! Render a bond as a stick
    bool renderBond(PainterDevice *pd, const Bond *b);

    //! Get the maximum semi-axis length for an atom
    double maxSemiAxis(const Atom *a) const;

    //! Scale multiplier for ellipsoid size
    double m_scale;

    //! Whether to draw isotropic Uiso spheres for atoms without anisotropic data (otherwise VdW spheres)
    bool m_drawIsotropicSpheres;

    //! Cached diagonalized U data per atom (indexed by atom index)
    mutable std::vector<Eigen::Matrix3d> m_cachedEigenvectors;
    mutable std::vector<Eigen::Vector3d> m_cachedSemiAxes;
    mutable bool m_cacheValid;

    EllipsoidSettingsWidget *m_settingsWidget;

  private Q_SLOTS:
    void settingsWidgetDestroyed();
  };

  class EllipsoidSettingsWidget : public QWidget, public Ui::EllipsoidSettingsWidget
  {
    Q_OBJECT
    public:
      EllipsoidSettingsWidget(EllipsoidEngine *engine, QWidget *parent=0)
        : QWidget(parent), m_engine(engine) {
        setupUi(this);
        scaleSpinBox->setValue(engine->scale());
        drawIsotropicCheckBox->setChecked(engine->drawIsotropicSpheres());
        connect(scaleSpinBox, SIGNAL(valueChanged(double)),
                this, SLOT(updateScale(double)));
        connect(drawIsotropicCheckBox, SIGNAL(toggled(bool)),
                this, SLOT(updateDrawIsotropic(bool)));
      }

    private:
      EllipsoidEngine *m_engine;

    private Q_SLOTS:
      void updateScale(double value) {
        if (m_engine)
          m_engine->setScale(value);
      }
      void updateDrawIsotropic(bool checked) {
        if (m_engine)
          m_engine->setDrawIsotropicSpheres(checked);
      }
  };

  //! Generates instances of our EllipsoidEngine class
  class EllipsoidEngineFactory : public QObject, public PluginFactory
  {
    Q_OBJECT
    Q_INTERFACES(Avogadro::PluginFactory)
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
    AVOGADRO_ENGINE_FACTORY(EllipsoidEngine)
  };

} // end namespace Avogadro

#endif // ELLIPSOIDENGINE_H
