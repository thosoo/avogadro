/**********************************************************************
  EraseTool - Tool for deleting atoms with a lasso

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

#ifndef ERASETOOL_H
#define ERASETOOL_H

#include <avogadro/tool.h>
#include <avogadro/molecule.h>

#include <QPointer>
#include <QVector>
#include <QPointF>
#include <QRectF>

class QCheckBox;
class QWidget;

namespace Avogadro {

  class Atom;

  /**
   * @class EraseTool erasetool.h <avogadro/erasetool.h>
   * @brief Delete atoms by drawing a lasso around them.
   *
   * Press on the canvas, drag a freehand loop, and release: every atom whose
   * projected position lies inside the closed loop is deleted in a single undo
   * step. A press-and-release without significant pointer travel is a no-op
   * (a tap never deletes). Deletion is evaluated in screen space at release
   * against the camera's projection, so the tool never uses GL picking and
   * stays independent of pick-slop tuning.
   */
  class EraseTool : public Tool
  {
    Q_OBJECT
    AVOGADRO_TOOL("Erase", tr("Erase"),
                  tr("Delete atoms by drawing a lasso around them"),
                  tr("Erase Settings"))

    public:
      //! Constructor
      EraseTool(QObject *parent = 0);
      //! Destructor
      virtual ~EraseTool();

      //! \name Tool Methods
      //@{
      virtual QUndoCommand* mousePressEvent(GLWidget *widget, QMouseEvent *event);
      virtual QUndoCommand* mouseMoveEvent(GLWidget *widget, QMouseEvent *event);
      virtual QUndoCommand* mouseReleaseEvent(GLWidget *widget, QMouseEvent *event);
      virtual QUndoCommand* mouseDoubleClickEvent(GLWidget *, QMouseEvent *) { return 0; }
      virtual QUndoCommand* wheelEvent(GLWidget *, QWheelEvent *) { return 0; }
      virtual QUndoCommand* keyPressEvent(GLWidget *, QKeyEvent *) { return 0; }
      //@}

      virtual bool paint(GLWidget *widget);
      virtual int usefulness() const;
      virtual QWidget *settingsWidget();
      virtual void writeSettings(QSettings &settings) const;
      virtual void readSettings(QSettings &settings);

      //! Even-odd point-in-polygon test. The polygon is treated as closed (an
      //! unclosed path auto-closes with a straight segment from end to start).
      //! Pure and static so it is unit-testable without a GLWidget.
      static bool pointInPolygon(const QVector<QPointF> &poly, const QPointF &pt);

    public Q_SLOTS:
      //! Called by the ToolGroup when another tool is activated. Abandons an
      //! in-flight lasso so no partial deletion or transient state survives.
      void toolActivated(Tool *tool);

      //! Called by the ToolGroup when the active molecule changes. Abandons an
      //! in-flight lasso on a document switch.
      virtual void setMolecule(Molecule *molecule);

      void setAddHydrogens(int state);

    private:
      bool m_toolActivatedConnected;
      bool m_dragging;
      bool m_addHydrogens;

      //! In-progress lasso path in device pixels (the same coordinate space the
      //! scaled mouse events and the camera's projection helpers use).
      QVector<QPointF> m_lassoPath;

      //! The molecule that owns the in-flight lasso, kept so setMolecule() can
      //! detect a document switch and abandon the gesture. QPointer so a
      //! destroyed molecule auto-nulls.
      QPointer<Molecule> m_molecule;

      QCheckBox *m_addHydrogensCheck;
      QWidget *m_settingsWidget;

      //! Clears the in-progress lasso path and dragging flag without pushing any
      //! undo command (the interrupted-gesture exit path).
      void abandonLasso();

      //! Total length (device pixels) of the recorded path; used by the
      //! minimum-path guard (a tap never deletes).
      static double pathLength(const QVector<QPointF> &path);

      //! Bounding box (device pixels) of the recorded path; used to bbox-cull
      //! atoms before the expensive point-in-polygon test.
      static QRectF pathBounds(const QVector<QPointF> &path);

    private Q_SLOTS:
      void settingsWidgetDestroyed();
  };

  class EraseToolFactory : public QObject, public PluginFactory
  {
    Q_OBJECT
    Q_INTERFACES(Avogadro::PluginFactory)
    // The repro test compiles erasetool.cpp directly into the test executable;
    // it instantiates the factory directly (never via Qt's plugin loader), so
    // its plugin metadata is unnecessary there. Skip it for that build only via
    // the AVOGADRO_TEST_NOPLUGIN compile definition, which is set on the test
    // target and never defined for the plugin build.
#ifndef AVOGADRO_TEST_NOPLUGIN
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
#endif
    AVOGADRO_TOOL_FACTORY(EraseTool)
  };

} // end namespace Avogadro

#endif
