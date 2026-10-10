/**********************************************************************
  ModernRendererBackend - modern-tier submit stage with chunked, budgeted,
  fenced instance upload (gpu-rendering-pipeline task 2.2).

  Implements RendererBackend for the modern tier. It owns one persistent VBO per
  instance type and uploads only the dirty ranges reported by each ChunkStore,
  sliced by a ChunkedUploader so that no single frame spends more than a fixed
  budget (default 2 ms) of CPU time uploading. Overflow ranges are left dirty
  and uploaded on the next frame, so a 50k-atom rebuild never stalls input in
  one unbounded glBufferSubData.

  GL sync fences (glFenceSync) are issued after each upload batch and waited on
  (with a bounded, non-blocking poll) before the next upload to the same VBO, so
  the CPU never stalls on glFinish and a VBO is never overwritten while the GPU
  is still reading it.

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

#ifndef AVOGADRO_RENDERING_MODERNBACKEND_H
#define AVOGADRO_RENDERING_MODERNBACKEND_H

#include <QElapsedTimer>
#include <QtOpenGL/QOpenGLBuffer>
#include <QtOpenGL/QOpenGLFunctions_3_3_Core>
#include <QVector>

#include <avogadro/glhit.h>
#include <avogadro/camera.h>

#include "chunkstore.h"
#include "chunkeduploader.h"
#include "records.h"
#include "rendererbackend.h"

namespace Avogadro {
namespace rendering {

//! One persistent instance VBO plus its upload bookkeeping.
struct InstanceVBO {
  QOpenGLBuffer buffer;
  bool created = false;
  std::size_t capacityBytes = 0; // allocated byte capacity
  ChunkedUploader uploader;      // per-frame budget scheduler
  GLsync fence = nullptr;        // last upload fence for this VBO
};

class A_EXPORT ModernRendererBackend : public RendererBackend {
public:
  //! Upper bound on a single glBufferSubData: instance buffers are uploaded as
  //! chunks no larger than this, never one unbounded write.
  static constexpr std::size_t kMaxChunkBytes = 256 * 1024; // 256 KB

  ModernRendererBackend();
  ~ModernRendererBackend() override;

  //! Create the VBOs. Must be called while the GL context is current.
  //! `maxSpheres` / `maxBonds` pre-reserve capacity so a rebuild of that size
  //! uploads without a costly (and un-budgeted) VBO reallocation.
  void initialize(int maxSpheres = 0, int maxBonds = 0);

  //! Per-frame upload budget in microseconds (default 2 ms).
  void setBudgetMicros(int budgetMicros) {
    m_sphereVBO.uploader.setBudgetMicros(budgetMicros);
    m_bondVBO.uploader.setBudgetMicros(budgetMicros);
  }

  //! Largest single measured glBufferSubData this frame (microseconds). Used by
  //! the 2.2 test to assert no upload exceeded the budget on a rebuild.
  double maxSingleUploadMicros() const {
    return qMax(m_sphereVBO.uploader.maxSingleUploadMicros(),
                m_bondVBO.uploader.maxSingleUploadMicros());
  }

  //! Number of records still pending upload (left dirty by budget overflow).
  unsigned int pendingUploads() const;

  //! Number of glBufferSubData calls issued this frame (one per uploaded
  //! chunk). > 1 demonstrates the upload is chunked, not one unbounded write.
  int uploadCallCount() const { return m_uploadCalls; }

  //! Largest single chunk uploaded this frame (bytes). Never exceeds
  //! kMaxChunkBytes.
  std::size_t maxChunkBytes() const { return m_maxChunkBytes; }

  // RendererBackend interface.
  bool beginFrame(const Camera *camera, const QRect &viewport,
                  int quality) override;
  void pushSpheres(const ChunkStore<SphereInstance> &store) override;
  void pushBonds(const ChunkStore<BondInstance> &store) override;
  void pushLines(const ChunkStore<LineInstance> &store) override;
  void pushMeshes(const ChunkStore<MeshInstance> &store) override;
  void endFrame() override;
  QList<GLHit> pick(int x, int y, int w, int h) override;

private:
  template <typename T>
  void pushDirtyRanges(const ChunkStore<T> &store, InstanceVBO &vbo);

  //! Ensure `vbo` can hold `bytes`; grow by re-uploading old data (rare, and
  //! outside the per-frame budget — called only when a store exceeds capacity).
  void ensureCapacity(InstanceVBO &vbo, std::size_t bytes);

  //! Wait (bounded, non-blocking) on the VBO's previous fence before a new
  //! upload, then issue a fresh fence after the uploads complete.
  void fenceAfterUpload(InstanceVBO &vbo);

  QOpenGLContext *m_ctx = nullptr;
  InstanceVBO m_sphereVBO;
  InstanceVBO m_bondVBO;
  QOpenGLFunctions_3_3_Core m_gl;
  bool m_glInitialized = false;
  int m_uploadCalls = 0;
  std::size_t m_maxChunkBytes = 0;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_MODERNBACKEND_H
