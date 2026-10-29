/**********************************************************************
  SceneSnapshot - immutable per-molecule revision snapshot.

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
 *****************************************************************/

#ifndef AVOGADRO_RENDERING_SCENESNAPSHOT_H
#define AVOGADRO_RENDERING_SCENESNAPSHOT_H

#include <QVector>

namespace Avogadro {
namespace rendering {

//! A single atom in a scene snapshot. Positions are copied to float (meters in
//! molecular units) so the snapshot never references the live Molecule vectors.
struct SnapshotAtom {
  float pos[3] = {0.0f, 0.0f, 0.0f};
  unsigned char atomicNumber = 0;
  //! The atom's index in the live Molecule (== its position in this vector
  //! for a snapshot taken in molecule order). Carried explicitly so the worker
  //! tessellator can name the primitive with its true molecule index even if a
  //! future snapshot reorders atoms, keeping pick names stable (design D2).
  unsigned int index = 0;
};

//! A single bond in a scene snapshot.
struct SnapshotBond {
  unsigned int begin = 0;
  unsigned int end = 0;
  unsigned int order = 1;
  //! The bond's index in the live Molecule (== its position in this vector
  //! for a snapshot taken in molecule order). Carried explicitly so the worker
  //! tessellator can name every half-segment with this bond's index, so a click
  //! on any segment resolves to this bond, not bond 0 (design D2).
  unsigned int index = 0;
};

//! An immutable snapshot of a molecule at a given revision.
//!
//! Taken by worker threads under Molecule::lock()->lockForRead(). The snapshot
//! model keys on revision: a worker records the revision it snapshotted and
//! discards its result if the molecule changed (revision bumped) before it
//! finished. Worker must copy values, never pointers, because Atom::pos()
//! returns a pointer into the live m_atomPos vector.
struct SceneSnapshot {
  unsigned int revision = 0;
  QVector<SnapshotAtom> atoms;
  QVector<SnapshotBond> bonds;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_SCENESNAPSHOT_H
