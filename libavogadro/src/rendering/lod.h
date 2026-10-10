/**********************************************************************
  Level-of-detail for the modern rendering tier (gpu-rendering-pipeline task
  2.3).

  The modern tier draws every atom from one unit-sphere VAO and every bond from
  one unit-cylinder VAO (geometry.h), instanced with the same per-instance
  records regardless of detail. LOD therefore means: 2-3 prebuilt index-buffer
  detail levels on the same instance data, chosen per zoom band with hysteresis
  (design D2). This replaces per-primitive dynamic scaling and is strictly
  higher quality than the octahedron floor.

  Two headless-testable pieces live here:
    - onScreenRadius(): the on-screen size (px) of a reference primitive for a
      given model*projection matrix and viewport. It responds to this app's
      zoom (a modelview linear-part scale), which 1/camera-distance does NOT.
    - ZoomLodController: a Schmitt-trigger selector that picks a LOD from the
      on-screen radius with a multiplicative deadband, so the level cannot
      flicker across a band boundary and never oscillates.

  Pure CPU (Eigen only, no OpenGL) so both are unit-testable headlessly.

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

#ifndef AVOGADRO_RENDERING_LOD_H
#define AVOGADRO_RENDERING_LOD_H

#include <avogadro/global.h>
#include <Eigen/Geometry>

namespace Avogadro {
namespace rendering {

//! Tessellation detail levels for the unit sphere and cylinder. The instance
//! data is identical across levels; only the generated index buffer changes.
enum LodLevel {
  LodLow,
  LodMedium,
  LodHigh,
  LodLevelCount
};

//! (sphereRings, sphereSegments, cylinderSegments) for a level. Every preset
//! stays well under the 65536 vertex ceiling of the unsigned-short index buffer.
struct LodParams {
  int sphereRings;
  int sphereSegments;
  int cylinderSegments;
};

//! Preset tessellation for a level. Medium preserves the existing default
//! (12 rings / 24 segments) so visual parity with the pre-LOD tier is intact;
//! Low is coarser, High is finer.
A_EXPORT LodParams lodParams(LodLevel level);

//! On-screen radius (pixels) of a primitive of physical radius `refRadius`,
//! for the given model and projection matrices and viewport height.
//!
//!   rho = s * refRadius * (H / 2) * P(1,1) / z_c      (perspective)
//!   rho = s * refRadius * (H / 2) * P(1,1)            (orthographic)
//!
//! where
//!   s     = |det(modelview.linear())|^(1/3)  == Camera::scalingCoefficient(),
//!           so this responds to scale()-zoom (unlike 1/camera-distance),
//!   z_c   = eye-space depth of `focus` = -((modelview * [c;1]).z()),
//!   P(1,1)= projection[1][1] (== 1/tan(fovY/2) for gluPerspective),
//!   H     = viewport height in pixels.
//!
//! Perspective is detected by P(3,3) ~= 0. A focus point at/behind the near
//! plane is clamped so z_c stays positive. No Camera object is required, so
//! this is headless-testable with hand-built Eigen matrices.
A_EXPORT float onScreenRadius(const Eigen::Matrix4f &modelview,
                              const Eigen::Matrix4f &projection,
                              float viewportHeight, const Eigen::Vector3f &focus,
                              float refRadius);

//! Per-frame LOD selector with Schmitt-trigger hysteresis.
//!
//! Thresholds are multiplicative: switching low->medium requires rho >= t01*g
//! while medium->low requires rho < t01/g, so the dead band [t01/g, t01*g]
//! fires neither condition and the level cannot oscillate at a boundary.
//!
//! select() is O(1) and idempotent. The very first select() after reset()
//! performs the initial selection without counting as churn (nothing was
//! tessellated before it); later level changes increment churnCount().
class A_EXPORT ZoomLodController {
public:
  //! t01 = low/medium boundary, t12 = medium/high boundary, g = deadband
  //! factor (>1). Boundaries are ratios (perceptual steps) because this app's
  //! zoom is multiplicative.
  ZoomLodController(float t01 = 4.0f, float t12 = 16.0f, float g = 1.3f);

  //! Select the LOD for the given on-screen radius (pixels). Returns the level
  //! and, only if it changed since the previous non-initial call, increments
  //! churnCount().
  LodLevel select(float onScreenRadius);

  LodLevel level() const { return m_level; }
  int churnCount() const { return m_churn; }

  //! Reset to the nominal medium level with churn cleared. The next select()
  //! is the initial selection and does not count as churn.
  void reset();

private:
  float m_t01;
  float m_t12;
  float m_g;
  LodLevel m_level;
  int m_churn;
  bool m_initialized;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_LOD_H
