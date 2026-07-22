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

#include "ellipsoidengine.h"

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/color.h>
#include <avogadro/glwidget.h>
#include <avogadro/molecule.h>
#include <avogadro/obeigenconv.h>
#include <avogadro/painter.h>
#include <avogadro/painterdevice.h>

#include <openbabel/elements.h>

#include <Eigen/Dense>

#include <QSettings>

using Eigen::Vector3d;
using Eigen::Matrix3d;

namespace Avogadro {

  EllipsoidEngine::EllipsoidEngine(QObject *parent)
    : Engine(parent),
      m_scale(1.0),
      m_meshQuality(3),
      m_opacity(1.0),
      m_showEllipsoids(true),
      m_useIsotropicFallback(true),
      m_showAxes(false)
  {
  }

  EllipsoidEngine::~EllipsoidEngine()
  {
  }

  Engine *EllipsoidEngine::clone() const
  {
    EllipsoidEngine *eng = new EllipsoidEngine;
    eng->m_scale = m_scale;
    eng->m_meshQuality = m_meshQuality;
    eng->m_opacity = m_opacity;
    eng->m_showEllipsoids = m_showEllipsoids;
    eng->m_useIsotropicFallback = m_useIsotropicFallback;
    eng->m_showAxes = m_showAxes;
    eng->setColorMap(colorMap());
    return eng;
  }

  bool EllipsoidEngine::renderOpaque(PainterDevice *pd)
  {
    // Render atoms as ellipsoids
    foreach(Atom *a, atoms()) {
      if (m_showEllipsoids)
        renderAtom(pd, a);
    }

    // Render bonds as sticks
    foreach(Bond *b, bonds()) {
      renderBond(pd, b);
    }

    return true;
  }

  bool EllipsoidEngine::renderQuick(PainterDevice *pd)
  {
    // For quick rendering, use the same as opaque
    return renderOpaque(pd);
  }

  bool EllipsoidEngine::renderPick(PainterDevice *pd)
  {
    // For picking, render atoms as spheres with the maximum semi-axis
    foreach(Atom *a, atoms()) {
      double r = maxSemiAxis(a);
      pd->painter()->setName(a);
      pd->painter()->drawSphere(a->pos(), r);
    }
    return true;
  }

  double EllipsoidEngine::transparencyDepth() const
  {
    return 0.0; // Opaque
  }

  Engine::Layers EllipsoidEngine::layers() const
  {
    return Opaque;
  }

  Engine::PrimitiveTypes EllipsoidEngine::primitiveTypes() const
  {
    return Atoms | Bonds;
  }

  Engine::ColorTypes EllipsoidEngine::colorTypes() const
  {
    return ColorPlugins;
  }

  double EllipsoidEngine::radius(const PainterDevice *pd, const Primitive *p) const
  {
    Q_UNUSED(pd);
    if (p) {
      if (p->type() == Primitive::AtomType) {
        const Atom *a = static_cast<const Atom *>(p);
        return maxSemiAxis(a);
      }
    }
    return 0.5; // Default bond radius
  }

  QWidget *EllipsoidEngine::settingsWidget()
  {
    // For now, return 0. A proper settings widget can be added later.
    return 0;
  }

  void EllipsoidEngine::writeSettings(QSettings &settings) const
  {
    settings.setValue("scale", m_scale);
    settings.setValue("meshQuality", m_meshQuality);
    settings.setValue("opacity", m_opacity);
    settings.setValue("showEllipsoids", m_showEllipsoids);
    settings.setValue("useIsotropicFallback", m_useIsotropicFallback);
    settings.setValue("showAxes", m_showAxes);
  }

  void EllipsoidEngine::readSettings(QSettings &settings)
  {
    m_scale = settings.value("scale", 1.0).toDouble();
    m_meshQuality = settings.value("meshQuality", 3).toInt();
    m_opacity = settings.value("opacity", 1.0).toDouble();
    m_showEllipsoids = settings.value("showEllipsoids", true).toBool();
    m_useIsotropicFallback = settings.value("useIsotropicFallback", true).toBool();
    m_showAxes = settings.value("showAxes", false).toBool();
  }

  bool EllipsoidEngine::renderAtom(PainterDevice *pd, const Atom *a)
  {
    // Get color
    Color *map = colorMap();
    if (!map) map = pd->colorMap();

    // Set color based on atom
    map->setFromPrimitive(a);
    pd->painter()->setColor(map);

    // Set name for picking
    pd->painter()->setName(a);

    // Apply opacity
    if (m_opacity < 1.0) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Check if atom has anisotropic displacement parameters
    if (a->hasAnisoU()) {
      // Render as ellipsoid
      pd->painter()->drawEllipsoid(*a->pos(), a->anisoU() * m_scale);
    } else if (m_useIsotropicFallback && a->hasUIso()) {
      // Fall back to sphere scaled by Uiso
      double uIso = a->uIso();
      double radius = sqrt(uIso) * m_scale * 10.0; // Scale factor for visibility
      pd->painter()->drawSphere(a->pos(), radius);
    } else {
      // Fall back to VdW radius sphere
      double radius = 1.7; // Default VdW radius in Angstroms
      pd->painter()->drawSphere(a->pos(), radius);
    }

    // Restore blending
    if (m_opacity < 1.0) {
      glDisable(GL_BLEND);
    }

    return true;
  }

  bool EllipsoidEngine::renderBond(PainterDevice *pd, const Bond *b)
  {
    // Get the two atoms
    Atom *atom1 = pd->molecule()->atomById(b->beginAtomId());
    Atom *atom2 = pd->molecule()->atomById(b->endAtomId());
    if (!atom1 || !atom2) return false;

    // Get color
    Color *map = colorMap();
    if (!map) map = pd->colorMap();

    // Set color based on bond
    map->setFromPrimitive(b);
    pd->painter()->setColor(map);

    // Set name for picking
    pd->painter()->setName(b);

    // Draw stick between atoms
    double bondRadius = 0.15;
    pd->painter()->drawCylinder(*atom1->pos(), *atom2->pos(), bondRadius);

    return true;
  }

  double EllipsoidEngine::maxSemiAxis(const Atom *a) const
  {
    if (a->hasAnisoU()) {
      // Get eigenvalues of U matrix
      Eigen::SelfAdjointEigenSolver<Matrix3d> solver(a->anisoU());
      if (solver.info() == Eigen::Success) {
        Vector3d eigenvalues = solver.eigenvalues();
        double maxVal = eigenvalues.maxCoeff();
        return sqrt(qMax(0.0, maxVal)) * m_scale;
      }
    } else if (m_useIsotropicFallback && a->hasUIso()) {
      return sqrt(a->uIso()) * m_scale * 10.0;
    }
    return 1.7; // Default VdW radius
  }

} // end namespace Avogadro
