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

#include <vector>

#include <QSettings>

using Eigen::Vector3d;
using Eigen::Matrix3d;

namespace Avogadro {

  EllipsoidEngine::EllipsoidEngine(QObject *parent)
    : Engine(parent),
      m_scale(1.73), // 50% probability ellipsoid (k-factor for 3D)
      m_drawIsotropicSpheres(true),
      m_cacheValid(false),
      m_settingsWidget(0)
  {
  }

  EllipsoidEngine::~EllipsoidEngine()
  {
  }

  Engine *EllipsoidEngine::clone() const
  {
    EllipsoidEngine *eng = new EllipsoidEngine;
    eng->m_scale = m_scale; // k-factor multiplier (1.73 = 50% probability)
    eng->m_drawIsotropicSpheres = m_drawIsotropicSpheres;
    eng->setColorMap(colorMap());
    return eng;
  }

  //! Diagonalize U-matrix once, returning eigenvectors and scaled semi-axes
  static inline void diagonalizeU(const Atom *a, Eigen::Matrix3d &eigenvectors,
                                   Eigen::Vector3d &semiAxes, double scale)
  {
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(a->anisoU() * scale);
    eigenvectors = solver.eigenvectors();
    Eigen::Vector3d eigenvalues = solver.eigenvalues();
    for (int i = 0; i < 3; ++i)
      semiAxes[i] = eigenvalues[i] > 0.0 ? sqrt(eigenvalues[i]) : 0.01;
  }

  //! Compute the point on an ellipsoid surface in a given direction from center
  static inline Eigen::Vector3d ellipsoidSurfacePoint(
      const Eigen::Vector3d &center,
      const Eigen::Matrix3d &eigenvectors,
      const Eigen::Vector3d &semiAxes,
      const Eigen::Vector3d &direction)
  {
    Eigen::Vector3d d = direction.normalized();
    // Transform direction into ellipsoid's principal axis frame
    Eigen::Vector3d dLocal = eigenvectors.transpose() * d;
    // Compute radius using ellipsoid equation: (x/a)² + (y/b)² + (z/c)² = 1
    double invSq = 0.0;
    for (int i = 0; i < 3; ++i) {
      double sa = semiAxes[i];
      if (sa > 1e-6)
        invSq += (dLocal[i] * dLocal[i]) / (sa * sa);
    }
    double r = (invSq > 0.0) ? (1.0 / sqrt(invSq)) : semiAxes.maxCoeff();
    return center + r * d;
  }

  //! Ensure cached diagonalized U data is up to date for all atoms
  void EllipsoidEngine::ensureCache(PainterDevice *) const
  {
    if (m_cacheValid && static_cast<int>(m_cachedEigenvectors.size()) == atoms().size())
      return;

    m_cachedEigenvectors.resize(atoms().size());
    m_cachedSemiAxes.resize(atoms().size());

    foreach(Atom *a, atoms()) {
      if (a->hasAnisoU()) {
        Eigen::Matrix3d eig;
        Eigen::Vector3d sa;
        diagonalizeU(a, eig, sa, m_scale);
        m_cachedEigenvectors[a->index()] = eig;
        m_cachedSemiAxes[a->index()] = sa;
      }
    }
    m_cacheValid = true;
  }

  //! Get cached eigenvectors for an atom (must call ensureCache first)
  const Eigen::Matrix3d &EllipsoidEngine::cachedEigenvectors(const Atom *a) const
  {
    return m_cachedEigenvectors[a->index()];
  }

  //! Get cached semi-axes for an atom (must call ensureCache first)
  const Eigen::Vector3d &EllipsoidEngine::cachedSemiAxes(const Atom *a) const
  {
    return m_cachedSemiAxes[a->index()];
  }

  bool EllipsoidEngine::renderOpaque(PainterDevice *pd)
  {
    // Pre-compute diagonalized U for all atoms (avoids redundant eigenvalue decompositions)
    ensureCache(pd);

    // Render atoms as ellipsoids
    foreach(Atom *a, atoms()) {
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
    // For quick rendering, draw atoms as spheres (max semi-axis) instead of ellipsoids
    foreach(Atom *a, atoms()) {
      Color *map = colorMap();
      if (!map) map = pd->colorMap();
      map->setFromPrimitive(a);
      pd->painter()->setColor(map);
      pd->painter()->setName(a);
      double r = maxSemiAxis(a);
      pd->painter()->drawSphere(a->pos(), r);
    }
    // Render bonds as simple center-to-center sticks (no surface intersection in quick mode)
    foreach(Bond *b, bonds()) {
      Atom *atom1 = pd->molecule()->atomById(b->beginAtomId());
      Atom *atom2 = pd->molecule()->atomById(b->endAtomId());
      if (!atom1 || !atom2) continue;

      Color *map = colorMap();
      if (!map) map = pd->colorMap();
      map->setFromPrimitive(b);
      pd->painter()->setColor(map);
      pd->painter()->setName(b);
      pd->painter()->drawCylinder(*atom1->pos(), *atom2->pos(), 0.15);
    }
    return true;
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
    if (!m_settingsWidget) {
      m_settingsWidget = new EllipsoidSettingsWidget(this);
      connect(m_settingsWidget, SIGNAL(destroyed(QObject*)),
              this, SLOT(settingsWidgetDestroyed()));
    }
    return m_settingsWidget;
  }

  void EllipsoidEngine::settingsWidgetDestroyed()
  {
    m_settingsWidget = 0;
  }

  void EllipsoidEngine::writeSettings(QSettings &settings) const
  {
    settings.setValue("scale", m_scale);
    settings.setValue("drawIsotropicSpheres", m_drawIsotropicSpheres);
  }

  void EllipsoidEngine::readSettings(QSettings &settings)
  {
    m_scale = settings.value("scale", 1.73).toDouble(); // 50% probability default
    m_drawIsotropicSpheres = settings.value("drawIsotropicSpheres", true).toBool();
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

    // Check if atom has anisotropic displacement parameters
    if (a->hasAnisoU()) {
      // Use pre-computed cached diagonalized data
      const Eigen::Matrix3d &eigenvectors = cachedEigenvectors(a);
      const Eigen::Vector3d &semiAxes = cachedSemiAxes(a);
      double maxAxis = semiAxes.maxCoeff();
      pd->painter()->drawEllipsoid(*a->pos(), eigenvectors, semiAxes, maxAxis);
    } else if (m_drawIsotropicSpheres && a->hasUIso()) {
      // Fall back to sphere scaled by Uiso (same k-factor as anisotropic ellipsoids)
      double uIso = a->uIso();
      double radius = m_scale * sqrt(uIso);
      pd->painter()->drawSphere(a->pos(), radius);
    } else {
      // Fall back to element-specific VdW radius sphere
      double radius = OpenBabel::OBElements::GetVdwRad(a->atomicNumber());
      pd->painter()->drawSphere(a->pos(), radius);
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

    // Compute bond endpoints on ellipsoid surfaces using cached data
    Eigen::Vector3d end1, end2;
    if (atom1->hasAnisoU() && atom2->hasAnisoU()) {
      // Both atoms have anisotropic data: compute surface intersections
      const Eigen::Matrix3d &eig1 = cachedEigenvectors(atom1);
      const Eigen::Vector3d &sa1 = cachedSemiAxes(atom1);
      const Eigen::Matrix3d &eig2 = cachedEigenvectors(atom2);
      const Eigen::Vector3d &sa2 = cachedSemiAxes(atom2);

      Eigen::Vector3d vec1to2 = *atom2->pos() - *atom1->pos();
      end1 = ellipsoidSurfacePoint(*atom1->pos(), eig1, sa1, vec1to2);
      end2 = ellipsoidSurfacePoint(*atom2->pos(), eig2, sa2, -vec1to2);
    } else if (atom1->hasAnisoU()) {
      // Only atom1 has anisotropic data
      const Eigen::Matrix3d &eig1 = cachedEigenvectors(atom1);
      const Eigen::Vector3d &sa1 = cachedSemiAxes(atom1);
      Eigen::Vector3d vec1to2 = *atom2->pos() - *atom1->pos();
      end1 = ellipsoidSurfacePoint(*atom1->pos(), eig1, sa1, vec1to2);
      end2 = *atom2->pos();
    } else if (atom2->hasAnisoU()) {
      // Only atom2 has anisotropic data
      const Eigen::Matrix3d &eig2 = cachedEigenvectors(atom2);
      const Eigen::Vector3d &sa2 = cachedSemiAxes(atom2);
      Eigen::Vector3d vec1to2 = *atom2->pos() - *atom1->pos();
      end1 = *atom1->pos();
      end2 = ellipsoidSurfacePoint(*atom2->pos(), eig2, sa2, -vec1to2);
    } else {
      // Neither has anisotropic data: use centers (VdW spheres)
      end1 = *atom1->pos();
      end2 = *atom2->pos();
    }

    // Draw stick between computed endpoints
    double bondRadius = 0.15;
    pd->painter()->drawCylinder(end1, end2, bondRadius);

    return true;
  }

  double EllipsoidEngine::maxSemiAxis(const Atom *a) const
  {
    if (a->hasAnisoU()) {
      // Use cached data if available, otherwise compute on the fly
      if (m_cacheValid && static_cast<long>(a->index()) < static_cast<long>(m_cachedSemiAxes.size())) {
        return m_cachedSemiAxes[a->index()].maxCoeff();
      }
      Eigen::Matrix3d eigenvectors;
      Eigen::Vector3d semiAxes;
      diagonalizeU(a, eigenvectors, semiAxes, m_scale);
      return semiAxes.maxCoeff();
    } else if (m_drawIsotropicSpheres && a->hasUIso()) {
      return m_scale * sqrt(a->uIso());
    }
    return OpenBabel::OBElements::GetVdwRad(a->atomicNumber());
  }

} // end namespace Avogadro
