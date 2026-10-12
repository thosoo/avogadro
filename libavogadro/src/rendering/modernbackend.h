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
#include <QPointer>
#include <QMatrix4x4>
#include <QtOpenGL/QOpenGLBuffer>
#include <QtOpenGL/QOpenGLFunctions_3_3_Core>
#include <QtOpenGL/QOpenGLShaderProgram>
#include <QtOpenGL/QOpenGLVertexArrayObject>
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

//! Shared per-primitive-type geometry for the modern tier: one indexed VAO
//! (interleaved position + normal) plus the shader program used to draw it
//! instanced. Built once in initialize() on the owning GL context; the
//! instance data it is drawn from lives in the matching InstanceVBO and is
//! re-uploaded (chunked, budgeted) each frame. A single VAO + one
//! glDrawElementsInstanced renders the whole scene of that primitive type.
struct PrimitiveGeometry {
  QOpenGLVertexArrayObject vao;
  QOpenGLBuffer geometryVBO;
  QOpenGLBuffer indexVBO; // GL_ELEMENT_ARRAY_BUFFER; IndexBuffer type is fixed at
                          // construction, so buildGeometry move-assigns it
  GLuint uMVPloc = 0;  // "uMVP" uniform location for this program
  QOpenGLShaderProgram program;
  int indexCount = 0; // triangles * 3 for the indexed draw
  bool valid = false;
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

  //! Whether initialize() succeeded on a live GL 3.3 context (shaders + geometry
  //! VAOs + instance VBOs created). The GLWidget uses this to fall back to the
  //! legacy tier if the modern backend could not be built, avoiding a black
  //! screen when the context is 3.3-capable but the backend failed.
  bool isInitialized() const { return m_glInitialized; }

  //! Largest single chunk uploaded this frame (bytes). Never exceeds
  //! kMaxChunkBytes.
  std::size_t maxChunkBytes() const { return m_maxChunkBytes; }

  //! Draw calls issued for the frame submitted in the most recent endFrame()
  //! (one instanced draw per primitive type that has instances). Bounded by
  //! construction: 2 for any scene size, reduced only by per-chunk culling.
  unsigned int drawCallsThisFrame() const { return m_drawCallsThisFrame; }

  //! Number of frames fully submitted (endFrame reached) since the last reset.
  unsigned int framesSubmitted() const { return m_framesSubmitted; }

  //! Model*projection matrix for the frame, set by the caller (GLWidget) from
  //! the camera before beginFrame. Stored as a shader uniform; without it the
  //! draw path is a no-op so a frame with a stale matrix never draws garbage.
  void setModelViewProjection(const QMatrix4x4 &mvp) { m_mvp = mvp; m_haveMvp = true; }

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

  //! Build the shared geometry VAOs and shader programs from geometry.h +
  //! shaders.h on the current GL context. Called from initialize(); rebuilt on
  //! context loss (initializeGL). Compiling the shaders replaces the
  //! ${LIGHTING} placeholder with the shared lighting preamble first.
  void buildGeometry();

  //! Draw the uploaded instance data for this frame: one instanced draw per
  //! primitive type that has instances, after uploads and fences complete.
  void drawInstances();

  //! Non-owning handle to the GL context the backend initialised on. Kept as
  //! a QPointer (not a raw pointer) so that if the owning context is deleted
  //! before the backend (e.g. in test teardown, or during widget destruction),
  //! it nulls out and the destructor can safely skip VBO cleanup rather than
  //! dereferencing freed memory. The VBOs are GL objects bound to that context,
  //! so when the context is gone they are gone too - nothing to leak.
  QPointer<QOpenGLContext> m_ctx;
  //! Pick pass: programs that write (type << 28 | id) instead of colour, plus
  //! a small RGBA8 + depth framebuffer sized to the pick rectangle.
  QOpenGLShaderProgram m_spherePickProgram;
  QOpenGLShaderProgram m_cylinderPickProgram;
  bool m_pickProgramsValid = false;
  GLuint m_pickFbo = 0;
  GLuint m_pickColour = 0;
  GLuint m_pickDepth = 0;
  int m_pickWidth = 0;
  int m_pickHeight = 0;
  void buildPickPrograms();
  void destroyPickResources();
  void ensurePickFbo(int width, int height);
  QList<GLHit> readPickHitsRect(int x, int y, int width, int height);

  InstanceVBO m_sphereVBO;
  InstanceVBO m_bondVBO;
  //! Lines rendered as thin cylinders (bond path), so the wire engine has a
  //! modern-tier representation without a second shader/geometry set.
  InstanceVBO m_lineVBO;
  PrimitiveGeometry m_sphereGeom;
  PrimitiveGeometry m_cylinderGeom;
  QMatrix4x4 m_mvp;
  bool m_haveMvp = false;
  unsigned int m_sphereCount = 0; // records drawn this frame (watermark)
  unsigned int m_bondCount = 0;
  unsigned int m_lineCount = 0;
  //! Frame dimensions from beginFrame; the pick pass needs them to map the
  //! pick rectangle back into clip space.
  int m_frameWidth = 0;
  int m_frameHeight = 0;
  unsigned int m_drawCallsThisFrame = 0;
  unsigned int m_framesSubmitted = 0;
  QOpenGLFunctions_3_3_Core m_gl;
  bool m_glInitialized = false;
  int m_uploadCalls = 0;
  std::size_t m_maxChunkBytes = 0;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_MODERNBACKEND_H
