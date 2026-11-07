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

#include "config.h"

#include "erasetool.h"
#include "drawcommand.h"

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/camera.h>
#include <avogadro/glwidget.h>
#include <avogadro/painter.h>
#include <avogadro/toolgroup.h>
#include <avogadro/undosequence.h>

#include <Eigen/Core>

#include <QtPlugin>
#include <QAction>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QIcon>
#include <QSettings>
#include <QMouseEvent>
#include <QDebug>

#include <cmath>

using Eigen::Vector3d;

namespace Avogadro {

  //! Minimum total path length (device pixels) that distinguishes a lasso from
  //! a click (design D4). A press-release whose travel is below this is a click;
  //! a longer drag is a lasso.
  const double kMinLassoPath = 10.0;

  //! Screen-space pick radius (device pixels) for the click gesture (D10). An
  //! atom whose projected position is within this distance of the click point is
  //! a candidate; the nearest one is deleted.
  const double kAtomPickRadius = 12.0;

  EraseTool::EraseTool(QObject *parent) : Tool(parent),
    m_toolActivatedConnected(false),
    m_dragging(false),
    m_addHydrogens(true),
    m_addHydrogensCheck(0),
    m_settingsWidget(0)
  {
    QAction *action = activateAction();
    action->setIcon(QIcon(QString::fromUtf8(":/erase/erase.png")));
    action->setToolTip(tr("Erase Tool\n\n"
                          "Left Mouse Drag: \tDrag a loop around atoms to delete them\n"
                          "Left Mouse Click: \tDelete the single atom under the pointer"));
    // No keyboard shortcut in this change (F7 is reserved for a future one).
  }

  EraseTool::~EraseTool()
  {
    if (m_settingsWidget)
      m_settingsWidget->deleteLater();
  }

  int EraseTool::usefulness() const
  {
    // Just below the Draw tool so the eraser sits next to it in the palette.
    return 3000000;
  }

  QUndoCommand* EraseTool::mousePressEvent(GLWidget *widget, QMouseEvent *event)
  {
    // Lazily connect to tool switches (UniqueConnection makes this idempotent)
    // so an in-flight lasso is abandoned when the user switches tools mid-drag.
    if (!m_toolActivatedConnected && widget && widget->toolGroup()) {
      connect(widget->toolGroup(), SIGNAL(toolActivated(Tool *)),
              this, SLOT(toolActivated(Tool *)), Qt::UniqueConnection);
      m_toolActivatedConnected = true;
    }

    // Abandon any stale state left by an interrupted gesture.
    abandonLasso();

    if (!widget || !widget->molecule()) {
      event->ignore();
      return 0;
    }

    // Only the left button starts a lasso; other buttons pass to the navigate
    // tool.
    if (event->button() != Qt::LeftButton) {
      event->ignore();
      return 0;
    }

    m_dragging = true;
    m_molecule = widget->molecule();
    m_lassoPath.clear();
    m_lassoPath.append(event->position());
    event->accept();
    return 0;
  }

  QUndoCommand* EraseTool::mouseMoveEvent(GLWidget *widget, QMouseEvent *event)
  {
    Q_UNUSED(widget);
    if (!m_dragging)
      return 0;

    m_lassoPath.append(event->position());
    event->accept();
    return 0;
  }

  QUndoCommand* EraseTool::mouseReleaseEvent(GLWidget *widget, QMouseEvent *event)
  {
    if (!m_dragging || !widget) {
      abandonLasso();
      return 0;
    }

    // Record the final point.
    m_lassoPath.append(event->position());

    // D4: minimum-path guard. A sub-threshold press-release is a click (delete
    // the single atom under the pointer); a longer drag is a lasso.
    if (pathLength(m_lassoPath) < kMinLassoPath) {
      return handleClick(widget, event);
    }

    Molecule *molecule = widget->molecule();
    if (!molecule) {
      abandonLasso();
      return 0;
    }

    // D6: acquire the molecule write lock; on contention abort the gesture
    // cleanly (discard polyline, no commands, no transient state).
    if (!molecule->lock()->tryLockForWrite()) {
      abandonLasso();
      return 0;
    }

    // D2: containment in screen space at release. Project every atom through
    // the camera, bbox-cull against the loop's bounding box, then even-odd
    // point-in-polygon on the survivors. The path auto-closes.
    const QRectF bounds = pathBounds(m_lassoPath);
    const QList<Atom *> atoms = molecule->atoms();
    QList<int> victims;
    QList<int> hydrogenVictims;
    for (int i = 0; i < atoms.size(); ++i) {
      Atom *atom = atoms.at(i);
      const Vector3d sp = widget->camera()->project(*atom->pos());
      const QPointF pt(sp.x(), sp.y());
      if (!bounds.contains(pt))
        continue;
      if (pointInPolygon(m_lassoPath, pt)) {
        // D5: managed-hydrogen parity. With adjust-hydrogens enabled, a
        // managed (bonded) hydrogen is not an independent lasso victim; the
        // heavy-atom delete churns it. With it disabled, every enclosed atom is
        // a victim, hydrogens included.
        if (m_addHydrogens && atom->isHydrogen() && int(atom->valence()))
          continue;
        // Order matters for undo: a DeleteAtomDrawCommand records bonds only to
        // non-hydrogen neighbors (drawcommand.cpp skips hydrogens), so a bonded
        // hydrogen's bond is re-added only by that hydrogen's own command. If a
        // hydrogen victim is restored before its heavy-atom neighbor, the bond
        // is created against a not-yet-restored atom and ends up null-ended
        // (FALSE_ID). Putting hydrogen victims first means an UndoSequence undo
        // restores the heavy atom before the hydrogen re-adds its bond.
        if (atom->isHydrogen())
          hydrogenVictims.append(i);
        else
          victims.append(i);
      }
    }
    victims = hydrogenVictims + victims;

    // D3: one DeleteAtomDrawCommand per victim inside an UndoSequence so a
    // single lasso is a single undo step. The commands snapshot atom data at
    // construction (before any mutation); the actual deletions run later when
    // the sequence is pushed (redo) under the undo stack's write lock.
    UndoSequence *seq = 0;
    if (!victims.isEmpty()) {
      seq = new UndoSequence();
      seq->setText(tr("Lasso Delete"));
      for (int i : victims) {
        DeleteAtomDrawCommand *cmd = new DeleteAtomDrawCommand(molecule, i,
                                                               m_addHydrogens);
        seq->append(cmd);
      }
    }

    molecule->lock()->unlock();
    abandonLasso();
    event->accept();
    return seq;
  }

  bool EraseTool::paint(GLWidget *widget)
  {
    if (!m_dragging || m_lassoPath.size() < 2 || !widget)
      return true;

    Painter *painter = widget->painter();
    if (!painter)
      return true;

    // D7: screen-space loop overlay. Unproject the recorded device-pixel
    // points into world space at the molecule-center depth, then draw a closed
    // line loop through the real painter so it renders on modern/compat/legacy
    // tiers without "painter not active" diagnostics.
    painter->setColor(0.9, 0.2, 0.2, 1.0);
    const Vector3d ref = widget->center();
    QList<Vector3d> worldPts;
    worldPts.reserve(m_lassoPath.size());
    for (const QPointF &p : m_lassoPath) {
      worldPts.append(widget->camera()->unProject(QPoint(qRound(p.x()),
                                                         qRound(p.y())), ref));
    }
    // The overlay draws after the instanced/fixed-function frame, which has
    // already filled the depth buffer with molecule geometry. Draw the lasso
    // with depth testing disabled and the depth mask locked so it composites
    // above the atoms instead of being occluded where the loop passes behind
    // them. The state is restored on every exit below.
    GLboolean depthTest = GL_FALSE;
    glGetBooleanv(GL_DEPTH_TEST, &depthTest);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    painter->drawLineLoop(worldPts, 1.0);
    glDepthMask(GL_TRUE);
    if (depthTest == GL_TRUE)
      glEnable(GL_DEPTH_TEST);
    return true;
  }

  void EraseTool::toolActivated(Tool *tool)
  {
    if (tool == this)
      return;
    abandonLasso();
  }

  void EraseTool::setMolecule(Molecule *molecule)
  {
    if (molecule != m_molecule)
      abandonLasso();
    m_molecule = molecule;
  }

  void EraseTool::setAddHydrogens(int state)
  {
    m_addHydrogens = (state == Qt::Checked);
  }

  QWidget *EraseTool::settingsWidget()
  {
    if (!m_settingsWidget) {
      m_settingsWidget = new QWidget;

      m_addHydrogensCheck = new QCheckBox(tr("Adjust Hydrogens"), m_settingsWidget);
      m_addHydrogensCheck->setCheckState(m_addHydrogens ? Qt::Checked : Qt::Unchecked);

      QVBoxLayout *layout = new QVBoxLayout;
      layout->addWidget(m_addHydrogensCheck);
      layout->addStretch(1);
      m_settingsWidget->setLayout(layout);

      connect(m_addHydrogensCheck, SIGNAL(stateChanged(int)),
              this, SLOT(setAddHydrogens(int)));
      connect(m_settingsWidget, SIGNAL(destroyed()),
              this, SLOT(settingsWidgetDestroyed()));
    }
    return m_settingsWidget;
  }

  void EraseTool::writeSettings(QSettings &settings) const
  {
    Tool::writeSettings(settings);
    settings.setValue("addHydrogens", m_addHydrogens);
  }

  void EraseTool::readSettings(QSettings &settings)
  {
    Tool::readSettings(settings);
    m_addHydrogens = settings.value("addHydrogens", true).toBool();
    if (m_addHydrogensCheck)
      m_addHydrogensCheck->setCheckState(m_addHydrogens ? Qt::Checked : Qt::Unchecked);
  }

  void EraseTool::abandonLasso()
  {
    m_dragging = false;
    m_lassoPath.clear();
  }

  QUndoCommand *EraseTool::handleClick(GLWidget *widget, QMouseEvent *event)
  {
    // D10: a sub-threshold press-release is a click — delete the single atom
    // nearest the click point within the pick radius. The click point is the
    // release position (the last recorded path vertex).
    if (!widget) {
      abandonLasso();
      return 0;
    }

    const QPointF click = m_lassoPath.isEmpty() ? QPointF() : m_lassoPath.last();

    Molecule *molecule = widget->molecule();
    if (!molecule) {
      abandonLasso();
      return 0;
    }

    // D2/D10: screen-space nearest atom, no GL_SELECT. Project every atom,
    // bbox-cull against a box around the click, then pick the nearest within the
    // pick radius. A click on empty space (no atom within the radius) deletes
    // nothing.
    const int index = nearestAtomAt(widget, click);
    if (index < 0) {
      abandonLasso();
      return 0;
    }

    // D6: acquire the molecule write lock; on contention abort the gesture
    // cleanly (discard the click, no commands, no transient state).
    if (!molecule->lock()->tryLockForWrite()) {
      abandonLasso();
      return 0;
    }

    // D5/D10: determine the victim set. With adjust-hydrogens enabled, the
    // single heavy-atom delete churns its managed hydrogens (adjustHydrogens =
    // true). With it disabled, the atom and its bonded explicit hydrogens are
    // all deletion victims. Hydrogen victims go first so an UndoSequence undo
    // restores the heavy atom before each hydrogen re-adds its bond (same
    // ordering invariant as the lasso, D5).
    QList<int> victims;
    QList<int> hydrogenVictims;
    Atom *clickAtom = molecule->atomById(index);
    if (m_addHydrogens) {
      victims.append(index);
    } else {
      victims.append(index);
      const QList<Bond *> bonds = molecule->bonds();
      for (Bond *b : bonds) {
        const unsigned long other = b->otherAtom(clickAtom->id());
        Atom *h = molecule->atomById(other);
        if (h && h->isHydrogen())
          hydrogenVictims.append(static_cast<int>(other));
      }
    }
    victims = hydrogenVictims + victims;

    // D3: one DeleteAtomDrawCommand per victim inside an UndoSequence so a click
    // is a single undo step. The commands snapshot atom data at construction
    // (before any mutation); the deletions run later when the sequence is pushed
    // under the undo stack's write lock.
    UndoSequence *seq = new UndoSequence();
    seq->setText(tr("Lasso Delete"));
    for (int i : victims) {
      DeleteAtomDrawCommand *cmd = new DeleteAtomDrawCommand(molecule, i,
                                                             m_addHydrogens);
      seq->append(cmd);
    }

    molecule->lock()->unlock();
    abandonLasso();
    if (event)
      event->accept();
    return seq;
  }

  int EraseTool::nearestAtomAt(GLWidget *widget, const QPointF &pt)
  {
    Molecule *molecule = widget->molecule();
    if (!molecule)
      return -1;

    // Bbox around the click for a cheap pre-filter before the distance test.
    const QRectF box(pt.x() - kAtomPickRadius, pt.y() - kAtomPickRadius,
                     2.0 * kAtomPickRadius, 2.0 * kAtomPickRadius);

    int best = -1;
    const double bestDist2 = kAtomPickRadius * kAtomPickRadius;
    const QList<Atom *> atoms = molecule->atoms();
    for (int i = 0; i < atoms.size(); ++i) {
      Atom *atom = atoms.at(i);
      const Vector3d sp = widget->camera()->project(*atom->pos());
      const QPointF p(sp.x(), sp.y());
      if (!box.contains(p))
        continue;
      const double dx = p.x() - pt.x();
      const double dy = p.y() - pt.y();
      const double dist2 = dx * dx + dy * dy;
      if (dist2 < bestDist2) {
        best = i;
      }
    }
    return best;
  }

  double EraseTool::pathLength(const QVector<QPointF> &path)
  {
    double len = 0.0;
    for (int i = 1; i < path.size(); ++i) {
      const double dx = path[i].x() - path[i - 1].x();
      const double dy = path[i].y() - path[i - 1].y();
      len += std::sqrt(dx * dx + dy * dy);
    }
    return len;
  }

  QRectF EraseTool::pathBounds(const QVector<QPointF> &path)
  {
    if (path.isEmpty())
      return QRectF();
    qreal minX = path.first().x(), maxX = path.first().x();
    qreal minY = path.first().y(), maxY = path.first().y();
    for (const QPointF &p : path) {
      if (p.x() < minX) minX = p.x();
      if (p.x() > maxX) maxX = p.x();
      if (p.y() < minY) minY = p.y();
      if (p.y() > maxY) maxY = p.y();
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
  }

  bool EraseTool::pointInPolygon(const QVector<QPointF> &poly, const QPointF &pt)
  {
    const int n = poly.size();
    if (n < 3)
      return false;

    // Even-odd ray-casting. The polygon is treated as closed: the last vertex
    // connects back to the first (an unclosed path auto-closes).
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      const QPointF &a = poly[j];
      const QPointF &b = poly[i];
      if ((a.y() > pt.y()) != (b.y() > pt.y())) {
        const double xInt =
          a.x() + (pt.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
        if (pt.x() < xInt)
          inside = !inside;
      }
    }
    return inside;
  }

  void EraseTool::settingsWidgetDestroyed()
  {
    m_settingsWidget = 0;
    m_addHydrogensCheck = 0;
  }

} // end namespace Avogadro
