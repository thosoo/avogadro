/**********************************************************************
  BSDYEngine - Dynamic detail engine for "balls and sticks" display

  Copyright (C) 2007 Donald Ephraim Curtis

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

#ifndef BSDYENGINE_H
#define BSDYENGINE_H

#include <avogadro/global.h>
#include <avogadro/engine.h>

#include "../rendering/snapshottessellator.h"

#include <QSet>

#include "ui_bsdysettingswidget.h"

namespace Avogadro {

  //! Ball and Stick Engine class.
  class Atom;
  class BSDYSettingsWidget;
  class BSDYEngine : public Engine
  {
    Q_OBJECT
    AVOGADRO_ENGINE("Ball and Stick", tr("Ball and Stick"),
                    tr("Renders primitives using Balls (atoms) and Sticks (bonds)"))

    public:
      //! Constructor
      BSDYEngine(QObject *parent=0);

      Engine *clone() const;

      //! Deconstructor
      ~BSDYEngine();

      bool renderOpaque(PainterDevice *pd);
      bool renderTransparent(PainterDevice *pd);
      bool renderQuick(PainterDevice *pd);
      bool renderPick(PainterDevice *pd);

      double transparencyDepth() const;

      Engine::Layers layers() const;

      double radius(const PainterDevice *pd, const Primitive *p = 0) const;

      /**
       * Capture this engine's current settings and per-atom render parameters
       * (radius, color) as a JobParams for the snapshot tessellator
       * (gpu-rendering-pipeline task 6.1 / design A7). The worker threads
       * build from (SceneSnapshot, JobParams) pairs, so the widget needs a
       * faithful capture of exactly what renderOpaque/renderTransparent would
       * resolve per atom. Selection sets are passed in by the caller (the
       * widget owns the selection state).
       */
      rendering::JobParams captureJobParams(
        const Color *colorMap,
        const QSet<unsigned int> &selectedAtomIds,
        const QSet<unsigned int> &selectedBondIds) const override;

      bool isSnapshotCovered() const override { return true; }

      /**
       * @return a pointer to the BSDYEngine settings widget
       */
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
      double radius(const Atom *atom) const;

      BSDYSettingsWidget *m_settingsWidget;

      double m_atomRadiusPercentage;
      const double m_atomRadiusScale;
      double m_bondRadius;
      const double m_bondRadiusScale;
      int m_atomRadiusType;
      int m_showMulti;

      double m_alpha; // transparency of the balls & sticks

      /**
       * Function pointer for the radius function to be used for rendering.
       */
      double (*pRadius)(const Atom *atom);

   private Q_SLOTS:
      void settingsWidgetDestroyed();

      /**
       * @param percent percentage of the VdwRad
       */
      void setAtomRadiusPercentage(int percent);
      /**
       * @param value determines if covalent or VdW radii are used
       */
      void setAtomRadiusType(int value);
      /**
       * @param value radius of the bonds * 10
       */
      void setBondRadius(int value);

      /**
       * @param value determines where multiple bonds are shown
       */
      void setShowMulti(int value);

      /**
       * @param value opacity of the balls & sticks / 20
       */
      void setOpacity(int value);

  };

  class BSDYSettingsWidget : public QWidget, public Ui::BSDYSettingsWidget
  {
    public:
      BSDYSettingsWidget(QWidget *parent=0) : QWidget(parent) {
        setupUi(this);
      }
  };

  class BSDYEngineFactory : public QObject, public PluginFactory
  {
    Q_OBJECT
    Q_INTERFACES(Avogadro::PluginFactory)
    // The autooptrepro test compiles this TU together with the DrawTool and
    // AutoOptTool plugin TUs; those builds define AVOGADRO_TEST_NOPLUGIN to
    // skip Q_PLUGIN_METADATA (moc's qt_plugin_instance symbols would collide
    // across the factory TUs). The test instantiates BSDYEngine directly and
    // never loads it through Qt's plugin loader. Guard mirrors drawtool.h.
#ifndef AVOGADRO_TEST_NOPLUGIN
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
#endif
    AVOGADRO_ENGINE_FACTORY(BSDYEngine);
  };

} // end namespace Avogadro

#endif
