/**********************************************************************
  DrawTool - Tool for drawing molecules

  Copyright (C) 2007 Donald Ephraim Curtis
  Copyright (C) 2007-2009 Marcus D. Hanwell
  Copyright (C) 2008 Tim Vandermeersch

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

#ifndef DRAWTOOL_H
#define DRAWTOOL_H

#include <avogadro/tool.h>
#include <avogadro/molecule.h>
#include <QPointer>

class QCheckBox;
class QVBoxLayout;
class QPushButton;
class QComboBox;

namespace OpenBabel {
  class OBForceField;
}

namespace Avogadro {

  class Atom;
  class Bond;
  class GLHit;

  class PeriodicTableView;
  class AddAtomCommand;

  class DrawTool : public Tool
  {
    Q_OBJECT
    AVOGADRO_TOOL("Draw", tr("Draw"),
                  tr("Draw and edit atoms and bonds"),
                  tr("Draw Settings"))

    public:
      //! Constructor
      DrawTool(QObject *parent = 0);
      //! Deconstructor
      virtual ~DrawTool();

      //! \name Tool Methods
      //@{
      //! \brief Callback methods for ui.actions on the canvas.

      /*! Handle a mouse press (i.e., beginning of drawing)
      */
      virtual QUndoCommand* mousePressEvent(GLWidget *widget, QMouseEvent *event);
      /*! Handle a mouse release (i.e., the end of drawing)
      */
      virtual QUndoCommand* mouseReleaseEvent(GLWidget *widget, QMouseEvent *event);
      /*! Handle a mouse move (perhaps drawing a bond)
      */
      virtual QUndoCommand* mouseMoveEvent(GLWidget *widget, QMouseEvent *event);
      virtual QUndoCommand* mouseDoubleClickEvent(GLWidget *, QMouseEvent *) { return 0; }

      /*! Handle a scroll wheel (i.e., zooming in and out of the canvas)
      */
      virtual QUndoCommand* wheelEvent(GLWidget *widget, QWheelEvent *event);

      virtual QUndoCommand* keyPressEvent(GLWidget *widget, QKeyEvent *event);
      //@}

      /**
       * Write the tool settings so that they can be saved between sessions.
       */
      virtual void writeSettings(QSettings &settings) const;

      /**
       * Read in the settings that have been saved for the tool instance.
       */
      virtual void readSettings(QSettings &settings);

      //! The priority of the tool in the toolbar
      virtual int usefulness() const;

      virtual QWidget *settingsWidget();

      void setElement(int i);
      int element() const;

      int bondOrder() const;
      int addHydrogens() const;

    public Q_SLOTS:
      //! Called by the ToolGroup when another tool is activated. Releases an
      //! in-flight draw stroke's transient force-field markers so the anchor
      //! atom is not left frozen/ignored under the new tool.
      void toolActivated(Tool *tool);

      void setAddHydrogens( int state );

      void elementChanged( int index );
      void customElementChanged( int index );
      void bondOrderChanged( int index );
      void setBondOrder(int i);

      void toggleDockTable(int state);

      void clearKeyPressBuffer();

    private:
      // Test hook: the repro test drives the in-flight stroke state machine
      // (private members) directly and asserts on the toolActivated() release
      // path without depending on GL picking under xvfb.
      friend class DrawToolExposed;

      Qt::MouseButtons    m_buttons;

      bool                m_movedSinceButtonPressed;
      bool                m_toolActivatedConnected;

      QPoint              m_initialDraggingPosition;
      QPoint              m_lastDraggingPosition;

      QString   m_keyPressBuffer;

      bool m_beginAtomAdded;
      bool m_endAtomAdded;
      // Cross-event stroke state is stored as molecule ids, not raw pointers,
      // so structural churn between mouse events (undo/redo, hydrogen
      // adjustment, deletion, molecule replacement) can never leave a dangling
      // reference: each id is resolved against the live molecule at use time
      // and a failed lookup abandons the interrupted stroke instead of
      // dereferencing a removed atom/bond.
      unsigned long m_beginAtomId;
      unsigned long m_endAtomId;
      int m_element;

      unsigned long m_bondId;
      int m_bondOrder;

      int m_prevAtomElement;

      unsigned long m_prevBondId;
      int m_prevBondOrder;

      // Molecule that owns m_beginAtomId, kept so toolActivated() (which has no
      // GLWidget) can resolve the begin atom to restore its element after an
      // alchemy edit when the tool is switched mid-stroke. QPointer so a
      // destroyed molecule auto-nulls instead of leaving a dangling pointer.
      QPointer<Molecule> m_beginMolecule;

      bool m_addHydrogens;
      QUndoCommand *m_hydrogenCommand;

      QList<GLHit> m_hits;

      QComboBox *m_comboElements;
      QList<int> m_elementsIndex;
      QComboBox *m_comboBondOrder;
      QCheckBox *m_addHydrogensCheck;
      QCheckBox *m_dockTableCheck;
      QPushButton *m_tableButton;
      PeriodicTableView *m_periodicTable;
      QPushButton *m_fragmentButton;
      QVBoxLayout *m_layout;

      QWidget *m_settingsWidget;

      Atom *addAtom(GLWidget *widget, Atom *beginAtom, const QPoint& p);
      void moveAtom(GLWidget *widget, Atom *atom, Atom *beginAtom, const QPoint& p);
      Bond *addBond(Molecule *molecule, Atom *beginAtom, Atom *endAtom);

      // Resolves the cross-event stroke state (stored atom/bond ids) to live
      // pointers against `molecule`. Returns false when any held id no longer
      // refers to a live atom/bond (structural churn between events), in which
      // case the caller must abandon the interrupted stroke. On success fills
      // the out-parameters with the resolved pointers (nullptr when that slot
      // holds no id).
      bool prepareStrokeState(Molecule *molecule,
                              Atom **beginAtom, Atom **endAtom,
                              Bond **bond, Bond **prevBond);
      // Clears all in-flight stroke state (ids, flags, remembered element and
      // bond order). Does not touch m_hydrogenCommand ownership or the
      // force-field markers; the caller manages those.
      void resetStrokeState();

      OpenBabel::OBForceField *m_forceField;

    private Q_SLOTS:
      void settingsWidgetDestroyed();
  };

  class DrawToolFactory : public QObject, public PluginFactory
  {
    Q_OBJECT
    Q_INTERFACES(Avogadro::PluginFactory)
    // The repro test (libavogadro/tests/autooptreprotest.cpp) compiles both
    // drawtool.cpp and autoopttool.cpp into one executable. Both factories
    // declare Q_PLUGIN_METADATA, whose moc generates the Qt plugin-export
    // symbols (qt_plugin_instance etc.); defining both in one TU set would
    // collide. The test instantiates the factory directly (never via Qt's
    // plugin loader), so its plugin metadata is unnecessary there. Skip it for
    // that build only via the AVOGADRO_TEST_NOPLUGIN compile definition, which
    // is set on the test target and is never defined for the plugin build.
#ifndef AVOGADRO_TEST_NOPLUGIN
    Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
#endif
    AVOGADRO_TOOL_FACTORY(DrawTool)
  };

} // end namespace Avogadro

#endif
