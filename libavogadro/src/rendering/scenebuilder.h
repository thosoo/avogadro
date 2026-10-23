/**********************************************************************
  SceneBuilder - tier-agnostic emitter that turns Painter draws into chunks.

  SceneBuilder is a Painter implementation whose draw calls do not issue GL:
  they append per-instance records to the tier-agnostic ChunkStores defined in
  the scene core. The GLWidget paints engines through a SceneBuilder (by
  redirecting PainterDevice::painter() to it), then hands the filled stores to
  the active RendererBackend via pushChunks. This is the engine-migration seam
  (design D4): porting an engine is mechanical because the signatures are the
  exact Painter signatures.

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

#ifndef AVOGADRO_RENDERING_SCENEBUILDER_H
#define AVOGADRO_RENDERING_SCENEBUILDER_H

#include <QColor>
#include <QVector>
#include <QString>

#include <Eigen/Core>

#include <avogadro/painter.h>

#include "chunkstore.h"
#include "records.h"

namespace Avogadro {
namespace rendering {

//! A single captured text primitive (engine labels, measurements, annotations,
//! overlay HUD text). Recorded by SceneBuilder::drawText instead of being
//! discarded; GLWidget flushes the store through the real painter after the
//! tier 3D pass so text renders on every tier (fix-modern-tier-overlay-parity).
struct TextItem {
  QString text;
  QColor color;
  bool worldSpace = false;
  Eigen::Vector3d worldPos;
  int x = 0;
  int y = 0;
};

class A_EXPORT SceneBuilder : public Painter {
public:
  SceneBuilder();
  ~SceneBuilder() override;

  // Painter interface. The draw methods append to the chunk stores instead of
  // issuing GL calls.
  int quality() const override;
  void setQuality(int quality);

  void setName(const Primitive *primitive) override;
  void setName(Primitive::Type type, int id) override;

  void setColor(const Color *color) override;
  void setColor(const QColor *color) override;
  void setColor(float red, float green, float blue, float alpha = 1.0) override;
  void setColor(QString name) override;

  void drawSphere(const Eigen::Vector3d &center, double radius) override;
  // drawSphere(const Eigen::Vector3d*, double) is inherited from Painter.
  void drawCylinder(const Eigen::Vector3d &end1, const Eigen::Vector3d &end2,
                    double radius) override;
  void drawMultiCylinder(const Eigen::Vector3d &end1, const Eigen::Vector3d &end2,
                         double radius, int order, double shift) override;
  void drawLine(const Eigen::Vector3d &start, const Eigen::Vector3d &end,
                double lineWidth) override;
  void drawMultiLine(const Eigen::Vector3d &start, const Eigen::Vector3d &end,
                     double lineWidth, int order, short stipple) override;
  void drawMesh(const Mesh &mesh, int mode = 0) override;
  void drawColorMesh(const Mesh &mesh, int mode = 0) override;

  // The remaining Painter primitives are not part of the core molecular path
  // but are captured as line/stroke geometry so nothing is silently dropped.
  void drawCone(const Eigen::Vector3d &base, const Eigen::Vector3d &cap,
                double baseRadius, double capRadius = 0.0) override;
  void drawTriangle(const Eigen::Vector3d &p1, const Eigen::Vector3d &p2,
                    const Eigen::Vector3d &p3) override;
  void drawTriangle(const Eigen::Vector3d &p1, const Eigen::Vector3d &p2,
                    const Eigen::Vector3d &p3, const Eigen::Vector3d &n) override;
  void drawShadedQuadrilateral(const Eigen::Vector3d &p1,
                               const Eigen::Vector3d &p2,
                               const Eigen::Vector3d &p3,
                               const Eigen::Vector3d &p4) override;
  void drawShadedSector(const Eigen::Vector3d &origin,
                        const Eigen::Vector3d &direction1,
                        const Eigen::Vector3d &direction2, double radius,
                        bool alternateAngle = false) override;
  void drawArc(const Eigen::Vector3d &origin, const Eigen::Vector3d &d1,
               const Eigen::Vector3d &d2, double radius, double lineWidth,
               bool alternateAngle = false) override;
  void drawSpline(const QVector<Eigen::Vector3d> &pts, double radius) override;
  void drawBox(const Eigen::Vector3d &corner1,
               const Eigen::Vector3d &corner2) override;
  void drawTorus(const Eigen::Vector3d &pos, double majorRadius,
                 double minorRadius) override;
  void drawEllipsoid(const Eigen::Vector3d &position,
                     const Eigen::Matrix3d &matrix) override;

  int drawText(int x, int y, const QString &string) override;
  int drawText(const QPoint &pos, const QString &string) override;
  int drawText(const Eigen::Vector3d &pos, const QString &string) override;

  //! Reset per-draw state (current color / name) at the start of a pass.
  void begin();
  void end();

  //! Empty all chunk stores. Call once per frame before painting engines.
  void clearStores();

  //! Whether per-primitive dynamic scaling is active (mirrors the Painter).
  bool dynamicScaling() const { return m_dynamicScaling; }
  void setDynamicScaling(bool scaling) { m_dynamicScaling = scaling; }

  // Filled chunk stores for this frame.
  ChunkStore<SphereInstance> &spheres() { return m_spheres; }
  ChunkStore<BondInstance> &bonds() { return m_bonds; }
  ChunkStore<LineInstance> &lines() { return m_lines; }
  ChunkStore<MeshInstance> &meshes() { return m_meshes; }

  const ChunkStore<SphereInstance> &spheres() const { return m_spheres; }
  const ChunkStore<BondInstance> &bonds() const { return m_bonds; }
  const ChunkStore<LineInstance> &lines() const { return m_lines; }
  const ChunkStore<MeshInstance> &meshes() const { return m_meshes; }

  //! Captured text items for this frame, in draw order. Flushed by GLWidget
  //! after the tier 3D pass; cleared in begin()/clearStores().
  const QVector<TextItem> &textItems() const { return m_textItems; }

  //! Packed (type << 28 | id) for record `index` of each primitive kind.
  quint32 spherePickId(std::size_t index) const;
  quint32 bondPickId(std::size_t index) const;
  quint32 linePickId(std::size_t index) const;

private:
  void appendBond(const Eigen::Vector3d &end1, const Eigen::Vector3d &end2,
                  double radius, int order, double shift);
  void appendLine(const Eigen::Vector3d &start, const Eigen::Vector3d &end,
                  double width, int order = 1, short stipple = 0);

  ChunkStore<SphereInstance> m_spheres;
  ChunkStore<BondInstance> m_bonds;
  ChunkStore<LineInstance> m_lines;
  ChunkStore<MeshInstance> m_meshes;

  //! Packed pick target per emitted record, in record order. Encoded as
  //! (type << 28) | id so the picking pass can resolve a pixel back to the
  //! primitive that produced it without a second name-stack pass.
  QVector<quint32> m_spherePickIds;
  QVector<quint32> m_bondPickIds;
  QVector<quint32> m_linePickIds;

  //! Captured text primitives for this frame (see TextItem / textItems()).
  QVector<TextItem> m_textItems;

  int m_quality;
  bool m_dynamicScaling;
  QColor m_currentColor;
  Primitive::Type m_currentType;
  int m_currentId;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_SCENEBUILDER_H
