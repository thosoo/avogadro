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

#include "geometry.h"
#include "shaders.h"

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

  // Shared geometry VAOs + shaders for the instanced draw (task 2.4). Built on
  // this context; rebuilt on context loss via initializeGL -> initialize().
  buildGeometry();
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
  // Per-frame state reset. The MVP is supplied separately via
  // setModelViewProjection(); without it drawInstances() is a no-op.
  m_sphereCount = 0;
  m_bondCount = 0;
  m_drawCallsThisFrame = 0;
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
  // Watermark: the count drawn this frame is the store size, never the
  // accumulated total. A smaller scene after a larger one must not draw stale
  // instances as garbage (risk #2 in the task design).
  m_sphereCount = static_cast<unsigned int>(store.size());
}

void ModernRendererBackend::pushBonds(const ChunkStore<BondInstance> &store) {
  pushDirtyRanges(store, m_bondVBO);
  fenceAfterUpload(m_bondVBO);
  m_bondCount = static_cast<unsigned int>(store.size());
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
  // Draw the uploaded instance data (one instanced draw per primitive type),
  // then present via the QOpenGLWidget swap. The fences above guarantee prior
  // uploads are consumed before reuse; the draw reads the same VBOs.
  drawInstances();
  ++m_framesSubmitted;
}

QList<GLHit> ModernRendererBackend::pick(int /*x*/, int /*y*/, int /*w*/,
                                         int /*h*/) {
  // Color-ID picking is the modern tier's pick strategy (task 4.1). Not yet
  // implemented in the 2.2 upload stage; returns empty until then.
  return QList<GLHit>();
}

//! Compile + link a program from the sphere/cylinder shader sources, replacing
//! the ${LIGHTING} placeholder with the shared lighting preamble first.
static bool compileShaderProgram(QOpenGLShaderProgram &program,
                                 const QString &vsrc, const QString &fsrc);

void ModernRendererBackend::buildGeometry()
{
  // Sphere: unit sphere tessellation + instanced sphere shader.
  {
    SphereGeometry sphere;
    sphere.setRings(12);
    sphere.setSegments(24);
    sphere.generate();

    if (!m_sphereGeom.vao.create() || !m_sphereGeom.geometryVBO.create()) {
      qWarning() << "ModernRendererBackend: sphere VAO/VBO create failed";
      return;
    }
    m_sphereGeom.geometryVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_sphereGeom.geometryVBO.allocate(sphere.vertices().data(),
                                      sphere.vertices().size() * sizeof(GeometryVertex));
    // Index buffer (GL_ELEMENT_ARRAY_BUFFER): must be an IndexBuffer-type VBO,
    // else ELEMENT_ARRAY_BUFFER stays at 0 and glDrawElements uses a null
    // client-side index pointer (SIGSEGV in the driver). A plain member is
    // VertexBuffer-type, so build it locally with the right type and move it in.
    {
      QOpenGLBuffer indexVBO(QOpenGLBuffer::IndexBuffer);
      indexVBO.create();
      indexVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
      indexVBO.allocate(sphere.indices().data(),
                        sphere.indices().size() * sizeof(unsigned short));
      m_sphereGeom.indexVBO = std::move(indexVBO);
    }
    m_sphereGeom.indexCount = sphere.indexCount();

    if (!compileShaderProgram(m_sphereGeom.program, kSphereVertexShader,
                        kSphereFragmentShader)) {
      qWarning() << "ModernRendererBackend: sphere program compile/link failed";
      return;
    }

    // Bind the VAO and record every attribute (geometry 0/1, instance 2+ with
    // per-instance divisor) plus the ELEMENT_ARRAY_BUFFER. After this, binding
    // the VAO alone restores the full draw configuration.
    if (!m_sphereGeom.program.bind()) {
      qWarning() << "ModernRendererBackend: sphere program bind failed during setup";
      return;
    }
    m_sphereGeom.uMVPloc = m_sphereGeom.program.uniformLocation("uMVP");
    m_sphereGeom.vao.bind();
    m_sphereGeom.geometryVBO.bind();
    m_gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                               sizeof(GeometryVertex), reinterpret_cast<void *>(0));
    m_gl.glEnableVertexAttribArray(0);
    m_gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                               sizeof(GeometryVertex), reinterpret_cast<void *>(12));
    m_gl.glEnableVertexAttribArray(1);
    // Instance attributes read from the (stable-object) upload VBO.
    m_sphereVBO.buffer.bind();
    m_sphereGeom.program.setAttributeBuffer(2, GL_FLOAT, 0, 3, sizeof(SphereInstance));
    m_sphereGeom.program.setAttributeBuffer(3, GL_FLOAT, 12, 1, sizeof(SphereInstance));
    m_sphereGeom.program.setAttributeBuffer(4, GL_UNSIGNED_BYTE, 16, 4, sizeof(SphereInstance));
    m_gl.glEnableVertexAttribArray(2);
    m_gl.glEnableVertexAttribArray(3);
    m_gl.glEnableVertexAttribArray(4);
    m_gl.glVertexAttribDivisor(2, 1);
    m_gl.glVertexAttribDivisor(3, 1);
    m_gl.glVertexAttribDivisor(4, 1);
    m_sphereGeom.vao.release();
    m_sphereGeom.program.release();
    m_sphereGeom.valid = true;
  }

  // Cylinder (bond half): unit cylinder tessellation + instanced cylinder shader.
  {
    CylinderGeometry cylinder;
    cylinder.setSegments(24);
    cylinder.generate();

    if (!m_cylinderGeom.vao.create() || !m_cylinderGeom.geometryVBO.create()) {
      qWarning() << "ModernRendererBackend: cylinder VAO/VBO create failed";
      return;
    }
    m_cylinderGeom.geometryVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_cylinderGeom.geometryVBO.allocate(cylinder.vertices().data(),
                                        cylinder.vertices().size() * sizeof(GeometryVertex));
    {
      QOpenGLBuffer indexVBO(QOpenGLBuffer::IndexBuffer);
      indexVBO.create();
      indexVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
      indexVBO.allocate(cylinder.indices().data(),
                        cylinder.indices().size() * sizeof(unsigned short));
      m_cylinderGeom.indexVBO = std::move(indexVBO);
    }
    m_cylinderGeom.indexCount = cylinder.indexCount();

    if (!compileShaderProgram(m_cylinderGeom.program, kCylinderVertexShader,
                        kCylinderFragmentShader)) {
      qWarning() << "ModernRendererBackend: cylinder program compile/link failed";
      return;
    }

    if (!m_cylinderGeom.program.bind()) {
      qWarning() << "ModernRendererBackend: cylinder program bind failed during setup";
      return;
    }
    m_cylinderGeom.uMVPloc = m_cylinderGeom.program.uniformLocation("uMVP");
    m_cylinderGeom.vao.bind();
    m_cylinderGeom.geometryVBO.bind();
    m_gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                               sizeof(GeometryVertex), reinterpret_cast<void *>(0));
    m_gl.glEnableVertexAttribArray(0);
    m_gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                               sizeof(GeometryVertex), reinterpret_cast<void *>(12));
    m_gl.glEnableVertexAttribArray(1);
    // Instance attributes read from the (stable-object) bond upload VBO.
    m_bondVBO.buffer.bind();
    m_cylinderGeom.program.setAttributeBuffer(2, GL_FLOAT, 0, 3, sizeof(BondInstance));
    m_cylinderGeom.program.setAttributeBuffer(3, GL_FLOAT, 12, 3, sizeof(BondInstance));
    m_cylinderGeom.program.setAttributeBuffer(4, GL_FLOAT, 24, 1, sizeof(BondInstance));
    m_cylinderGeom.program.setAttributeBuffer(5, GL_UNSIGNED_BYTE, 28, 4, sizeof(BondInstance));
    m_gl.glEnableVertexAttribArray(2);
    m_gl.glEnableVertexAttribArray(3);
    m_gl.glEnableVertexAttribArray(4);
    m_gl.glEnableVertexAttribArray(5);
    m_gl.glVertexAttribDivisor(2, 1);
    m_gl.glVertexAttribDivisor(3, 1);
    m_gl.glVertexAttribDivisor(4, 1);
    m_gl.glVertexAttribDivisor(5, 1);
    m_cylinderGeom.vao.release();
    m_cylinderGeom.program.release();
    m_cylinderGeom.valid = true;
  }
}

void ModernRendererBackend::drawInstances()
{
  if (!m_glInitialized || !m_haveMvp)
    return; // no matrix yet: a frame with a stale matrix must not draw

  // Blending + depth test so transparent (alpha < 1) instances composite over
  // opaque ones. Exact back-to-front sorting is out of scope for 2.4 (D2
  // allows "same visual class"); per-instance alpha already carries the
  // transparency.
  m_gl.glEnable(GL_BLEND);
  m_gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  m_gl.glEnable(GL_DEPTH_TEST);
  m_gl.glDepthFunc(GL_LEQUAL);

  if (m_sphereGeom.valid && m_sphereCount > 0) {
    if (!m_sphereGeom.program.bind()) {
      qWarning() << "ModernRendererBackend: sphere program bind failed";
      return;
    }
    m_sphereGeom.program.setUniformValue(m_sphereGeom.uMVPloc, m_mvp);
    // Re-establish every binding explicitly rather than relying on VAO state
    // to persist across the buildGeometry/drawInstances scope split: bind the
    // geometry VBO for attribs 0/1, the index VBO for ELEMENT_ARRAY_BUFFER,
    // then the instance VBO for 2+ with divisors.
    m_sphereGeom.vao.bind();
    m_sphereGeom.geometryVBO.bind();
    m_sphereGeom.indexVBO.bind();
    m_sphereGeom.program.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(GeometryVertex));
    m_sphereGeom.program.setAttributeBuffer(1, GL_FLOAT, 12, 3, sizeof(GeometryVertex));
    m_sphereVBO.buffer.bind();
    m_sphereGeom.program.setAttributeBuffer(2, GL_FLOAT, 0, 3, sizeof(SphereInstance));
    m_sphereGeom.program.setAttributeBuffer(3, GL_FLOAT, 12, 1, sizeof(SphereInstance));
    m_sphereGeom.program.setAttributeBuffer(4, GL_UNSIGNED_BYTE, 16, 4, sizeof(SphereInstance));
    m_gl.glVertexAttribDivisor(2, 1);
    m_gl.glVertexAttribDivisor(3, 1);
    m_gl.glVertexAttribDivisor(4, 1);
    m_gl.glDrawElementsInstanced(GL_TRIANGLES, m_sphereGeom.indexCount,
                                 GL_UNSIGNED_SHORT, nullptr,
                                 static_cast<GLsizei>(m_sphereCount));
    ++m_drawCallsThisFrame;
    m_sphereGeom.vao.release();
  }

  if (m_cylinderGeom.valid && m_bondCount > 0) {
    if (!m_cylinderGeom.program.bind()) {
      qWarning() << "ModernRendererBackend: cylinder program bind failed";
      return;
    }
    m_cylinderGeom.program.setUniformValue(m_cylinderGeom.uMVPloc, m_mvp);
    m_cylinderGeom.vao.bind();
    m_cylinderGeom.geometryVBO.bind();
    m_cylinderGeom.indexVBO.bind();
    m_cylinderGeom.program.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(GeometryVertex));
    m_cylinderGeom.program.setAttributeBuffer(1, GL_FLOAT, 12, 3, sizeof(GeometryVertex));
    m_bondVBO.buffer.bind();
    m_cylinderGeom.program.setAttributeBuffer(2, GL_FLOAT, 0, 3, sizeof(BondInstance));
    m_cylinderGeom.program.setAttributeBuffer(3, GL_FLOAT, 12, 3, sizeof(BondInstance));
    m_cylinderGeom.program.setAttributeBuffer(4, GL_FLOAT, 24, 1, sizeof(BondInstance));
    m_cylinderGeom.program.setAttributeBuffer(5, GL_UNSIGNED_BYTE, 28, 4, sizeof(BondInstance));
    m_gl.glVertexAttribDivisor(2, 1);
    m_gl.glVertexAttribDivisor(3, 1);
    m_gl.glVertexAttribDivisor(4, 1);
    m_gl.glVertexAttribDivisor(5, 1);
    m_gl.glDrawElementsInstanced(GL_TRIANGLES, m_cylinderGeom.indexCount,
                                 GL_UNSIGNED_SHORT, nullptr,
                                 static_cast<GLsizei>(m_bondCount));
    ++m_drawCallsThisFrame;
    m_cylinderGeom.vao.release();
  }
}

//! Compile + link a program from the sphere/cylinder shader sources, replacing
//! the ${LIGHTING} placeholder with the shared lighting preamble first.
static bool compileShaderProgram(QOpenGLShaderProgram &program,
                                 const QString &vsrc, const QString &fsrc)
{
  QString vs = vsrc;
  vs.replace("${LIGHTING}", kLightingPreamble);
  QString fs = fsrc;
  fs.replace("${LIGHTING}", kLightingPreamble);

  QOpenGLShader *vs_ = new QOpenGLShader(QOpenGLShader::Vertex);
  QOpenGLShader *fs_ = new QOpenGLShader(QOpenGLShader::Fragment);
  if (!vs_->compileSourceCode(vs) || !fs_->compileSourceCode(fs)) {
    qWarning() << "modern backend shader log:" << vs_->log() << fs_->log();
    delete vs_;
    delete fs_;
    return false;
  }
  if (!program.addShader(vs_) || !program.addShader(fs_)) {
    qWarning() << "modern backend shader program log:" << program.log();
    delete vs_;
    delete fs_;
    return false;
  }
  delete vs_;
  delete fs_;
  return program.link();
}

} // namespace rendering
} // namespace Avogadro
