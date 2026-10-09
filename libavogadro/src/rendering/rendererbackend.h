/**********************************************************************
  RendererBackend - swappable submit stage for the tiered rendering pipeline.

  The scene core (snapshot -> chunked instance stores -> dirty ranges) is
  tier-agnostic. This interface is the seam between the builder side (which
  fills ChunkStores via the SceneBuilder emitter) and the submit side (which
  uploads and draws those chunks for a given GL tier).

  The modern tier uses instanced shaders; the compat tier uses a batched
  fixed-function flush of the same chunk stores; the legacy tier keeps the
  pre-existing immediate-mode path. All three implement this seam so the
  GLWidget paint loop can be written once.

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

#ifndef AVOGADRO_RENDERING_RENDERERBACKEND_H
#define AVOGADRO_RENDERING_RENDERERBACKEND_H

#include <QList>
#include <QRect>

#include <avogadro/glhit.h>
#include <avogadro/camera.h>

#include "chunkstore.h"
#include "records.h"

namespace Avogadro {
namespace rendering {

//! Swappable submit stage for one render frame.
//!
//! Typical frame flow (owned by the GLWidget paint loop):
//!   backend->beginFrame(camera, viewport, quality);
//!   SceneBuilder fills its ChunkStores while engines paint through it;
//!   backend->pushSpheres(builder.spheres());
//!   backend->pushBonds(builder.bonds());
//!   ...
//!   backend->endFrame();
//!
//! Picking is also part of the seam so each tier can use its own strategy
//! (color-ID pass on the modern tier, GL_SELECT with overflow detection on
//! the compat/legacy tiers).
class A_EXPORT RendererBackend {
public:
  virtual ~RendererBackend() {}

  //! Begin a frame. camera/viewport/quality describe the pass being submitted.
  //! \return true on success.
  virtual bool beginFrame(const Camera *camera, const QRect &viewport,
                          int quality) = 0;

  //! Submit a completed chunk store for this frame.
  virtual void pushSpheres(const ChunkStore<SphereInstance> &store) = 0;
  virtual void pushBonds(const ChunkStore<BondInstance> &store) = 0;
  virtual void pushLines(const ChunkStore<LineInstance> &store) = 0;
  virtual void pushMeshes(const ChunkStore<MeshInstance> &store) = 0;

  //! End a frame (present / flush).
  virtual void endFrame() = 0;

  //! Return the primitives under a selection region, nearest first.
  virtual QList<GLHit> pick(int x, int y, int w, int h) = 0;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_RENDERERBACKEND_H
