/**********************************************************************
  SnapshotTessellator - snapshot-driven, thread-safe primitive emission.

  The ported Engine classes are GUI-affine: during emission they read the live
  Molecule, the GLWidget's selection set, the colour map and per-atom radii
  (all written on the GUI thread). Running them on a worker thread would race
  that state, or force holding the molecule read lock for the whole build and
  starve every editor write. This header therefore defines a *pure*
  tessellator: it reproduces the BSDY ball-and-stick emission from an immutable
  SceneSnapshot plus a GUI-captured JobParams, writing into the same
  ChunkStores the SceneBuilder would produce. It holds no QObject, issues no GL
  and references no live Molecule, so it is safe to run on a worker thread.

  (gpu-rendering-pipeline task 6.1 / design A7)
 *****************************************************************/

#ifndef AVOGADRO_RENDERING_SNAPSHOTTESSELLATOR_H
#define AVOGADRO_RENDERING_SNAPSHOTTESSELLATOR_H

#include <QString>
#include <QVector>
#include <QSet>

#include "scenesnapshot.h"
#include "records.h"
#include "chunkstore.h"

namespace Avogadro {
namespace rendering {

//! Fixed-function settings captured from the engine on the GUI thread.
struct TessellatorSettings {
  float alpha = 1.0f;      // atom/bond transparency (BSDY m_alpha)
  int showMulti = 2;       // BSDY m_showMulti (0 off, nonzero => split by order)
  float bondRadius = 0.1f; // BSDY m_bondRadius
  float shift = 0.15f;     // perpendicular offset per bond-half (multi-bond)
};

//! Per-atom data captured on the GUI thread so the worker never touches GUI
//! state. Colours are resolved here (map->setFromPrimitive + custom colour)
//! because Color resolution is GUI-affine; the worker only reads the bytes.
struct CapturedAtom {
  float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float radius = 0.3f;
  QString customColorName;
};

//! Immutable emission parameters handed from the GUI thread to a worker.
struct JobParams {
  TessellatorSettings settings;
  QVector<CapturedAtom> atoms;   // indexed by atom id
  QSet<unsigned int> selectedAtoms;
  QSet<unsigned int> selectedBonds;
};

//! The chunk stores produced by the tessellator. ChunkStore<T> is implicitly
//! shared, so copying the result between threads is a refcount bump, not a
//! deep copy.
struct TessellationResult {
  ChunkStore<SphereInstance> spheres;
  ChunkStore<BondInstance> bonds;
  ChunkStore<LineInstance> lines;
};

//! Pure BSDY ball-and-stick tessellation. Reproduces renderOpaque +
//! renderTransparent from (snapshot, params) using the SceneBuilder emitter so
//! the resulting records are byte-identical to the GUI-thread engine path.
TessellationResult snapshotTessellateBSDY(const SceneSnapshot &snapshot,
                                          const JobParams &params);

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_SNAPSHOTTESSELLATOR_H
