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

namespace Avogadro {

  //! Ellipsoid Engine class - renders atoms as displacement ellipsoids
  class Atom;
  class Bond;
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
    //! Render an Atom as an ellipsoid
    bool renderAtom(PainterDevice *pd, const Atom *a);

    //! Render a bond as a stick
    bool renderBond(PainterDevice *pd, const Bond *b);

    //! Get the maximum semi-axis length for an atom
    double maxSemiAxis(const Atom *a) const;

    //! Scale multiplier for ellipsoid size
    double m_scale;

    //! Mesh quality (subdivision levels for icosphere)
    int m_meshQuality;

    //! Opacity of the ellipsoids
    double m_opacity;

    //! Whether to show ellipsoids
    bool m_showEllipsoids;

    //! Whether to use isotropic fallback for atoms without U-tensor
    bool m_useIsotropicFallback;

    //! Whether to show eigenvector axes
    bool m_showAxes;
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
