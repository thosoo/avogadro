/**********************************************************************
  Level-of-detail implementation (gpu-rendering-pipeline task 2.3).
 *****************************************************************/

#include "lod.h"

#include <cmath>

namespace Avogadro {
namespace rendering {

namespace {
//! Clamp for the eye-space focus depth so a focus point at/behind the camera
//! cannot make z_c <= 0 and flip the sign of the perspective term.
constexpr float kMinFocusDepth = 1e-3f;
} // namespace

LodParams lodParams(LodLevel level)
{
  switch (level) {
    case LodLow:
      return {8, 16, 16};
    case LodHigh:
      return {32, 64, 64};
    case LodMedium:
    default:
      // Preserve the pre-LOD default for visual parity.
      return {12, 24, 24};
  }
}

float onScreenRadius(const Eigen::Matrix4f &modelview,
                     const Eigen::Matrix4f &projection, float viewportHeight,
                     const Eigen::Vector3f &focus, float refRadius)
{
  // P(1,1) == 1/tan(fovY/2) for gluPerspective; for orthographic it carries
  // the ortho scale. Perspective is detected by P(3,3) ~= 0.
  const bool perspective = std::abs(projection(3, 3)) < 0.5f;
  const float p11 = projection(1, 1);

  // s == Camera::scalingCoefficient(): the isotropic scale of the modelview
  // linear part (top-left 3x3). scale()-zoom multiplies this, so it is what
  // genuine zoom changes (1/camera-distance would miss it for a centered scene).
  // Compute the 3x3 determinant by hand: this Eigen build exposes no det().
  const Eigen::Matrix3f lin = modelview.block<3, 3>(0, 0);
  const float det =
      lin(0, 0) * (lin(1, 1) * lin(2, 2) - lin(1, 2) * lin(2, 1)) -
      lin(0, 1) * (lin(1, 0) * lin(2, 2) - lin(1, 2) * lin(2, 0)) +
      lin(0, 2) * (lin(1, 0) * lin(2, 1) - lin(1, 1) * lin(2, 0));
  const float s = std::pow(std::abs(det), 1.0f / 3.0f);

  float rho;
  if (perspective) {
    const Eigen::Vector4f fc =
        modelview * Eigen::Vector4f(focus.x(), focus.y(), focus.z(), 1.0f);
    float zc = -fc[2]; // eye-space depth of the focus point (positive ahead)
    if (zc < kMinFocusDepth)
      zc = kMinFocusDepth;
    rho = s * refRadius * (viewportHeight * 0.5f) * p11 / zc;
  } else {
    // Orthographic: scale() lives in the projection (orthoScale), so P(1,1)
    // already carries the zoom and s == 1.
    rho = s * refRadius * (viewportHeight * 0.5f) * p11;
  }
  return rho;
}

ZoomLodController::ZoomLodController(float t01, float t12, float g)
    : m_t01(t01), m_t12(t12), m_g(g), m_level(LodMedium), m_churn(0),
      m_initialized(false)
{
}

LodLevel ZoomLodController::select(float onScreenRadius)
{
  // Initial selection: no hysteresis, no churn (nothing was tessellated yet).
  if (!m_initialized) {
    if (onScreenRadius >= m_t12)
      m_level = LodHigh;
    else if (onScreenRadius >= m_t01)
      m_level = LodMedium;
    else
      m_level = LodLow;
    m_initialized = true;
    return m_level;
  }

  const LodLevel previous = m_level;
  switch (m_level) {
    case LodLow:
      if (onScreenRadius >= m_t01 * m_g)
        m_level = LodMedium;
      break;
    case LodMedium:
      if (onScreenRadius >= m_t12 * m_g)
        m_level = LodHigh;
      else if (onScreenRadius < m_t01 / m_g)
        m_level = LodLow;
      break;
    case LodHigh:
      if (onScreenRadius < m_t12 / m_g)
        m_level = LodMedium;
      break;
    default:
      break;
  }

  if (m_level != previous)
    ++m_churn;
  return m_level;
}

void ZoomLodController::reset()
{
  m_level = LodMedium;
  m_churn = 0;
  m_initialized = false;
}

} // namespace rendering
} // namespace Avogadro
