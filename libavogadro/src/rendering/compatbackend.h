/**********************************************************************
  CompatRendererBackend — the compat (tier 1) submit stage.

  Task 7.2 / design D9: a batched fixed-function flush of the same chunk
  stores the modern tier draws with instanced shaders. The unit sphere and
  cylinder geometry come from rendering/geometry.h (interleaved position +
  normal, unsigned-short indices), so both tiers tessellate identically; the
  compat stage replays each per-instance record through the matrix stack and
  glDrawElements. Draw calls scale with the instance count (fixed function has
  no instancing), which is acceptable for the compat tier's monomer-scale
  target (>= 30 fps) rather than the modern tier's 1M-atom target.

  Visual parity with the modern tier (design D2 "same visual class"):
  blending is enabled for the whole frame and per-instance RGBA (including
  alpha) carries transparency, exactly like ModernRendererBackend::drawInstances.
  Lines are unlit (mirroring GLPainter::drawLine). Lighting comes from the two
  fixed-function lights initializeGL() already configured, so no lighting
  uniform set is needed.

  Picking does NOT go through this class: the compat tier keeps the legacy
  GL_SELECT name-stack path (with the 4.2 overflow detection), which is what
  GLWidget::hits() falls back to whenever the modern tier is not active.
 *****************************************************************/

#ifndef AVOGADRO_RENDERING_COMPATBACKEND_H
#define AVOGADRO_RENDERING_COMPATBACKEND_H

#include <QRect>

#include "chunkstore.h"
#include "records.h"

#include <avogadro/camera.h>

class QMatrix4x4;

namespace Avogadro {
namespace rendering {

class CompatRendererBackend
{
public:
  CompatRendererBackend() = default;
  ~CompatRendererBackend() = default;

  //! Capture the unit geometry client arrays. Must be called while a GL
  //! context is current (GLWidget::initializeGL).
  void initialize();

  //! Whether initialize() found the geometry usable.
  bool isInitialized() const { return m_initialized; }

  //! Set the fixed-function state for a frame (blend, color material,
  //! normalize, depth). The model*projection and lighting state stay on the
  //! GL matrix/attribute stack paintGL() configured.
  bool beginFrame(const Camera *camera, const QRect &viewport, int quality);

  //! Interface parity with the modern backend. Fixed function reads the
  //! matrices off the GL stack, so this is a documented no-op.
  void setModelViewProjection(const QMatrix4x4 &mvp) { Q_UNUSED(mvp); }

  void pushSpheres(const ChunkStore<SphereInstance> &store);
  void pushBonds(const ChunkStore<BondInstance> &store);
  void pushLines(const ChunkStore<LineInstance> &store);

  //! Restore the fixed-function state touched by the frame.
  void endFrame();

  unsigned int drawCallsThisFrame() const { return m_drawCallsThisFrame; }
  unsigned int framesSubmitted() const { return m_framesSubmitted; }

private:
  QRect m_viewport;
  bool m_initialized = false;
  bool m_inFrame = false;

  // Unit geometry (radius 1 sphere; radius-1, height-1, +Z-aligned cylinder),
  // generated once and replayed per instance through the matrix stack.
  QVector<float> m_sphereVertices;   // interleaved pos(3) + normal(3)
  QVector<unsigned short> m_sphereIndices;
  QVector<float> m_cylinderVertices;
  QVector<unsigned short> m_cylinderIndices;

  unsigned int m_drawCallsThisFrame = 0;
  unsigned int m_framesSubmitted = 0;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_COMPATBACKEND_H
