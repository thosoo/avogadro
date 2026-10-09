/**********************************************************************
  GPU vertex-format layout for the modern tier instance records
  (gpu-rendering-pipeline task 2.1).

  The per-instance records live in records.h as plain structs. Because the
  structs are trivially copyable and contain no padding-sensitive types beyond
  the aligned floats/bytes, their in-memory layout is exactly what is uploaded
  to the instance VBO and consumed by the instanced shaders in shaders.h. This
  header documents that layout (offsets + stride, in bytes) in one place so the
  backend bindings and the tests cannot drift apart.

  Layout notes:
    * float  = 4 bytes, 4-byte aligned.
    * unsigned char[4] = 4 bytes; stored as a vec4 attribute with
      GL_UNSIGNED_BYTE / GL_FLOAT / normalized so the values are mapped to
      [0, 1] automatically.
    * int  = 4 bytes (bond multiplicity).
    * short = 2 bytes (aromatic stipple flag).

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

#ifndef AVOGADRO_RENDERING_INSTANCEFORMAT_H
#define AVOGADRO_RENDERING_INSTANCEFORMAT_H

#include <cstddef>

#include "records.h"

namespace Avogadro {
namespace rendering {

//! Byte layout of a SphereInstance instance record (see records.h).
//!   pos      : 3 x float  @ 0   (atom centre, applied after the unit scale)
//!   radius   : 1 x float  @ 12
//!   color    : 4 x u8     @ 16  (rgba, uploaded as a normalized vec4)
//! stride     : 20 bytes
namespace sphereInstanceLayout {
constexpr int kPosOffset = 0;
constexpr int kRadiusOffset = 12;
constexpr int kColorOffset = 16;
constexpr int kStride = 20;
} // namespace sphereInstanceLayout

//! Byte layout of a BondInstance instance record (see records.h).
//!
//!   start    : 3 x float  @ 0   (cylinder start point)
//!   end      : 3 x float  @ 12
//!   radius   : 1 x float  @ 24
//!   color    : 4 x u8     @ 28  (uploaded as a normalized vec4)
//!   order    : 1 x int    @ 32  (bond multiplicity)
//!   shift    : 1 x double @ 40  (8-byte aligned; metadata, not bound by the
//!                                 geometry shaders — positions are pre-offset
//!                                 by the SceneBuilder emitter)
//! stride     : 40 bytes
namespace bondInstanceLayout {
constexpr int kStartOffset = 0;
constexpr int kEndOffset = 12;
constexpr int kRadiusOffset = 24;
constexpr int kColorOffset = 28;
constexpr int kOrderOffset = 32;
constexpr int kShiftOffset = 40;
constexpr int kStride = 48;
} // namespace bondInstanceLayout

// Compile-time sanity checks that the record structs lay out exactly as the
//! constants above assume. These fire at compile time so a struct edit cannot
//! silently desynchronise the GPU binding from the CPU record. offsetof is used
//! (rather than QMetaType::offsetOf) so there is no QMetaType registration
//! dependency and it works identically on MSVC and GCC/Clang.
struct InstanceFormatAssertions {
  static_assert(sizeof(SphereInstance) == sphereInstanceLayout::kStride,
                "SphereInstance size must match sphereInstanceLayout::kStride");
  static_assert(offsetof(SphereInstance, pos) == 0,
                "SphereInstance.pos must be at offset 0");
  static_assert(offsetof(SphereInstance, radius) == 12,
                "SphereInstance.radius must be at offset 12");
  static_assert(offsetof(SphereInstance, color) == 16,
                "SphereInstance.color must be at offset 16");

  static_assert(sizeof(BondInstance) == bondInstanceLayout::kStride,
                "BondInstance size must match bondInstanceLayout::kStride");
  static_assert(offsetof(BondInstance, start) == 0,
                "BondInstance.start must be at offset 0");
  static_assert(offsetof(BondInstance, end) == 12,
                "BondInstance.end must be at offset 12");
  static_assert(offsetof(BondInstance, radius) == 24,
                "BondInstance.radius must be at offset 24");
  static_assert(offsetof(BondInstance, color) == 28,
                "BondInstance.color must be at offset 28");
  static_assert(offsetof(BondInstance, order) == 32,
                "BondInstance.order must be at offset 32");
  static_assert(offsetof(BondInstance, shift) == 40,
                "BondInstance.shift must be at offset 40");
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_INSTANCEFORMAT_H
