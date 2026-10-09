/**********************************************************************
  Instance records - per-instance data for the tiered rendering pipeline.

  These are the CPU-side per-instance record types that engines emit through
  the SceneBuilder (an Avogadro::Painter) and that a RendererBackend uploads
  and draws. They mirror the formats described in the gpu-rendering-pipeline
  design (D2): a sphere instance is pos(3f) + radius(1f) + color(4xu8); a
  bond half is endpoints(2x3f) + radius + color.

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

#ifndef AVOGADRO_RENDERING_RECORDS_H
#define AVOGADRO_RENDERING_RECORDS_H

#include <Eigen/Geometry>

#include <avogadro/mesh.h>

namespace Avogadro {
namespace rendering {

//! A sphere primitive instance (one atom / atom image).
//! pos is the center, radius the drawn radius, color is RGBA in 0-255.
struct SphereInstance {
  float pos[3] = {0.0f, 0.0f, 0.0f};
  float radius = 1.0f;
  unsigned char color[4] = {255, 255, 255, 255}; // rgba, 0-255
};

//! A bond-half primitive instance (one cylinder segment of a bond).
//! A full bond is emitted as one or two of these so each segment can carry its
//! own radius offset (multi-bond splitting) and per-endpoint lighting.
struct BondInstance {
  float start[3] = {0.0f, 0.0f, 0.0f};
  float end[3] = {0.0f, 0.0f, 0.0f};
  float radius = 0.1f;
  unsigned char color[4] = {255, 255, 255, 255}; // rgba, 0-255
  int order = 1; // bond multiplicity this segment belongs to
  double shift = 0.0; // perpendicular offset applied to this segment
};

//! A line primitive instance (one segment of a bond / stick / wireframe).
struct LineInstance {
  float start[3] = {0.0f, 0.0f, 0.0f};
  float end[3] = {0.0f, 0.0f, 0.0f};
  float width = 1.0f;
  unsigned char color[4] = {255, 255, 255, 255}; // rgba, 0-255
  int order = 1; // bond multiplicity (multi-bond splitting)
  short stipple = 0; // stipple pattern flag (e.g. aromatic)
};

//! A mesh primitive instance (surface / ribbon / cartoon). The GPU buffers are
//! streamed once when the mesh becomes stable (see task 5.1).
struct MeshInstance {
  const Mesh *mesh = nullptr;
  int mode = 0; // 0 = filled, 1 = lines, 2 = points
  unsigned char color[4] = {255, 255, 255, 255}; // rgba, 0-255
  bool colored = false; // true => use the mesh's own per-vertex colors
};

//! Convenience alias for the per-primitive-type chunk stores.
template <typename T>
class ChunkStore; // forward declaration; defined in chunkstore.h

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_RECORDS_H
