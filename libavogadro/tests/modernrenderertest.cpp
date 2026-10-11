/**********************************************************************
  Single-chunk render test for the modern tier (gpu-rendering-pipeline task
  2.1).

  Two layers of verification:

    1. testShaderSources() - always runs, no OpenGL. Checks that the instanced
      sphere/bond shaders in shaders.h expose the attributes, uniforms and the
      two-light Blinn-Phong body the backend binds, so a shader-text regression
      is caught even on headless runners.

    2. testSingleChunkRender() - real rendering. Builds one sphere instance and
      one bond instance in GPU VAOs from the geometry generators + shaders,
      renders them once, and reads pixels back to confirm the atom renders as a
      coloured disc, the bond connects them, and the background stays clear.
      This needs a live GL 3.3+ context; on runners where one cannot be
      created (e.g. the offscreen Qt platform with no software/hardware GL) it
      QSKIPs with a diagnostic instead of failing, so it does real verification
      wherever GL is available (xcb + a display / Xvfb, or a real GPU).

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

#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QtOpenGL/QOpenGLFunctions_3_3_Core>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QtOpenGL/QOpenGLFramebufferObject>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QtTest>

#include <Eigen/Geometry>

#include "libavogadro/src/rendering/geometry.h"
#include "libavogadro/src/rendering/shaders.h"
#include "libavogadro/src/rendering/records.h"
#include "libavogadro/src/rendering/chunkstore.h"
#include "libavogadro/src/rendering/chunkeduploader.h"
#include "libavogadro/src/rendering/modernbackend.h"
#include "libavogadro/src/rendering/lod.h"
#include "libavogadro/src/rendering/frustum.h"

using Avogadro::rendering::CylinderGeometry;
using Avogadro::rendering::GeometryVertex;
using Avogadro::rendering::SphereGeometry;
using Avogadro::rendering::Frustum;
using Avogadro::rendering::LodLevel;
using Avogadro::rendering::ZoomLodController;

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 256;

// Clear colour (dark grey) so the background is easy to distinguish from lit
// geometry.
const float kBackground[3] = {0.05f, 0.05f, 0.05f};

//! Perspective model-view-projection. The camera sits at the origin looking
//! down -Z; translating the scene by -5 puts the atoms 5 units in front, well
//! inside the [0.1, 100] depth range. At that distance the frustum half-width
//! is ~2.1 world units, so atom 1 at the origin projects to screen centre and
//! the bond (0..1 on +X) projects to roughly pixel x=157 — matching the
//! pixel-location assertions below.
QMatrix4x4 viewProjection()
{
  QMatrix4x4 m;
  m.perspective(45.0f, static_cast<float>(kWidth) / kHeight, 0.1f, 100.0f);
  m.translate(0.0f, 0.0f, -5.0f);
  return m;
}

//! Try to obtain a current GL 3.3+ context on an offscreen GLX/ES surface.
//! Heap-allocates both objects (so they outlive the call) and returns them via
//! the out-params; the caller owns and must delete them. Returns nullptr and
//! leaves both out-params null when no usable context could be created (caller
//! should QSKIP). The context is left current on success.
QOpenGLContext *makeGLContext(QOffscreenSurface **off)
{
  QOpenGLContext *context = new QOpenGLContext();
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  context->setFormat(format);

  QOffscreenSurface *surface = new QOffscreenSurface();
  surface->setFormat(format);
  surface->create();

  if (!context->create() || !context->makeCurrent(surface))
  {
    context->makeCurrent(nullptr);
    delete surface;
    delete context;
    *off = nullptr;
    return nullptr;
  }

  *off = surface;
  return context;
}

//! Read one pixel as (r,g,b) in [0,1] from the back buffer.
void readPixel(QOpenGLFunctions *glf, int x, int y, float out[3])
{
  unsigned char px[3] = {0, 0, 0};
  glf->glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glf->glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);
  out[0] = px[0] / 255.0f;
  out[1] = px[1] / 255.0f;
  out[2] = px[2] / 255.0f;
}

//! Per-instance divisor, resolved via QOpenGLContext::getProcAddress (GL
//! 3.0+ core). The base QOpenGLFunctions does not expose getProcAddress.
void setDivisor(QOpenGLContext *ctx, GLuint location, GLuint divisor)
{
  typedef void (*PFNGLVERTEXATTRIBDIVISORPROC)(GLuint, GLuint);
  auto fn = reinterpret_cast<PFNGLVERTEXATTRIBDIVISORPROC>(
      ctx->getProcAddress("glVertexAttribDivisor"));
  if (fn)
    fn(location, divisor);
}

//! glDrawElementsInstanced (GL 3.1). The shared geometry is an indexed triangle
//! list, so the modern tier draws it indexed + instanced rather than as a
//! triangle strip.
void drawElementsInstanced(QOpenGLContext *ctx, GLenum mode, GLsizei count,
                           GLenum type, const void *offset,
                           GLsizei instancecount)
{
  typedef void (*PFNGLDRAWELEMENTSINSTANCEDPROC)(GLenum, GLsizei, GLenum,
                                                 const void *, GLsizei);
  auto fn = reinterpret_cast<PFNGLDRAWELEMENTSINSTANCEDPROC>(
      ctx->getProcAddress("glDrawElementsInstanced"));
  if (fn)
    fn(mode, count, type, offset, instancecount);
}

//! Upload the shared lighting uniforms that the fragment shaders' lighting
//! preamble consumes. Values mirror global.h (LIGHT_AMBIENT, LIGHT0_*,
//! LIGHT1_*) so the modern tier matches the legacy fixed-function lighting.
//! The light directions are passed un-normalised; the shaders normalise them
//! (L0 = normalize(uLight0Dir)), matching the preamble's own convention.
//!
//! These MUST be set: GLSL leaves an uninitialised uniform at (0,0,0), so an
//! unset uAmbient makes every fragment output exactly black.
void setLightingUniforms(QOpenGLShaderProgram *prog)
{
  prog->setUniformValue("uAmbient", 0.2f, 0.2f, 0.2f);
  prog->setUniformValue("uLight0Diff", 1.0f, 1.0f, 1.0f);
  prog->setUniformValue("uLight0Spec", 1.0f, 1.0f, 1.0f);
  prog->setUniformValue("uLight0Dir", 0.8f, 0.7f, 1.0f);
  prog->setUniformValue("uLight1Diff", 0.3f, 0.3f, 0.3f);
  prog->setUniformValue("uLight1Spec", 0.5f, 0.5f, 0.5f);
  prog->setUniformValue("uLight1Dir", -0.8f, 0.7f, -0.5f);
}

} // anonymous namespace

class ModernRenderTest : public QObject
{
  Q_OBJECT

  private Q_SLOTS:
    void testShaderSources();
    void testSingleChunkRender();
    void testChunkedUploaderBudget();
    void testUpload50kWithinBudget();
    void testDrawCallsBounded();

    // Task 2.3: LOD zoom bands + hysteresis and per-chunk AABB frustum culling.
    void testLodParams();
    void testOnScreenRadiusZoomInvariantUnderRotation();
    void testOnScreenRadiusZoomResponsive();
    void testLodChurnInvariantAtFixedZoom();
    void testLodHysteresis();
    void testFrustumCulling();
    void testChunkAABB();
};

void ModernRenderTest::testShaderSources()
{
  // Both shaders must be GLSL 3.30 with the per-instance attributes the
  // backend binds (geometry locs 0/1, instance locs 2+ and up).
  for (const QString &src : {Avogadro::rendering::kSphereVertexShader,
                             Avogadro::rendering::kSphereFragmentShader,
                             Avogadro::rendering::kCylinderVertexShader,
                             Avogadro::rendering::kCylinderFragmentShader})
  {
    QVERIFY(src.contains("#version 330"));
  }

  // Instance attribute locations required by the layout in instanceformat.h.
  for (const QString &src : {Avogadro::rendering::kSphereVertexShader,
                             Avogadro::rendering::kCylinderVertexShader})
  {
    QVERIFY(src.contains("location = 2")); // instance origin
  }
  QVERIFY(Avogadro::rendering::kSphereVertexShader.contains("aInstRadius"));
  QVERIFY(Avogadro::rendering::kCylinderVertexShader.contains("aInstStart"));
  QVERIFY(Avogadro::rendering::kCylinderVertexShader.contains("aInstEnd"));

  // Lighting parity: the two lights from global.h and the Phong material model
  // from glpainter_p.cpp (ambient = base/3, chroma-based specular, shininess
  // 50). These live in the shared preamble, which every fragment shader
  // embeds via the ${LIGHTING} placeholder.
  const QString &pre = Avogadro::rendering::kLightingPreamble;
  QVERIFY(pre.contains("uAmbient"));
  QVERIFY(pre.contains("uLight0Diff"));
  QVERIFY(pre.contains("uLight1Diff"));
  QVERIFY(pre.contains("base / 3.0"));
  QVERIFY(pre.contains("pow(ndoth, 50.0)"));

  // Both fragment shaders must pull in the lighting preamble and then shade
  // using the per-vertex normal and instance colour.
  for (const QString &frag : {Avogadro::rendering::kSphereFragmentShader,
                              Avogadro::rendering::kCylinderFragmentShader})
  {
    QVERIFY(frag.contains("${LIGHTING}"));
    QVERIFY(frag.contains("vNormal"));
    QVERIFY(frag.contains("shadeOne"));
  }
}

void ModernRenderTest::testSingleChunkRender()
{
  QOffscreenSurface *off = nullptr;
  QOpenGLContext *ctx = makeGLContext(&off);
  if (!ctx)
    QSKIP("No GL 3.3+ context available on this runner; skipping GPU render",
          SkipSingle);

  qWarning() << "OFFSCREEN SURFACE SIZE" << off->size();

  QOpenGLFunctions gl;
  gl.initializeOpenGLFunctions();
  // 3.3-core entry points (e.g. glVertexAttribDivisor) that the base
  // QOpenGLFunctions does not expose, and that getProcAddress failed to set
  // correctly on this driver.
  QOpenGLFunctions_3_3_Core gl33;
  gl33.initializeOpenGLFunctions();

  // The QOffscreenSurface's default framebuffer is 1x1, so we render into an
  // explicit 256x256 framebuffer object and read pixels back from it. A
  // depth renderbuffer is attached explicitly: it is required so the depth
  // test occludes the bond (drawn after the sphere, starting at the sphere's
  // centre) where the two overlap. The QOpenGLFramebufferObject::Depth
  // attachment did not yield a functional depth buffer on this driver, so we
  // build the FBO by hand.
  GLuint fboID = 0, colorTexID = 0, depthRBID = 0;
  gl.glGenFramebuffers(1, &fboID);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, fboID);
  gl.glGenTextures(1, &colorTexID);
  gl.glBindTexture(GL_TEXTURE_2D, colorTexID);
  gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kWidth, kHeight, 0, GL_RGB,
                  GL_UNSIGNED_BYTE, nullptr);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, colorTexID, 0);
  gl.glGenRenderbuffers(1, &depthRBID);
  gl.glBindRenderbuffer(GL_RENDERBUFFER, depthRBID);
  gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
                          kWidth, kHeight);
  gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_RENDERBUFFER, depthRBID);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  QVERIFY(fboID != 0);

  // --- compile the instanced shaders -------------------------------------
  auto compile = [&](QOpenGLShader::ShaderType type, const QString &src,
                     QString *log) -> QOpenGLShader * {
    QOpenGLShader *shader = new QOpenGLShader(type);
    QString expanded = src;
    expanded.replace("${LIGHTING}", Avogadro::rendering::kLightingPreamble);
    if (!shader->compileSourceCode(expanded))
    {
      log->append(shader->log());
      return nullptr;
    }
    return shader;
  };

  QString vlog, flog;
  QOpenGLShader *vsrc = compile(QOpenGLShader::Vertex,
                                Avogadro::rendering::kSphereVertexShader, &vlog);
  QOpenGLShader *fsrc = compile(QOpenGLShader::Fragment,
                                Avogadro::rendering::kSphereFragmentShader, &flog);
  QVERIFY2(vsrc != nullptr, vlog.toStdString().c_str());
  QVERIFY2(fsrc != nullptr, flog.toStdString().c_str());

  QOpenGLShaderProgram program;
  QVERIFY2(program.addShader(vsrc), vlog.toStdString().c_str());
  QVERIFY2(program.addShader(fsrc), flog.toStdString().c_str());
  QVERIFY2(program.link(), "program link failed");
  program.bind();
  const GLuint uMVP = program.uniformLocation("uMVP");
  QVERIFY(uMVP != GLuint(-1));

  // --- build the shared geometry VAOs ------------------------------------
  SphereGeometry sphere;
  sphere.setRings(16);
  sphere.setSegments(32);
  sphere.generate();
  CylinderGeometry cylinder;
  cylinder.setSegments(16);
  cylinder.generate();

  QOpenGLVertexArrayObject sphereVAO;
  sphereVAO.create();
  sphereVAO.bind();
  QOpenGLBuffer sphereVBO;
  sphereVBO.create();
  sphereVBO.bind();
  sphereVBO.allocate(sphere.vertices().data(),
                     sphere.vertices().size() * sizeof(GeometryVertex));
  gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                         sizeof(GeometryVertex), reinterpret_cast<void *>(0));
  gl.glEnableVertexAttribArray(0);
  gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                         sizeof(GeometryVertex), reinterpret_cast<void *>(12));
  gl.glEnableVertexAttribArray(1);
  // Indexed triangle list. The buffer MUST be an IndexBuffer (GL_ELEMENT_ARRAY
  // BUFFER); the default QOpenGLBuffer type is VertexBuffer (GL_ARRAY_BUFFER),
  // which would leave GL_ELEMENT_ARRAY_BUFFER at 0 and make glDrawElements use
  // a null client-side index pointer (SIGSEGV).
  QOpenGLBuffer sphereIndexVBO(QOpenGLBuffer::IndexBuffer);
  sphereIndexVBO.create();
  sphereIndexVBO.bind();
  sphereIndexVBO.allocate(sphere.indices().data(),
                          sphere.indices().size() * sizeof(unsigned short));
  sphereVAO.release();

  QOpenGLVertexArrayObject cylinderVAO;
  cylinderVAO.create();
  cylinderVAO.bind();
  QOpenGLBuffer cylinderVBO;
  cylinderVBO.create();
  cylinderVBO.bind();
  cylinderVBO.allocate(cylinder.vertices().data(),
                       cylinder.vertices().size() * sizeof(GeometryVertex));
  gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex),
                           reinterpret_cast<void *>(0));
  gl.glEnableVertexAttribArray(0);
  gl.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex),
                           reinterpret_cast<void *>(12));
  gl.glEnableVertexAttribArray(1);
  QOpenGLBuffer cylinderIndexVBO(QOpenGLBuffer::IndexBuffer);
  cylinderIndexVBO.create();
  cylinderIndexVBO.bind();
  cylinderIndexVBO.allocate(cylinder.indices().data(),
                            cylinder.indices().size() * sizeof(unsigned short));
  cylinderVAO.release();

  // --- one sphere instance + one bond instance ---------------------------
  struct Inst {
    float pos[3];
    float radius;
    unsigned char color[4];
  };
  Inst sphereInst{};
  sphereInst.pos[0] = 0.0f;
  sphereInst.pos[1] = 0.0f;
  sphereInst.pos[2] = 0.0f;
  sphereInst.radius = 0.3f;
  sphereInst.color[0] = 255;
  sphereInst.color[1] = 0;
  sphereInst.color[2] = 0;
  sphereInst.color[3] = 255;

  struct BondInst {
    float start[3];
    float end[3];
    float radius;
    unsigned char color[4];
  };
  BondInst bondInst{};
  memcpy(bondInst.start, sphereInst.pos, sizeof(float) * 3);
  bondInst.end[0] = 1.0f;
  bondInst.end[1] = 0.0f;
  bondInst.end[2] = 0.0f;
  bondInst.radius = 0.15f;
  bondInst.color[0] = 255;
  bondInst.color[1] = 255;
  bondInst.color[2] = 255;
  bondInst.color[3] = 255;

  QOpenGLBuffer instanceVBO;
  instanceVBO.create();
  instanceVBO.bind();
  instanceVBO.allocate(&sphereInst, sizeof(Inst)); // sphere first

  // --- draw the single chunk ---------------------------------------------
  gl.glBindFramebuffer(GL_FRAMEBUFFER, fboID);
  gl.glViewport(0, 0, kWidth, kHeight);
  gl.glClearColor(kBackground[0], kBackground[1], kBackground[2], 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  gl.glDepthFunc(GL_LEQUAL);

  const QMatrix4x4 mvp = viewProjection();

  // Sphere: geometry VAO (locs 0/1) + instance VBO (locs 2/3/4, instanced).
  // Bind the sphere program BEFORE configuring attribute buffers or setting
  // uniforms, since Qt applies those against the currently bound program.
  program.bind();
  // Sanity: assert a program is actually bound before drawing.
  GLuint boundProg = 0;
  gl.glGetIntegerv(GL_CURRENT_PROGRAM, reinterpret_cast<GLint*>(&boundProg));
  QVERIFY2(boundProg == program.programId(), "no shader program bound for sphere draw");

  // Geometry and instance data live in separate VBOs, so bind the geometry
  // VBO while configuring attribs 0/1, then bind the instance VBO for 2/3/4.
  sphereVAO.bind();
  sphereVBO.bind();
  program.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(GeometryVertex));
  gl.glEnableVertexAttribArray(0);
  program.setAttributeBuffer(1, GL_FLOAT, 12, 3, sizeof(GeometryVertex));
  gl.glEnableVertexAttribArray(1);
  instanceVBO.bind();
  program.setAttributeBuffer(2, GL_FLOAT, 0, 3, sizeof(Inst));
  program.setAttributeBuffer(3, GL_FLOAT, 12, 1, sizeof(Inst));
  program.setAttributeBuffer(4, GL_UNSIGNED_BYTE, 16, 4, sizeof(Inst));
  gl.glEnableVertexAttribArray(2);
  gl.glEnableVertexAttribArray(3);
  gl.glEnableVertexAttribArray(4);
  // Set per-instance divisors via the direct 3.3-core entry point. The
  // getProcAddress-based setDivisor left the divisor at 0 on this driver,
  // which made glDrawElementsInstanced read past the 1-element instance VBO
  // (GL_INVALID_OPERATION, no fragments).
  gl33.glVertexAttribDivisor(2, 1);
  gl33.glVertexAttribDivisor(3, 1);
  gl33.glVertexAttribDivisor(4, 1);
  setLightingUniforms(&program);
  // Use viewProjection (camera outside the sphere) so the sphere renders as a
  // normal disc and colour can be read at its centre.
  program.setUniformValue(uMVP, mvp);
  // The index buffer is bound to the VAO's ELEMENT_ARRAY_BUFFER target during
  // VAO setup above; draw the instanced sphere.
  drawElementsInstanced(ctx, GL_TRIANGLES, sphere.indexCount(),
                        GL_UNSIGNED_SHORT, nullptr, 1);
  instanceVBO.release();
  sphereVAO.release();
  gl.glFlush();

  // Bond: cylinder geometry VAO + a fresh instance buffer holding the bond.
  QOpenGLShaderProgram bprogram;
  QOpenGLShader *bcv =
    compile(QOpenGLShader::Vertex, Avogadro::rendering::kCylinderVertexShader, &vlog);
  QOpenGLShader *bcf =
    compile(QOpenGLShader::Fragment, Avogadro::rendering::kCylinderFragmentShader, &flog);
  QVERIFY2(bcv != nullptr, vlog.toStdString().c_str());
  QVERIFY2(bcf != nullptr, flog.toStdString().c_str());
  QVERIFY2(bprogram.addShader(bcv), vlog.toStdString().c_str());
  QVERIFY2(bprogram.addShader(bcf), flog.toStdString().c_str());
  QVERIFY2(bprogram.link(), "bond program link failed");
  bprogram.bind();
  const GLuint bMVP = bprogram.uniformLocation("uMVP");
  QVERIFY(bMVP != GLuint(-1));

  QOpenGLBuffer bondInstanceVBO;
  bondInstanceVBO.create();
  bondInstanceVBO.bind();
  bondInstanceVBO.allocate(&bondInst, sizeof(BondInst));
  cylinderVAO.bind();
  cylinderVBO.bind();
  bprogram.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(GeometryVertex));
  gl.glEnableVertexAttribArray(0);
  bprogram.setAttributeBuffer(1, GL_FLOAT, 12, 3, sizeof(GeometryVertex));
  gl.glEnableVertexAttribArray(1);
  bondInstanceVBO.bind();
  bprogram.setAttributeBuffer(2, GL_FLOAT, 0, 3, sizeof(BondInst));
  bprogram.setAttributeBuffer(3, GL_FLOAT, 12, 3, sizeof(BondInst));
  bprogram.setAttributeBuffer(4, GL_FLOAT, 24, 1, sizeof(BondInst));
  bprogram.setAttributeBuffer(5, GL_UNSIGNED_BYTE, 28, 4, sizeof(BondInst));
  gl.glEnableVertexAttribArray(2);
  gl.glEnableVertexAttribArray(3);
  gl.glEnableVertexAttribArray(4);
  gl.glEnableVertexAttribArray(5);
  setDivisor(ctx, 2, 1);
  setDivisor(ctx, 3, 1);
  setDivisor(ctx, 4, 1);
  setDivisor(ctx, 5, 1);
  setLightingUniforms(&bprogram);
  bprogram.setUniformValue(bMVP, mvp);
  // Draw the single white bond instance (origin -> (1,0,0)).
  drawElementsInstanced(ctx, GL_TRIANGLES, cylinder.indexCount(),
                        GL_UNSIGNED_SHORT, nullptr, 1);

  bprogram.release();
  cylinderVAO.release();
  bondInstanceVBO.release();

  gl.glFlush();

  // --- verify the rendered pixels ----------------------------------------
  // Atom 1 (red) is centred on screen.
  float center[3];
  readPixel(&gl, kWidth / 2, kHeight / 2, center);
  QVERIFY2(center[0] > 0.25, "atom centre must be lit red");
  QVERIFY(center[0] > center[1]);
  QVERIFY(center[0] > center[2]);

  // Background stays clear (below the lit threshold).
  float bg[3];
  readPixel(&gl, 10, 10, bg);
  QVERIFY2(bg[0] < 0.1, "background must stay clear");

  // The white bond between the atoms is lit (all channels non-trivial).
  float bondpx[3];
  readPixel(&gl, kWidth / 2 + 30, kHeight / 2, bondpx);
  QVERIFY2(bondpx[0] > 0.08 && bondpx[1] > 0.08 && bondpx[2] > 0.08,
           "bond must render lit");

  program.release();
  // Tear down the manual FBO.
  gl.glBindFramebuffer(GL_FRAMEBUFFER, fboID);
  if (depthRBID != 0)
    gl.glDeleteRenderbuffers(1, &depthRBID);
  if (colorTexID != 0)
    gl.glDeleteTextures(1, &colorTexID);
  if (fboID != 0)
    gl.glDeleteFramebuffers(1, &fboID);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  ctx->makeCurrent(nullptr);
  delete vsrc;
  delete fsrc;
  delete bcv;
  delete bcf;
  delete off;
  delete ctx;
}

// Headless: the ChunkedUploader must slice a large dirty set against a fixed
// per-frame budget and defer the overflow, while a small set fits whole.
void ModernRenderTest::testChunkedUploaderBudget()
{
  // Synthetic cost: 1 MB of instance data costs 1000 us of upload time.
  const Avogadro::rendering::ChunkedUploader::CostFn cost =
      [](std::size_t bytes) {
        return static_cast<double>(bytes) / (1u << 20) * 1000.0;
      };

  // Budget 2000 us accepts two 1-MB ranges; the rest are deferred.
  Avogadro::rendering::ChunkedUploader uploader(2000);
  std::vector<Avogadro::rendering::ChunkedUploader::Range> big;
  for (int i = 0; i < 5; ++i)
    big.push_back({static_cast<std::size_t>(i) * (1u << 20), 1u << 20});
  const int accepted = uploader.schedule(big, cost);
  QCOMPARE(accepted, 2);
  QCOMPARE(uploader.deferredCount(), 3);
  QCOMPARE(uploader.totalScheduled(), 5);
  QVERIFY(!uploader.fullyAccepted());

  // A single 1-MB range fits entirely under the 2000 us budget.
  Avogadro::rendering::ChunkedUploader small(2000);
  std::vector<Avogadro::rendering::ChunkedUploader::Range> one;
  one.push_back({0, 1u << 20});
  QCOMPARE(small.schedule(one, cost), 1);
  QVERIFY(small.fullyAccepted());
  QCOMPARE(small.deferredCount(), 0);

  // recordUploadMicros tracks the worst-case single upload, not the average.
  small.recordUploadMicros(500.0);
  small.recordUploadMicros(1500.0);
  small.recordUploadMicros(900.0);
  QCOMPARE(small.maxSingleUploadMicros(), 1500.0);
}

// Real GL: upload a 50k-atom store through the modern backend in bounded
// chunks and assert no single glBufferSubData exceeded the 2 ms budget.
void ModernRenderTest::testUpload50kWithinBudget()
{
  QOffscreenSurface *off = nullptr;
  QOpenGLContext *ctx = makeGLContext(&off);
  if (!ctx)
    QSKIP("No GL 3.3+ context available on this runner; skipping 50k upload",
          SkipSingle);

  Avogadro::rendering::ModernRendererBackend backend;
  // Pre-reserve capacity for 64k atoms / bonds so the 50k rebuild uploads
  // without a costly, un-budgeted VBO reallocation.
  backend.initialize(65536, 65536);

  // Build a 50k-atom store, every atom dirty.
  constexpr int kAtoms = 50000;
  Avogadro::rendering::ChunkStore<Avogadro::rendering::SphereInstance> store;
  store.reserve(kAtoms);
  for (int i = 0; i < kAtoms; ++i) {
    Avogadro::rendering::SphereInstance s;
    s.pos[0] = static_cast<float>(i);
    s.radius = 0.5f;
    s.color[0] = 255; s.color[1] = 128; s.color[2] = 0; s.color[3] = 255;
    store.append(s);
  }
  QCOMPARE(static_cast<int>(store.size()), kAtoms);

  // Upload in frames until the whole store is on the GPU. The default 2 ms
  // budget accepts the ~1 MB store in one frame (a real glBufferSubData of
  // that size is well under 2 ms); the loop still proves the mechanism scales
  // and defers correctly if a rebuild ever exceeds the budget.
  const int kMaxFrames = 64;
  int frames = 0;
  do {
    backend.beginFrame(nullptr, QRect(0, 0, kWidth, kHeight), 0);
    backend.pushSpheres(store);
    ++frames;
  } while (backend.pendingUploads() > 0 && frames < kMaxFrames);

  QVERIFY(frames < kMaxFrames);
  // Every atom was acknowledged (nothing left dirty).
  QCOMPARE(store.dirtyRanges().count(), 0);
  // The upload was chunked (multiple bounded glBufferSubData calls).
  QVERIFY(backend.uploadCallCount() >= 1);
  QVERIFY(backend.maxChunkBytes() <= Avogadro::rendering::ModernRendererBackend::kMaxChunkBytes);
  // The core 2.2 assertion: no single upload exceeded the 2 ms budget.
  QVERIFY2(backend.maxSingleUploadMicros() <= 2000.0,
           qPrintable(QString("max single upload %1 us exceeded 2 ms budget")
                          .arg(backend.maxSingleUploadMicros(), 0, 'f', 1)));

  ctx->makeCurrent(nullptr);
  delete off;
  delete ctx;
}

// Task 2.4: the modern backend issues a bounded number of instanced draws per
// frame — one per primitive type that has instances — independent of scene
// size. A static 1M-atom synthetic scene must still draw exactly 2 (sphere +
// cylinder), proving the draw count does not scale with the atom count.
void ModernRenderTest::testDrawCallsBounded()
{
  QOffscreenSurface *off = nullptr;
  QOpenGLContext *ctx = makeGLContext(&off);
  if (!ctx)
    QSKIP("No GL 3.3+ context available on this runner; skipping draw-count test",
          SkipSingle);

  Avogadro::rendering::ModernRendererBackend backend;
  // Pre-reserve for a 1M-atom scene so no VBO reallocation happens during the
  // upload (a reallocation would be an un-budgeted write, which is fine for the
  // count test but we want to exercise the reserve path).
  backend.initialize(1 << 20, 1 << 20);

  using SphereStore = Avogadro::rendering::ChunkStore<Avogadro::rendering::SphereInstance>;
  using BondStore = Avogadro::rendering::ChunkStore<Avogadro::rendering::BondInstance>;

  // 1) No MVP supplied: a frame with a stale matrix must draw nothing (the draw
  //    path is a no-op until an MVP is set, so a mis-ordered frame never draws
  //    stale instances).
  backend.beginFrame(nullptr, QRect(0, 0, kWidth, kHeight), 0);
  {
    SphereStore spheres;
    spheres.reserve(1024);
    for (int i = 0; i < 1024; ++i) {
      Avogadro::rendering::SphereInstance s;
      s.pos[0] = 0; s.pos[1] = 0; s.pos[2] = 0; s.radius = 0.5f;
      s.color[0] = 255; s.color[1] = 0; s.color[2] = 0; s.color[3] = 255;
      spheres.append(s);
    }
    backend.pushSpheres(spheres);
    backend.endFrame();
  }
  QCOMPARE(static_cast<int>(backend.drawCallsThisFrame()), 0);

  // 2) A 1M-atom synthetic scene WITH an MVP: exactly 2 instanced draws, one
  //    per primitive type, regardless of the atom count.
  backend.beginFrame(nullptr, QRect(0, 0, kWidth, kHeight), 0);
  backend.setModelViewProjection(viewProjection());
  {
    SphereStore spheres;
    spheres.reserve(1 << 20);
    for (int i = 0; i < (1 << 20); ++i) {
      Avogadro::rendering::SphereInstance s;
      s.pos[0] = static_cast<float>(i); s.pos[1] = 0; s.pos[2] = 0;
      s.radius = 0.5f;
      s.color[0] = 255; s.color[1] = 128; s.color[2] = 0; s.color[3] = 255;
      spheres.append(s);
    }
    BondStore bonds;
    bonds.reserve(1 << 20);
    for (int i = 0; i < (1 << 20); ++i) {
      Avogadro::rendering::BondInstance b;
      b.start[0] = 0; b.start[1] = 0; b.start[2] = 0;
      b.end[0] = 1; b.end[1] = 0; b.end[2] = 0;
      b.radius = 0.2f;
      b.color[0] = 200; b.color[1] = 200; b.color[2] = 200; b.color[3] = 255;
      b.order = 0; b.shift = 0;
      bonds.append(b);
    }
    backend.pushSpheres(spheres);
    backend.pushBonds(bonds);
    backend.endFrame();
  }
  QCOMPARE(static_cast<int>(backend.drawCallsThisFrame()), 2);
  QCOMPARE(static_cast<int>(backend.framesSubmitted()), 2);

  ctx->makeCurrent(nullptr);
  delete off;
  delete ctx;
}

// --- Task 2.3: LOD + frustum culling (headless; no GL needed) --------------

// lodParams(): detail levels must be strictly coarser-to-finer, Medium must
// preserve the pre-LOD default (12/24) for visual parity, and every generated
// level must stay under the unsigned-short index-buffer vertex ceiling.
void ModernRenderTest::testLodParams()
{
  using Avogadro::rendering::SphereGeometry;
  using Avogadro::rendering::LodParams;

  const LodParams low = Avogadro::rendering::lodParams(Avogadro::rendering::LodLow);
  const LodParams med = Avogadro::rendering::lodParams(Avogadro::rendering::LodMedium);
  const LodParams high = Avogadro::rendering::lodParams(Avogadro::rendering::LodHigh);

  QVERIFY(low.sphereRings < med.sphereRings);
  QVERIFY(low.sphereSegments < med.sphereSegments);
  QVERIFY(med.sphereRings < high.sphereRings);
  QVERIFY(med.sphereSegments < high.sphereSegments);
  // Medium preserves the pre-LOD default for visual parity.
  QCOMPARE(med.sphereRings, 12);
  QCOMPARE(med.sphereSegments, 24);

  SphereGeometry sl, sm, sh;
  sl.setRings(low.sphereRings); sl.setSegments(low.sphereSegments); sl.generate();
  sm.setRings(med.sphereRings); sm.setSegments(med.sphereSegments); sm.generate();
  sh.setRings(high.sphereRings); sh.setSegments(high.sphereSegments); sh.generate();

  QVERIFY(sh.indexCount() > sm.indexCount());
  QVERIFY(sm.indexCount() > sl.indexCount());
  // Index buffer is unsigned short: stay under 65536 vertices.
  // Index values reference vertex slots, so the vertex count is what must fit
  // in the unsigned-short index buffer.
  QVERIFY(sh.vertexCount() < 65536);
}

// Build a perspective projection matrix (gluPerspective form) for tests that
// need a real P(1,1) and P(3,3) == 0.
static Eigen::Matrix4f makePerspective(float fovYDeg, float aspect,
                                       float nearPlane, float farPlane)
{
  Eigen::Matrix4f p = Eigen::Matrix4f::Zero();
  const float f = 1.0f / std::tan(fovYDeg * 0.5f * static_cast<float>(M_PI) / 180.0f);
  p(0, 0) = f / aspect;
  p(1, 1) = f;
  p(2, 2) = -(farPlane + nearPlane) / (farPlane - nearPlane);
  p(2, 3) = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);
  p(3, 3) = 0.0f;
  return p;
}

// onScreenRadius must be invariant under orbiting the camera about the focus
// point (a pure rotation changes only the rotation part: s stays 1, the focus
// depth and P are unchanged). This is the "rotation at fixed zoom" that must
// NOT change the LOD.
void ModernRenderTest::testOnScreenRadiusZoomInvariantUnderRotation()
{
  using Avogadro::rendering::onScreenRadius;
  const float d = 5.0f;      // eye-space depth of the (origin) focus
  const float H = 256.0f;
  const float aspect = 1.0f;
  const Eigen::Vector3f focus(0.0f, 0.0f, 0.0f);
  const float refRadius = 1.0f;

  Eigen::Matrix4f p = makePerspective(40.0f, aspect, 0.1f, 100.0f);

  // modelview = rotation about Z (any angle) then translate the scene -d.
  auto buildMv = [&](float angleDeg) {
    Eigen::AngleAxisf aa(angleDeg * static_cast<float>(M_PI) / 180.0f,
                         Eigen::Vector3f::UnitZ());
    Eigen::Transform<float, 3, Eigen::Affine> t;
    t.linear() = aa.toRotationMatrix();
    t.translation() = Eigen::Vector3f(0.0f, 0.0f, -d);
    return t.matrix();
  };

  const float rho0 = onScreenRadius(buildMv(0.0f), p, H, focus, refRadius);
  const float rho90 = onScreenRadius(buildMv(90.0f), p, H, focus, refRadius);
  const float rho137 = onScreenRadius(buildMv(137.0f), p, H, focus, refRadius);

  QCOMPARE(rho0, rho90);
  QCOMPARE(rho0, rho137);
}

// onScreenRadius must respond to genuine zoom. This app zooms by scaling the
// modelview linear part (Camera::scale), so doubling that scale must double
// rho. This is the regression guard against the zoom-blind 1/distance metric.
void ModernRenderTest::testOnScreenRadiusZoomResponsive()
{
  using Avogadro::rendering::onScreenRadius;
  const float d = 5.0f;
  const float H = 256.0f;
  const Eigen::Vector3f focus(0.0f, 0.0f, 0.0f);
  const float refRadius = 1.0f;
  Eigen::Matrix4f p = makePerspective(40.0f, 1.0f, 0.1f, 100.0f);

  auto buildScaledMv = [&](float s) {
    Eigen::Transform<float, 3, Eigen::Affine> t;
    t.linear() = s * Eigen::Matrix3f::Identity();
    t.translation() = Eigen::Vector3f(0.0f, 0.0f, -d);
    return t.matrix();
  };

  const float rhoFar = onScreenRadius(buildScaledMv(1.0f), p, H, focus, refRadius);
  const float rhoNear = onScreenRadius(buildScaledMv(2.0f), p, H, focus, refRadius);

  // rho scales linearly with the modelview scale factor.
  QCOMPARE(rhoNear / rhoFar, 2.0f);
}

// At a constant on-screen radius (fixed zoom), selecting the LOD every frame
// must never churn: the Schmitt trigger is a fixed point once primed.
void ModernRenderTest::testLodChurnInvariantAtFixedZoom()
{
  ZoomLodController ctrl(4.0f, 16.0f, 1.3f);
  ctrl.reset();
  // Prime at Low with the initial (non-churning) selection.
  ctrl.select(1.0f);
  QCOMPARE(ctrl.churnCount(), 0);
  // 120 "rotation frames" at the same zoom: no LOD change, no churn.
  for (int i = 0; i < 120; ++i)
    ctrl.select(1.0f);
  QCOMPARE(ctrl.churnCount(), 0);
  QCOMPARE(ctrl.level(), Avogadro::rendering::LodLow);
}

// Schmitt-trigger hysteresis: crossing a boundary up counts one churn, wiggling
// inside the dead band counts zero, and falling back down counts one more.
void ModernRenderTest::testLodHysteresis()
{
  ZoomLodController ctrl(4.0f, 16.0f, 1.3f); // t01=4, t12=16, g=1.3
  ctrl.reset();
  ctrl.select(1.0f); // initial selection -> Low, no churn
  QCOMPARE(ctrl.churnCount(), 0);
  QCOMPARE(ctrl.level(), Avogadro::rendering::LodLow);

  // Inside the low->medium dead band [t01/g=3.08, t01*g=5.2]: no transition.
  ctrl.select(4.0f);
  QCOMPARE(ctrl.level(), Avogadro::rendering::LodLow);
  QCOMPARE(ctrl.churnCount(), 0);

  // Cross up to Medium (>= 5.2): exactly one churn.
  ctrl.select(5.2f);
  QCOMPARE(ctrl.level(), Avogadro::rendering::LodMedium);
  QCOMPARE(ctrl.churnCount(), 1);

  // Inside the medium->high dead band [t12/g=12.3, t12*g=20.8]: no transition.
  ctrl.select(16.0f);
  QCOMPARE(ctrl.level(), Avogadro::rendering::LodMedium);
  QCOMPARE(ctrl.churnCount(), 1);

  // Fall below t01/g (3.08) back to Low: one more churn.
  ctrl.select(3.0f);
  QCOMPARE(ctrl.level(), Avogadro::rendering::LodLow);
  QCOMPARE(ctrl.churnCount(), 2);
}

// Per-chunk AABB-vs-frustum culling: a box around the origin is visible in the
// identity (standard) clip volume; a box beyond the far plane is culled; a box
// straddling a side plane is kept (the conservative test must never drop a
// visible chunk); a box fully outside is culled.
void ModernRenderTest::testFrustumCulling()
{
  const Frustum f(Eigen::Matrix4f::Identity());

  // Box around the origin is inside [-1,1]^3.
  Eigen::Vector3f mn(-0.5f, -0.5f, -0.5f), mx(0.5f, 0.5f, 0.5f);
  QVERIFY(Avogadro::rendering::aabbVisible(mn, mx, f));

  // Box beyond the far plane (z in [5,6]) is culled.
  Eigen::Vector3f behindMN(0.0f, 0.0f, 5.0f), behindMX(1.0f, 1.0f, 6.0f);
  QVERIFY(!Avogadro::rendering::aabbVisible(behindMN, behindMX, f));

  // Box straddling the left plane (x in [-2,-0.5]) is still visible.
  Eigen::Vector3f addMN(-2.0f, -0.5f, -0.5f), addMX(-0.5f, 0.5f, 0.5f);
  QVERIFY(Avogadro::rendering::aabbVisible(addMN, addMX, f));

  // Box fully left of the left plane (x in [-3,-2]) is culled.
  Eigen::Vector3f outMN(-3.0f, -0.5f, -0.5f), outMX(-2.0f, -0.2f, -0.2f);
  QVERIFY(!Avogadro::rendering::aabbVisible(outMN, outMX, f));

  // cullChunks returns the visible indices in order.
  std::vector<Eigen::Vector3f> mins = {
      Eigen::Vector3f(-0.5f, -0.5f, -0.5f),  // 0 visible
      Eigen::Vector3f(5.0f, 5.0f, 5.0f),     // 1 culled (far)
      Eigen::Vector3f(-2.0f, -0.5f, -0.5f),  // 2 visible (straddle)
  };
  std::vector<Eigen::Vector3f> maxs = {
      Eigen::Vector3f(0.5f, 0.5f, 0.5f),
      Eigen::Vector3f(6.0f, 6.0f, 6.0f),
      Eigen::Vector3f(-0.5f, 0.5f, 0.5f),
  };
  const auto visible = Avogadro::rendering::cullChunks(mins, maxs, f);
  QCOMPARE(static_cast<int>(visible.size()), 2);
  QCOMPARE(visible[0], 0);
  QCOMPARE(visible[1], 2);
}

// buildSphereAABB expands each record's centre by its radius per axis, and a
// per-chunk AABB set culls correctly (on-screen kept, off-screen dropped).
void ModernRenderTest::testChunkAABB()
{
  using Avogadro::rendering::SphereInstance;

  std::vector<SphereInstance> recs;
  SphereInstance a;
  a.pos[0] = 0.0f; a.pos[1] = 0.0f; a.pos[2] = 0.0f; a.radius = 0.5f;
  recs.push_back(a);
  SphereInstance b;
  b.pos[0] = 100.0f; b.pos[1] = 2.0f; b.pos[2] = -3.0f; b.radius = 1.5f;
  recs.push_back(b);

  Eigen::Vector3f mn, mx;
  Avogadro::rendering::buildSphereAABB(recs.data(), static_cast<int>(recs.size()), mn, mx);
  // Per-axis min/max over both records (a: r=0.5 at origin, b: r=1.5 at 100,2,-3).
  QCOMPARE(mn.x(), -0.5f);
  QCOMPARE(mn.y(), -0.5f);
  QCOMPARE(mn.z(), -4.5f);
  QCOMPARE(mx.x(), 101.5f);
  QCOMPARE(mx.y(), 3.5f);
  QCOMPARE(mx.z(), 0.5f);

  // Two per-chunk AABBs: chunk 0 near origin (visible), chunk 1 far off +X
  // (culled against the identity clip volume).
  std::vector<Eigen::Vector3f> mins = {
      Eigen::Vector3f(-0.5f, -0.5f, -0.5f),
      Eigen::Vector3f(98.0f, 0.5f, -4.5f),
  };
  std::vector<Eigen::Vector3f> maxs = {
      Eigen::Vector3f(0.5f, 0.5f, 0.5f),
      Eigen::Vector3f(102.0f, 3.5f, -1.5f),
  };
  const Frustum f(Eigen::Matrix4f::Identity());
  const auto visible = Avogadro::rendering::cullChunks(mins, maxs, f);
  QCOMPARE(static_cast<int>(visible.size()), 1);
  QCOMPARE(visible[0], 0);
}

QTEST_MAIN(ModernRenderTest)
#include "modernrenderertest.moc"
