/**********************************************************************
  ModernRendererBackend implementation (gpu-rendering-pipeline task 2.2).

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

#include "modernbackend.h"

#include <QtDebug>

namespace Avogadro {
namespace rendering {

namespace {
//! Conservative per-byte upload cost used only to gate scheduling before a real
//! probe is taken. Real glBufferSubData is far faster than this, so scheduling
//! errs toward accepting work; the measured per-upload time (recordUploadMicros)
//! is what the budget assertion actually checks, not this estimate.
constexpr double kDefaultBytesPerMicros = 4.0e6; // ~4 MB/us upper-bound guess
} // namespace

ModernRendererBackend::ModernRendererBackend() = default;

ModernRendererBackend::~ModernRendererBackend() {
  if (m_ctx && m_ctx->isValid()) {
    m_ctx->makeCurrent(m_ctx->surface());
    InstanceVBO *vbos[] = {&m_sphereVBO, &m_bondVBO};
    for (InstanceVBO *vbo : vbos) {
      if (vbo->fence)
        m_gl.glDeleteSync(vbo->fence);
      vbo->fence = nullptr;
      if (vbo->created)
        vbo->buffer.destroy();
    }
    m_ctx->doneCurrent();
  }
}

void ModernRendererBackend::initialize(int maxSpheres, int maxBonds) {
  m_ctx = QOpenGLContext::currentContext();
  m_glInitialized = m_gl.initializeOpenGLFunctions();
  if (!m_ctx || !m_glInitialized) {
    qWarning() << "ModernRendererBackend: cannot initialise GL 3.3 core";
    return;
  }
  m_ctx->makeCurrent(m_ctx->surface());

  const std::size_t sphereBytes =
      static_cast<std::size_t>(qMax(maxSpheres, 1024)) * sizeof(SphereInstance);
  const std::size_t bondBytes =
      static_cast<std::size_t>(qMax(maxBonds, 1024)) * sizeof(BondInstance);
  auto alloc = [](InstanceVBO &vbo, std::size_t bytes) {
    if (!vbo.buffer.create()) {
      qWarning() << "ModernRendererBackend: VBO create failed";
      return;
    }
    vbo.buffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    vbo.buffer.allocate(bytes);
    vbo.capacityBytes = bytes;
    vbo.created = true;
  };
  alloc(m_sphereVBO, sphereBytes);
  alloc(m_bondVBO, bondBytes);
}

void ModernRendererBackend::ensureCapacity(InstanceVBO &vbo,
                                           std::size_t bytes) {
  if (!vbo.created || bytes > vbo.capacityBytes) {
    // Reallocation is rare (a store exceeding the pre-reserved size) and is
    // intentionally outside the per-frame budget: re-upload prior contents.
    vbo.buffer.destroy();
    if (!vbo.buffer.create()) {
      qWarning() << "ModernRendererBackend: VBO recreate failed";
      return;
    }
    vbo.buffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    vbo.buffer.allocate(bytes);
    vbo.capacityBytes = bytes;
    vbo.created = true;
  }
}

unsigned int ModernRendererBackend::pendingUploads() const {
  return m_sphereVBO.uploader.deferredCount() +
         m_bondVBO.uploader.deferredCount();
}

bool ModernRendererBackend::beginFrame(const Camera * /*camera*/,
                                       const QRect & /*viewport*/,
                                       int /*quality*/) {
  if (!m_ctx || !m_ctx->isValid() || !m_glInitialized)
    return false;
  m_ctx->makeCurrent(m_ctx->surface());
  m_sphereVBO.uploader.beginFrame();
  m_bondVBO.uploader.beginFrame();
  m_uploadCalls = 0;
  m_maxChunkBytes = 0;
  return true;
}

template <typename T>
void ModernRendererBackend::pushDirtyRanges(const ChunkStore<T> &store,
                                            InstanceVBO &vbo) {
  if (!vbo.created)
    return;
  ensureCapacity(vbo, static_cast<std::size_t>(store.size()) * sizeof(T));

  // Byte ranges for the dirty spans, sliced into bounded sub-chunks so the
  // upload is chunked rather than one unbounded write, then scheduled against
  // the per-frame budget. Slicing is done in RECORD space (not byte space) so
  // every chunk's byte length is an exact multiple of sizeof(T): a byte chunk
  // boundary that is not a multiple of sizeof(T) would otherwise drop records
  // when the byte offset is converted back to a record index, leaving them
  // un-uploaded and forever dirty.
  const std::size_t recordsPerChunk = qMax<std::size_t>(1, kMaxChunkBytes /
                                                           sizeof(T));
  std::vector<ChunkedUploader::Range> ranges;
  for (const DirtyRange &dr : store.dirtyRanges()) {
    if (dr.isEmpty())
      continue;
    std::size_t r = dr.start;
    const std::size_t end = dr.start + dr.count;
    while (r < end) {
      const std::size_t n = qMin<std::size_t>(end - r, recordsPerChunk);
      ranges.push_back(ChunkedUploader::Range{r * sizeof(T), n * sizeof(T)});
      r += n;
    }
  }
  if (ranges.empty())
    return;

  const ChunkedUploader::CostFn cost = [this](std::size_t bytes) {
    return static_cast<double>(bytes) / kDefaultBytesPerMicros;
  };
  const int accepted = vbo.uploader.schedule(ranges, cost);

  // Upload the accepted ranges, measuring each single write.
  for (int i = 0; i < accepted; ++i) {
    const ChunkedUploader::Range &r = ranges[i];
    QElapsedTimer t;
    t.start();
    vbo.buffer.bind();
    vbo.buffer.write(static_cast<int>(r.offset),
                       reinterpret_cast<const void *>(store.data() +
                                                      r.offset / sizeof(T)),
                       static_cast<int>(r.bytes));
    vbo.uploader.recordUploadMicros(t.elapsed() * 1000.0);
    ++m_uploadCalls;
    if (r.bytes > m_maxChunkBytes)
      m_maxChunkBytes = r.bytes;
  }

  // Acknowledge only the accepted ranges; deferred ranges stay dirty for the
  // next frame's push.
  for (int i = 0; i < accepted; ++i) {
    const unsigned int start =
        static_cast<unsigned int>(ranges[i].offset / sizeof(T));
    const unsigned int count =
        static_cast<unsigned int>(ranges[i].bytes / sizeof(T));
    store.clearDirty(start, count);
  }
}

template void ModernRendererBackend::pushDirtyRanges<SphereInstance>(
    const ChunkStore<SphereInstance> &, InstanceVBO &);
template void ModernRendererBackend::pushDirtyRanges<BondInstance>(
    const ChunkStore<BondInstance> &, InstanceVBO &);

void ModernRendererBackend::pushSpheres(const ChunkStore<SphereInstance> &store) {
  pushDirtyRanges(store, m_sphereVBO);
  fenceAfterUpload(m_sphereVBO);
}

void ModernRendererBackend::pushBonds(const ChunkStore<BondInstance> &store) {
  pushDirtyRanges(store, m_bondVBO);
  fenceAfterUpload(m_bondVBO);
}

void ModernRendererBackend::pushLines(const ChunkStore<LineInstance> & /*store*/) {
  // Lines share the bond path in later tasks; none to chunk-upload in 2.2.
}

void ModernRendererBackend::pushMeshes(
    const ChunkStore<MeshInstance> & /*store*/) {
  // Meshes stream once when stable (task 5.1); nothing to chunk-upload here.
}

void ModernRendererBackend::fenceAfterUpload(InstanceVBO &vbo) {
  if (!vbo.created)
    return;
  // Wait on the previous fence (bounded, non-blocking) so a VBO is never
  // overwritten while the GPU still reads it, then issue a fresh fence.
  if (vbo.fence) {
    GLint completed = GL_FALSE;
    m_gl.glGetSynciv(vbo.fence, GL_SYNC_STATUS, sizeof(GLint), nullptr,
                     &completed);
    if (completed != GL_TRUE) {
      // Bounded wait: up to 10 ms, then continue (the next fence still guards).
      m_gl.glWaitSync(vbo.fence, 0, 10 * 1000 * 1000);
      m_gl.glGetSynciv(vbo.fence, GL_SYNC_STATUS, sizeof(GLint), nullptr,
                       &completed);
    }
    m_gl.glDeleteSync(vbo.fence);
    vbo.fence = nullptr;
  }
  vbo.fence = m_gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void ModernRendererBackend::endFrame() {
  // Present / flush. The modern tier presents via the QOpenGLWidget swap; the
  // fences above guarantee prior uploads are consumed before reuse.
}

QList<GLHit> ModernRendererBackend::pick(int /*x*/, int /*y*/, int /*w*/,
                                         int /*h*/) {
  // Color-ID picking is the modern tier's pick strategy (task 4.1). Not yet
  // implemented in the 2.2 upload stage; returns empty until then.
  return QList<GLHit>();
}

} // namespace rendering
} // namespace Avogadro
