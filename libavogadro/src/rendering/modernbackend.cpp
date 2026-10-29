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

#include <cstddef> // offsetof (BondInstance::pickId in the pick pass)

#include "geometry.h"
#include "shaders.h"
#include <avogadro/primitive.h>

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
  // A shared widget's initializeGL can run with no current context (observed
  // for the second GLWidget created from MainWindow::newGLWidget): resolving
  // version functions then dereferences null inside Qt. Bail out cleanly and
  // let the caller fall back (isInitialized() == false), never crash.
  m_ctx = QOpenGLContext::currentContext();
  if (!m_ctx) {
    qWarning() << "ModernRendererBackend: no current GL context; cannot initialise";
    m_glInitialized = false;
    return;
  }
  m_glInitialized = m_gl.initializeOpenGLFunctions();
  if (!m_glInitialized) {
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
    // allocate() targets whatever is bound to GL_ARRAY_BUFFER; without an
    // explicit bind it silently no-ops (GL_INVALID_OPERATION) and the buffer
    // stays at size 0, so every subsequent write is dropped and the instanced
    // draw reads radius 0 => degenerate triangles => nothing renders.
    vbo.buffer.bind();
    vbo.buffer.allocate(bytes);
    vbo.buffer.release();
    vbo.capacityBytes = bytes;
    vbo.created = true;
  };
  alloc(m_sphereVBO, sphereBytes);
  alloc(m_bondVBO, bondBytes);
  // Lines are drawn as thin cylinders (the bond path), so they reserve their
  // own instance buffer in the BondInstance layout.
  alloc(m_lineVBO, bondBytes);

  // Count persistence (fix-modern-tier-frame-presentation): beginFrame() no
  // longer zeroes the per-primitive draw counts, so they survive across frames.
  // On a fresh or context-lost backend the VBOs are brand new and hold nothing,
  // so zero the counts here — nothing may draw until the first adopt pushes.
  m_sphereCount = 0;
  m_bondCount = 0;
  m_lineCount = 0;

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
    vbo.buffer.bind(); // allocate() needs an explicit bind; see alloc() above.
    vbo.buffer.allocate(bytes);
    vbo.buffer.release();
    vbo.capacityBytes = bytes;
    vbo.created = true;
  }
}

unsigned int ModernRendererBackend::pendingUploads() const {
  return m_sphereVBO.uploader.deferredCount() +
         m_bondVBO.uploader.deferredCount();
}

bool ModernRendererBackend::beginFrame(const Camera * /*camera*/,
                                       const QRect &viewport,
                                       int /*quality*/) {
  if (!m_ctx || !m_ctx->isValid() || !m_glInitialized)
    return false;
  m_ctx->makeCurrent(m_ctx->surface());
  // makeCurrent() resets the GL viewport to the (offscreen) surface size,
  // discarding whatever the caller set. Re-apply the viewport we were given,
  // otherwise every draw is clipped to a 1x1 region and renders nothing.
  if (viewport.isValid()) {
    m_gl.glViewport(viewport.x(), viewport.y(), viewport.width(),
                    viewport.height());
    m_frameWidth = viewport.width();
    m_frameHeight = viewport.height();
  }
  m_sphereVBO.uploader.beginFrame();
  m_bondVBO.uploader.beginFrame();
  m_lineVBO.uploader.beginFrame();
  m_uploadCalls = 0;
  m_maxChunkBytes = 0;
  // Retention contract (fix-modern-tier-frame-presentation): the per-primitive
  // draw counts are deliberately NOT reset here. They are owned by the last
  // push — pushSpheres / pushBonds set count = store.size(), pushLines sets 0
  // on an empty store — and the stale-instance watermark already lives at push
  // time, so a smaller scene after a larger one never draws stale garbage.
  // beginFrame() only resets per-frame upload bookkeeping and the draw-call
  // counter, so an endFrame() with no intervening push re-presents the
  // GPU-resident geometry the last push uploaded (the VBO contents are
  // unchanged by begin/end without pushes). Zeroing the counts here would
  // blank every in-flight frame, which is the flicker this change removes.
  // (Counts are zeroed in initialize() for a fresh / context-lost backend,
  // whose new VBOs hold nothing to draw.) The MVP is supplied separately via
  // setModelViewProjection(); without it drawInstances() is a no-op.
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

  // Restore GL_ARRAY_BUFFER (fix-modern-tier-pick-and-axes, design D3): the
  // upload bound the instance VBO to the current VAO to write into it. Leave
  // it bound and the default VAO's ARRAY_BUFFER_BINDING still points at this
  // instance VBO, so a later fixed-function / display-list draw (the axes
  // glyph's client-pointer cylinder) would misread a client pointer as a VBO
  // offset and emit GL_INVALID_OPERATION. Bind 0 so the binding is clean.
  m_gl.glBindBuffer(GL_ARRAY_BUFFER, 0);

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

void ModernRendererBackend::pushLines(const ChunkStore<LineInstance> &store) {
  // Lines have no dedicated shader in this tier: each segment is converted to
  // a thin cylinder (the bond instance layout) and drawn through the existing
  // cylinder geometry + program. That keeps the wireframe engines on the
  // instanced path without a second geometry/shader set, and the visual class
  // matches the legacy fixed-function line at the widths engines actually use.
  const std::size_t n = store.size();
  if (n == 0) {
    m_lineCount = 0;
    return;
  }
  ensureCapacity(m_lineVBO, n * sizeof(BondInstance));
  if (!m_lineVBO.created) {
    m_lineCount = 0;
    return;
  }

  ChunkStore<BondInstance> converted;
  converted.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const LineInstance &line = store.at(i);
    BondInstance bond;
    bond.start[0] = line.start[0];
    bond.start[1] = line.start[1];
    bond.start[2] = line.start[2];
    bond.end[0] = line.end[0];
    bond.end[1] = line.end[1];
    bond.end[2] = line.end[2];
    // width is the legacy GL line width in pixels; a 1 px line is about a
    // 0.02 A cylinder at typical molecular view scales.
    bond.radius = 0.02f * line.width;
    bond.color[0] = line.color[0];
    bond.color[1] = line.color[1];
    bond.color[2] = line.color[2];
    bond.color[3] = line.color[3];
    bond.order = 1;
    bond.shift = 0.0;
    converted.append(bond);
  }
  converted.markDirty(0, static_cast<unsigned int>(converted.size()));
  pushDirtyRanges(converted, m_lineVBO);
  fenceAfterUpload(m_lineVBO);
  m_lineCount = static_cast<unsigned int>(converted.size());
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

void ModernRendererBackend::buildPickPrograms()
{
  if (m_pickProgramsValid)
    return;
  auto build = [&](QOpenGLShaderProgram &program,
                   const QString &vertexSource) -> bool {
    program.removeAllShaders();
    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource))
      return false;
    if (!program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                         kPickFragmentShader))
      return false;
    if (!program.link())
      return false;
    program.bind();
    program.setUniformValue("uPickType", 0u);
    program.setUniformValue("uPickBase", 0u);
    program.release();
    return true;
  };
  m_pickProgramsValid = build(m_spherePickProgram, kSpherePickVertexShader) &&
                        build(m_cylinderPickProgram,
                              kCylinderPickVertexShader);
  if (!m_pickProgramsValid) {
    qWarning() << "ModernRendererBackend: pick programs failed to build:"
               << m_spherePickProgram.log() << m_cylinderPickProgram.log();
  }
}

void ModernRendererBackend::destroyPickResources()
{
  if (!m_ctx || !m_ctx->isValid())
    return;
  m_ctx->makeCurrent(m_ctx->surface());
  if (m_pickFbo) {
    m_gl.glDeleteFramebuffers(1, &m_pickFbo);
    m_pickFbo = 0;
  }
  if (m_pickColour) {
    m_gl.glDeleteTextures(1, &m_pickColour);
    m_pickColour = 0;
  }
  if (m_pickDepth) {
    m_gl.glDeleteRenderbuffers(1, &m_pickDepth);
    m_pickDepth = 0;
  }
  m_pickWidth = m_pickHeight = 0;
}

void ModernRendererBackend::ensurePickFbo(int width, int height)
{
  if (m_pickFbo && m_pickWidth == width && m_pickHeight == height)
    return;
  destroyPickResources();

  m_gl.glGenTextures(1, &m_pickColour);
  m_gl.glBindTexture(GL_TEXTURE_2D, m_pickColour);
  m_gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, nullptr);
  m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  m_gl.glGenRenderbuffers(1, &m_pickDepth);
  m_gl.glBindRenderbuffer(GL_RENDERBUFFER, m_pickDepth);
  m_gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width,
                             height);

  m_gl.glGenFramebuffers(1, &m_pickFbo);
  m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
  m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_pickColour, 0);
  m_gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_RENDERBUFFER, m_pickDepth);
  m_gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  m_pickWidth = width;
  m_pickHeight = height;
}

QList<GLHit> ModernRendererBackend::readPickHitsRect(int x, int y, int width, int height)
{
  QList<GLHit> hits;
  const int samples = width * height;
  if (samples <= 0)
    return hits;

  // The pick framebuffer is small (a cursor-sized rect or a rubber band), so a
  // single read is cheap; the PBO keeps the transfer asynchronous so the CPU
  // does not stall on the driver for the full-rect rubber-band pass.
  QVector<quint8> pixels(samples * 4);
  GLuint pbo = 0;
  m_gl.glGenBuffers(1, &pbo);
  m_gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
  m_gl.glBufferData(GL_PIXEL_PACK_BUFFER, pixels.size(), nullptr,
                    GL_STREAM_READ);
  m_gl.glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  quint8 *mapped = static_cast<quint8 *>(
      m_gl.glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
  if (mapped)
    memcpy(pixels.data(), mapped, pixels.size());
  m_gl.glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
  m_gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  m_gl.glDeleteBuffers(1, &pbo);

  for (int i = 0; i < samples; ++i) {
    const quint8 *px = pixels.data() + i * 4;
    const quint32 packed = static_cast<quint32>(px[0]) |
                           (static_cast<quint32>(px[1]) << 8) |
                           (static_cast<quint32>(px[2]) << 16) |
                           (static_cast<quint32>(px[3]) << 24);
    if (packed == 0xFFFFFFFFu)
      continue; // the clear sentinel: nothing was drawn into this pixel
    const quint32 type = packed >> 28;
    const quint32 name = packed & 0x0FFFFFFFu;
    const int col = i % width;
    const int row = i / width;
    GLfloat depth = 1.0f;
    m_gl.glReadPixels(x + col, y + row, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT,
                      &depth);
    const GLuint z = static_cast<GLuint>(depth * 2147483647.0f);
    hits.append(GLHit(type, name, z, z));
  }

  std::sort(hits.begin(), hits.end());
  return hits;
}

QList<GLHit> ModernRendererBackend::pick(int x, int y, int w, int h) {
  QList<GLHit> hits;
  if (!m_ctx || !m_ctx->isValid() || !m_glInitialized || !m_haveMvp)
    return hits;
  if (w <= 0 || h <= 0)
    return hits;
  if (!m_sphereGeom.valid && !m_cylinderGeom.valid)
    return hits;

  m_ctx->makeCurrent(m_ctx->surface());
  buildPickPrograms();
  if (!m_pickProgramsValid)
    return hits;
  // The pick pass renders at frame resolution with the frame's MVP (so the
  // geometry lands exactly where the visible frame put it) and uses the
  // scissor to confine rasterisation to the requested rectangle. Only that
  // rectangle is read back, so a cursor-sized pick stays cheap and a rubber
  // band costs no more than the pixels it covers.
  ensurePickFbo(m_frameWidth, m_frameHeight);

  GLint restoreFbo = 0;
  m_gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restoreFbo);
  m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
  m_gl.glViewport(0, 0, m_frameWidth, m_frameHeight);
  // All-ones can never be a valid (type << 28 | id) target, so it is an
  // unambiguous "nothing here" sentinel after the clear.
  m_gl.glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  m_gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  m_gl.glEnable(GL_DEPTH_TEST);
  m_gl.glDepthFunc(GL_LEQUAL);
  m_gl.glDisable(GL_BLEND);
  // Restore lighting at teardown (fix-modern-tier-pick-and-axes, design D7.2):
  // this pass is a known GL_LIGHTING disabler. Query the prior enable state
  // before clearing it so we can hand the context back exactly as we found it.
  // Without this, the disable leaks into the next fixed-function frame, where
  // material-based draws (the axes glyph) render unlit in the leftover raster
  // color — the white/yellow axes symptom.
  GLint priorLighting = GL_TRUE;
  m_gl.glGetIntegerv(GL_LIGHTING, &priorLighting);
  m_gl.glDisable(GL_LIGHTING);

  // glScissor's origin is bottom-left; the caller's y is top-down.
  const GLint sy = static_cast<GLint>(m_frameHeight) - (y + h);
  m_gl.glEnable(GL_SCISSOR_TEST);
  m_gl.glScissor(x, sy, w, h);

  struct PickPass
  {
    QOpenGLShaderProgram *program;
    PrimitiveGeometry *geometry;
    InstanceVBO *vbo;
    unsigned int count;
    std::size_t recordSize;
    quint32 type;
  };
  const PickPass passes[] = {
      {&m_spherePickProgram, &m_sphereGeom, &m_sphereVBO, m_sphereCount,
       sizeof(SphereInstance),
       static_cast<quint32>(Primitive::AtomType)},
      {&m_cylinderPickProgram, &m_cylinderGeom, &m_bondVBO, m_bondCount,
       sizeof(BondInstance),
       static_cast<quint32>(Primitive::BondType)},
  };

  for (const PickPass &pass : passes) {
    if (!pass.geometry->valid || pass.count == 0)
      continue;
    if (!pass.program->bind()) {
      qWarning() << "ModernRendererBackend: pick program bind failed";
      continue;
    }
    pass.program->setUniformValue("uMVP", m_mvp);
    // Legacy pick scene fattened click targets: bonds +0.05, atoms +0.03,
    // matching the hardcoded pick slop in BSDYEngine::renderPick (bonds
    // m_bondRadius+0.05, heavy atoms radius+0.03 at bsdyengine.cpp:416,426).
    // NOTE: SEL_BOND_EXTRA_RADIUS/SEL_ATOM_EXTRA_RADIUS (0.07/0.18) are the
    // selection-highlight extras and are unrelated to this pick slop. The
    // modern pick inflates every sphere by +0.03 (legacy additionally shrinks
    // hydrogens by -0.06), so hydrogen click targets are marginally larger on
    // the modern tier -- a deliberate, spec-sanctioned "more forgiving" choice.
    // Atoms are the sphere pass, bonds are the cylinder pass.
    pass.program->setUniformValue("uPickRadiusSlop",
                                  pass.type == static_cast<quint32>(Primitive::AtomType)
                                    ? 0.03f : 0.05f);
    // Uploaded explicitly: the unsigned-integer uniforms must go through
    // glUniform1ui, and Qt's name-based overload does not guarantee that.
    m_gl.glUniform1ui(pass.program->uniformLocation("uPickType"),
                      static_cast<GLuint>(pass.type));
    m_gl.glUniform1ui(pass.program->uniformLocation("uPickBase"), 0u);

    pass.geometry->vao.bind();
    pass.geometry->geometryVBO.bind();
    pass.geometry->indexVBO.bind();
    pass.program->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(GeometryVertex));
    pass.program->setAttributeBuffer(1, GL_FLOAT, 12, 3, sizeof(GeometryVertex));
    pass.vbo->buffer.bind();
    pass.program->setAttributeBuffer(2, GL_FLOAT, 0, 3, pass.recordSize);
    pass.program->setAttributeBuffer(3, GL_FLOAT, 12, 3, pass.recordSize);
    for (int loc = 0; loc <= 3; ++loc)
      m_gl.glEnableVertexAttribArray(loc);
    m_gl.glVertexAttribDivisor(2, 1);
    m_gl.glVertexAttribDivisor(3, 1);
    if (pass.type == static_cast<quint32>(Primitive::BondType)) {
      // Bond instances carry their bond's primitive index (BondInstance::pickId);
      // the cylinder pick shader resolves the hit name from it, so a click on
      // any half-segment resolves to the clicked bond, not the instance index.
      m_gl.glVertexAttribIPointer(6, 1, GL_UNSIGNED_INT,
                                  static_cast<GLsizei>(pass.recordSize),
                                  reinterpret_cast<const void *>(
                                    offsetof(BondInstance, pickId)));
      m_gl.glEnableVertexAttribArray(6);
      m_gl.glVertexAttribDivisor(6, 1);
    }

    m_gl.glDrawElementsInstanced(GL_TRIANGLES, pass.geometry->indexCount,
                                 GL_UNSIGNED_SHORT, nullptr,
                                 static_cast<GLsizei>(pass.count));
    pass.program->release();
    pass.geometry->vao.release();
  }
  m_gl.glDisable(GL_SCISSOR_TEST);

  m_gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pickFbo);
  hits = readPickHitsRect(x, sy, w, h);
  m_gl.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(restoreFbo));

  // Restore GL_ARRAY_BUFFER (fix-modern-tier-pick-and-axes, design D3): the
  // pick passes bound the geometry + instance VBOs inside their VAOs and left
  // the last instance VBO current. Unbind so a following fixed-function draw
  // (the axes glyph) does not misread a client pointer as a VBO offset.
  m_gl.glBindBuffer(GL_ARRAY_BUFFER, 0);

  // Restore the lighting enable state this pass cleared (design D7.2). The
  // axes overlay fix (glPushAttrib/glPopAttrib in renderAxesOverlay) makes the
  // glyph immune on its own, but honoring the restore contract here also
  // protects any other material-based fixed-function draw that runs later in
  // the same frame or the next one. No-op on a context where lighting is
  // already enabled.
  if (priorLighting == GL_TRUE) {
    GLint currentLighting = GL_FALSE;
    m_gl.glGetIntegerv(GL_LIGHTING, &currentLighting);
    if (currentLighting == GL_FALSE)
      m_gl.glEnable(GL_LIGHTING);
  }

  return hits;
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
    // allocate() writes to whatever is currently bound to the target; with the
    // VAO created (create() also binds it) and nothing bound to
    // GL_ARRAY_BUFFER the upload silently no-ops with GL_INVALID_OPERATION and
    // the VBO stays empty, so every vertex collapses onto aInstPos and the
    // instanced draw rasterises zero triangles. Bind explicitly first.
    m_sphereGeom.geometryVBO.bind();
    m_sphereGeom.geometryVBO.allocate(sphere.vertices().data(),
                                      sphere.vertices().size() * sizeof(GeometryVertex));
    m_sphereGeom.geometryVBO.release();
    // Index buffer (GL_ELEMENT_ARRAY_BUFFER): must be an IndexBuffer-type VBO,
    // else ELEMENT_ARRAY_BUFFER stays at 0 and glDrawElements uses a null
    // client-side index pointer (SIGSEGV in the driver). A plain member is
    // VertexBuffer-type, so build it locally with the right type and move it in.
    {
      QOpenGLBuffer indexVBO(QOpenGLBuffer::IndexBuffer);
      indexVBO.create();
      indexVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
      indexVBO.bind();
      indexVBO.allocate(sphere.indices().data(),
                        sphere.indices().size() * sizeof(unsigned short));
      indexVBO.release();
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
    m_sphereGeom.uMVloc = m_sphereGeom.program.uniformLocation("uModelView");
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
    // Same bind-before-allocate requirement as the sphere block above.
    m_cylinderGeom.geometryVBO.bind();
    m_cylinderGeom.geometryVBO.allocate(cylinder.vertices().data(),
                                        cylinder.vertices().size() * sizeof(GeometryVertex));
    m_cylinderGeom.geometryVBO.release();
    {
      QOpenGLBuffer indexVBO(QOpenGLBuffer::IndexBuffer);
      indexVBO.create();
      indexVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
      indexVBO.bind();
      indexVBO.allocate(cylinder.indices().data(),
                        cylinder.indices().size() * sizeof(unsigned short));
      indexVBO.release();
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
    m_cylinderGeom.uMVloc = m_cylinderGeom.program.uniformLocation("uModelView");
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

//! Mirror the legacy lighting (global.h: LIGHT_AMBIENT, LIGHT0_*, LIGHT1_*)
//! so the per-instance Phong lighting in the instanced fragment shaders produces
//! a lit, non-black result. The legacy GLPainter set these per draw; the backend
//! must set them explicitly because drawInstances() owns the draw.
static void setLightingUniforms(QOpenGLShaderProgram *program) {
  program->setUniformValue("uAmbient", 0.2f, 0.2f, 0.2f);
  program->setUniformValue("uLight0Diff", 1.0f, 1.0f, 1.0f);
  program->setUniformValue("uLight0Spec", 1.0f, 1.0f, 1.0f);
  program->setUniformValue("uLight0Dir", 0.8f, 0.7f, 1.0f);
  program->setUniformValue("uLight1Diff", 0.3f, 0.3f, 0.3f);
  program->setUniformValue("uLight1Spec", 0.5f, 0.5f, 0.5f);
  program->setUniformValue("uLight1Dir", -0.8f, 0.7f, -0.5f);
}

void ModernRendererBackend::drawInstances()
{
  if (!m_glInitialized || !m_haveMvp || !m_haveModelView)
    return; // missing a required matrix: a frame with a stale matrix must not
            // draw. Both the composed MVP and the modelview are required — the
            // lit sphere/cylinder shaders need both to shade correctly.

  // Drain any GL error left on the shared context by code outside the modern
  // tier's draw (e.g. shared context initialization). GL errors are sticky, so
  // a stale error from before this frame would otherwise be reported by the
  // post-cylinder check below and mislabeled as a cylinder-draw error. Draining
  // once here — after the early return and before any of the modern tier's own
  // draws — preserves detection of genuine errors set by the sphere/cylinder/
  // line draws that run later in this same frame.
  while (m_gl.glGetError() != GL_NO_ERROR)
    ;

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
    m_sphereGeom.program.setUniformValue(m_sphereGeom.uMVloc, m_modelView);
    setLightingUniforms(&m_sphereGeom.program);
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
    // Re-enable every array explicitly (the manual single-chunk render test
    // does this and renders; relying on VAO restore across the
    // buildGeometry/drawInstances split left the arrays disabled here and the
    // draw produced zero fragments). Explicit enable is the proven-safe path.
    m_gl.glEnableVertexAttribArray(0);
    m_gl.glEnableVertexAttribArray(1);
    m_gl.glEnableVertexAttribArray(2);
    m_gl.glEnableVertexAttribArray(3);
    m_gl.glEnableVertexAttribArray(4);
    m_gl.glVertexAttribDivisor(2, 1);
    m_gl.glVertexAttribDivisor(3, 1);
    m_gl.glVertexAttribDivisor(4, 1);
    m_gl.glDrawElementsInstanced(GL_TRIANGLES, m_sphereGeom.indexCount,
                                 GL_UNSIGNED_SHORT, nullptr,
                                 static_cast<GLsizei>(m_sphereCount));
    ++m_drawCallsThisFrame;
    m_sphereGeom.vao.release();
    // State restoration (fix-modern-tier-overlay-state-leak): release the
    // sphere program and disable the instance attributes (2-4) with their
    // divisors reset. Otherwise the program and enabled per-instance attributes
    // leak to the frame's later fixed-function draws (axes glyph, debug/extension
    // text, tool overlay), which then render through the stale molecule MVP and
    // the constant black per-instance color. The other bind sites in this file
    // (348/351, 622/648, 686/715) already follow this release discipline.
    m_sphereGeom.program.release();
    m_gl.glDisableVertexAttribArray(2);
    m_gl.glDisableVertexAttribArray(3);
    m_gl.glDisableVertexAttribArray(4);
    m_gl.glVertexAttribDivisor(2, 0);
    m_gl.glVertexAttribDivisor(3, 0);
    m_gl.glVertexAttribDivisor(4, 0);
  }

  // Lines are drawn through the cylinder path, so bonds and lines must be
  // considered together here.
  if (m_cylinderGeom.valid && (m_bondCount > 0 || m_lineCount > 0)) {
    if (!m_cylinderGeom.program.bind()) {
      qWarning() << "ModernRendererBackend: cylinder program bind failed";
      return;
    }
    m_cylinderGeom.program.setUniformValue(m_cylinderGeom.uMVPloc, m_mvp);
    m_cylinderGeom.program.setUniformValue(m_cylinderGeom.uMVloc, m_modelView);
    setLightingUniforms(&m_cylinderGeom.program);
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
    GLenum gerr = m_gl.glGetError();
    if (gerr != GL_NO_ERROR)
      qWarning() << "CYLINDER draw GL error:" << gerr;
    ++m_drawCallsThisFrame;

    // Lines (converted to thin cylinders, same instance layout) draw through
    // the very same cylinder VAO + program; only the instance buffer changes.
    if (m_lineCount > 0) {
      m_lineVBO.buffer.bind();
      m_cylinderGeom.program.setAttributeBuffer(2, GL_FLOAT, 0, 3,
                                                sizeof(BondInstance));
      m_cylinderGeom.program.setAttributeBuffer(3, GL_FLOAT, 12, 3,
                                                sizeof(BondInstance));
      m_cylinderGeom.program.setAttributeBuffer(4, GL_FLOAT, 24, 1,
                                                sizeof(BondInstance));
      m_cylinderGeom.program.setAttributeBuffer(5, GL_UNSIGNED_BYTE, 28, 4,
                                                sizeof(BondInstance));
      m_gl.glVertexAttribDivisor(2, 1);
      m_gl.glVertexAttribDivisor(3, 1);
      m_gl.glVertexAttribDivisor(4, 1);
      m_gl.glVertexAttribDivisor(5, 1);
      m_gl.glDrawElementsInstanced(GL_TRIANGLES, m_cylinderGeom.indexCount,
                                   GL_UNSIGNED_SHORT, nullptr,
                                   static_cast<GLsizei>(m_lineCount));
      ++m_drawCallsThisFrame;
    }
    m_cylinderGeom.vao.release();
    // State restoration (fix-modern-tier-overlay-state-leak): release the
    // cylinder program and disable the instance attributes (2-5) with their
    // divisors reset, matching the sphere block above. An empty scene never
    // binds here (the guards are before the bind), so the early-return path is
    // unchanged.
    m_cylinderGeom.program.release();
    m_gl.glDisableVertexAttribArray(2);
    m_gl.glDisableVertexAttribArray(3);
    m_gl.glDisableVertexAttribArray(4);
    m_gl.glDisableVertexAttribArray(5);
    m_gl.glVertexAttribDivisor(2, 0);
    m_gl.glVertexAttribDivisor(3, 0);
    m_gl.glVertexAttribDivisor(4, 0);
    m_gl.glVertexAttribDivisor(5, 0);
  }

  // Restore GL_ARRAY_BUFFER (fix-modern-tier-pick-and-axes, design D3):
  // the sphere and/or cylinder block above bound an instance VBO to
  // GL_ARRAY_BUFFER for the instanced draws and left it current. Run this
  // unconditionally at the end of the frame so the default VAO's
  // ARRAY_BUFFER_BINDING is 0 even for a sphere-only scene (atoms but no
  // bonds, which still renders the axes glyph), letting the axes display list
  // and the fixed-function overlays draw without misreading a client pointer
  // as a VBO offset. Spec: no modern-tier instance buffer remains bound after
  // the frame's draw work.
  m_gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
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
