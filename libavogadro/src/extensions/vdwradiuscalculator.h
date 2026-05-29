/**********************************************************************
  VdwRadiusCalculator - van der Waals enclosing radius helpers

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 ***********************************************************************/

#ifndef VDWRADIUSCALCULATOR_H
#define VDWRADIUSCALCULATOR_H

#include <avogadro/atom.h>
#include <avogadro/bond.h>
#include <avogadro/glwidget.h>
#include <avogadro/molecule.h>
#include <avogadro/primitive.h>
#include <avogadro/primitivelist.h>

#include <openbabel/elements.h>

#include <Eigen/Core>

#include <QtCore/QList>
#include <QtCore/QSet>
#include <QtCore/QtGlobal>

namespace Avogadro {
namespace MolecularProperties {

  static inline double atomVdwRadius(const Atom *atom)
  {
    if (!atom)
      return 0.0;

    // Honor explicit per-atom custom radius overrides when present;
    // otherwise use Open Babel's element vdW radius table.
    if (atom->customRadius() > 0.0)
      return atom->customRadius();

    // Avogadro's current Open Babel dependency provides OBElements::GetVdwRad().
    return OpenBabel::OBElements::GetVdwRad(atom->atomicNumber());
  }

  static inline void appendAtomIfNew(QList<Atom*> *atoms, QSet<Atom*> *seen,
                                     Atom *atom)
  {
    if (atom && !seen->contains(atom)) {
      seen->insert(atom);
      atoms->append(atom);
    }
  }

  static inline QList<Atom*> atomsForVdwRadius(Molecule *mol, GLWidget *widget)
  {
    QList<Atom*> atoms;
    QSet<Atom*> seen;

    if (widget) {
      PrimitiveList selectedPrimitives = widget->selectedPrimitives();

      foreach(Primitive *primitive, selectedPrimitives.subList(Primitive::AtomType))
        appendAtomIfNew(&atoms, &seen, static_cast<Atom*>(primitive));

      foreach(Primitive *primitive, selectedPrimitives.subList(Primitive::BondType)) {
        Bond *bond = static_cast<Bond*>(primitive);
        appendAtomIfNew(&atoms, &seen, bond->beginAtom());
        appendAtomIfNew(&atoms, &seen, bond->endAtom());
      }
    }

    if (atoms.isEmpty() && mol)
      atoms = mol->atoms();

    return atoms;
  }

  static inline double vdwEnclosingRadius(const QList<Atom*> &atoms)
  {
    if (atoms.isEmpty())
      return 0.0;

    // This is an arithmetic-center enclosing radius: it is the maximum
    // distance from the selected atoms' geometric center to their vdW surfaces.
    // It is not a minimum enclosing sphere and not a vdW volume or
    // equivalent-volume radius.
    Eigen::Vector3d center = Eigen::Vector3d::Zero();
    int atomCount = 0;
    foreach(Atom *atom, atoms) {
      if (!atom || !atom->pos())
        continue;

      center += *atom->pos();
      ++atomCount;
    }

    if (!atomCount)
      return 0.0;

    center /= static_cast<double>(atomCount);

    double radius = 0.0;
    foreach(Atom *atom, atoms) {
      if (!atom || !atom->pos())
        continue;

      radius = qMax(radius, (*atom->pos() - center).norm() + atomVdwRadius(atom));
    }

    return radius;
  }

} // namespace MolecularProperties
} // namespace Avogadro

#endif // VDWRADIUSCALCULATOR_H
