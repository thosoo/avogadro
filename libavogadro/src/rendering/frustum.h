/**********************************************************************
  Frustum culling primitives for the modern rendering tier (gpu-rendering-
  pipeline task 2.3).

  Pure CPU linear algebra (Eigen only, no OpenGL) so the frustum extraction and
  the axis-aligned bounding-box test can be unit-tested headlessly. This is the
  per-chunk AABB-vs-frustum culling the design calls for (D2): "per-chunk AABB
  vs frustum on the CPU (fine at capsid scale)".

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

#ifndef AVOGADRO_RENDERING_FRUSTUM_H
#define AVOGADRO_RENDERING_FRUSTUM_H

#include <Eigen/Geometry>

#include <vector>

namespace Avogadro {
namespace rendering {

//! A single clipping plane in world space. The interior is n.x + d >= 0.
//! Planes are normalised so ||n|| == 1.
struct FrustumPlane {
  Eigen::Vector3f n;
  float d = 0.0f;
};

//! The six-plane view clip frustum (left, right, bottom, top, near, far),
//! extracted from a model*projection matrix with the Gribb-Hartmann row
//! subtraction. Interior of each plane is (n.x + d) >= 0.
//!
//! Header-only: the constructor is a template so callers may pass either an
//! Eigen::Matrix4f or a Matrix4d (e.g. from Eigen::Projective3d::matrix()).
class Frustum {
public:
  static constexpr int kPlaneCount = 6;

  template <typename MatrixType>
  explicit Frustum(const MatrixType &mvp)
  {
    const Eigen::Matrix4f M = mvp.template cast<float>();
    const Eigen::Vector4f r0 = M.row(0);
    const Eigen::Vector4f r1 = M.row(1);
    const Eigen::Vector4f r2 = M.row(2);
    const Eigen::Vector4f r3 = M.row(3);

    // clip.x <= w  <=>  (r3 - r0).x >= 0, etc. Interior is the +side.
    const Eigen::Vector4f raw[kPlaneCount] = {
        r3 + r0,  // left
        r3 - r0,  // right
        r3 + r1,  // bottom
        r3 - r1,  // top
        r3 + r2,  // near
        r3 - r2,  // far
    };
    m_planeCount = kPlaneCount;
    for (int i = 0; i < kPlaneCount; ++i) {
      Eigen::Vector3f n = raw[i].head<3>();
      const float denom = n.norm();
      if (denom < 1e-8f) {
        // Degenerate plane (should not happen for a finite frustum); drop it
        // rather than dividing by ~0. The box is then judged on the rest.
        m_planeCount = i;
        break;
      }
      m_planes[i].n = n / denom;
      m_planes[i].d = raw[i][3] / denom;
    }
  }

  const FrustumPlane &plane(int i) const { return m_planes[i]; }
  int planeCount() const { return m_planeCount; }

private:
  FrustumPlane m_planes[kPlaneCount];
  int m_planeCount = kPlaneCount;
};

//! Most-positive-vertex AABB-vs-frustum test. Returns true if any part of the
//! box [min, max] is inside the frustum. Conservative: a box that straddles a
//! frustum boundary is kept (never dropped), which is what makes large visible
//! chunks survive — the 8-corner "any corner inside" test would cull them.
inline bool aabbVisible(const Eigen::Vector3f &min, const Eigen::Vector3f &max,
                        const Frustum &frustum)
{
  for (int i = 0; i < frustum.planeCount(); ++i) {
    const FrustumPlane &p = frustum.plane(i);
    const Eigen::Vector3f pv(
        p.n.x() >= 0.0f ? max.x() : min.x(),
        p.n.y() >= 0.0f ? max.y() : min.y(),
        p.n.z() >= 0.0f ? max.z() : min.z());
    if (p.n.dot(pv) + p.d < 0.0f)
      return false; // entirely outside this plane
  }
  return true;
}

//! Indices of the chunks (given as parallel min/max AABB arrays) visible in the
//! frustum. Chunk i is visible iff aabbVisible(mins[i], maxs[i], frustum).
inline std::vector<int> cullChunks(const std::vector<Eigen::Vector3f> &mins,
                                   const std::vector<Eigen::Vector3f> &maxs,
                                   const Frustum &frustum)
{
  std::vector<int> visible;
  const std::size_t n = mins.size();
  if (maxs.size() < n)
    return visible;
  visible.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (aabbVisible(mins[i], maxs[i], frustum))
      visible.push_back(static_cast<int>(i));
  }
  return visible;
}

//! Axis-aligned bounding box of a set of sphere records (each record's center
//! expanded by its radius), computed per axis from the per-record min/max so it
//! is tighter than a single shared radius. Cached with the chunk at build time
//! (task 2.4); here it is the testable building block for 2.3.
template <typename Record>
void buildSphereAABB(const Record *records, int count, Eigen::Vector3f &min,
                     Eigen::Vector3f &max)
{
  bool first = true;
  for (int i = 0; i < count; ++i) {
    const Eigen::Vector3f c(records[i].pos[0], records[i].pos[1],
                            records[i].pos[2]);
    const float r = records[i].radius;
    const Eigen::Vector3f lo = c - Eigen::Vector3f(r, r, r);
    const Eigen::Vector3f hi = c + Eigen::Vector3f(r, r, r);
    if (first) {
      min = lo;
      max = hi;
      first = false;
    } else {
      for (int a = 0; a < 3; ++a) {
        if (lo[a] < min[a])
          min[a] = lo[a];
        if (hi[a] > max[a])
          max[a] = hi[a];
      }
    }
  }
}

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_FRUSTUM_H
