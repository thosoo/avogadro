/**********************************************************************
  Primitive geometry implementation (see geometry.h).
 *****************************************************************/

#include "geometry.h"

namespace Avogadro {
namespace rendering {

void SphereGeometry::generate()
{
  const int rings = qMax(1, m_rings);
  const int segments = qMax(3, m_segments);

  m_vertices.clear();
  m_indices.clear();
  m_vertices.reserve(static_cast<int>(rings + 1) * segments);

  const double twoPi = 2.0 * M_PI;

  // A (rings + 1) x segments grid of vertices. Ring 0 is the north pole
  // (phi = 0) and ring `rings` is the south pole (phi = pi); at the poles
  // sin(phi) = 0 so every vertex in that ring collapses to the pole, which is
  // harmless because no quad there has non-zero area.
  for (int r = 0; r <= rings; ++r) {
    const double phi = M_PI * static_cast<double>(r) / rings; // 0..pi
    const double sinPhi = std::sin(phi);
    const double cosPhi = std::cos(phi);

    for (int s = 0; s < segments; ++s) {
      const double theta = twoPi * s / segments; // 0..2pi
      const double cosTheta = std::cos(theta);
      const double sinTheta = std::sin(theta);

      const double x = sinPhi * cosTheta;
      const double y = cosPhi;
      const double z = sinPhi * sinTheta;

      GeometryVertex v;
      v.pos[0] = static_cast<float>(x);
      v.pos[1] = static_cast<float>(y);
      v.pos[2] = static_cast<float>(z);
      // On a unit sphere the normal equals the position.
      v.normal[0] = static_cast<float>(x);
      v.normal[1] = static_cast<float>(y);
      v.normal[2] = static_cast<float>(z);
      m_vertices.append(v);
    }
  }

  m_indices.reserve(rings * segments * 6);
  for (int r = 0; r < rings; ++r) {
    for (int s = 0; s < segments; ++s) {
      const int sNext = (s + 1) % segments;
      const int i00 = r * segments + s;         // (r,   s  )
      const int i01 = r * segments + sNext;     // (r,   s+1)
      const int i10 = (r + 1) * segments + s;   // (r+1, s  )
      const int i11 = (r + 1) * segments + sNext; // (r+1, s+1)
      // Two triangles per cell, ordered counter-clockwise when viewed from
      // outside the sphere (outward-facing normals).
      m_indices.append(i00);
      m_indices.append(i01);
      m_indices.append(i11);
      m_indices.append(i00);
      m_indices.append(i11);
      m_indices.append(i10);
    }
  }
}

void CylinderGeometry::generate()
{
  const int segments = qMax(3, m_segments);

  m_vertices.clear();
  m_indices.clear();
  m_vertices.reserve(segments * 2);

  const double twoPi = 2.0 * M_PI;

  // Two rows of vertices per segment: top (z = +0.5) and bottom (z = -0.5).
  for (int s = 0; s < segments; ++s) {
    const double theta = twoPi * s / segments;
    const double cosT = std::cos(theta);
    const double sinT = std::sin(theta);

    GeometryVertex top;
    top.pos[0] = static_cast<float>(cosT);
    top.pos[1] = static_cast<float>(sinT);
    top.pos[2] = 0.5f;
    top.normal[0] = static_cast<float>(cosT);
    top.normal[1] = static_cast<float>(sinT);
    top.normal[2] = 0.0f;

    GeometryVertex bot;
    bot.pos[0] = static_cast<float>(cosT);
    bot.pos[1] = static_cast<float>(sinT);
    bot.pos[2] = -0.5f;
    bot.normal[0] = static_cast<float>(cosT);
    bot.normal[1] = static_cast<float>(sinT);
    bot.normal[2] = 0.0f;

    m_vertices.append(top);
    m_vertices.append(bot);
  }

  m_indices.reserve(segments * 6);
  for (int s = 0; s < segments; ++s) {
    const int sNext = (s + 1) % segments;
    const int iTop = s * 2;
    const int iBot = s * 2 + 1;
    const int jTop = sNext * 2;
    const int jBot = sNext * 2 + 1;

    // Two triangles per quad with outward-facing normals.
    m_indices.append(iTop);
    m_indices.append(iBot);
    m_indices.append(jTop);
    m_indices.append(jTop);
    m_indices.append(iBot);
    m_indices.append(jBot);
  }
}

} // namespace rendering
} // namespace Avogadro
