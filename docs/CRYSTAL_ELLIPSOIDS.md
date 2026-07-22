# Crystal Structure Ellipsoids

## Overview

This feature adds displacement ellipsoid rendering for crystal structures in Avogadro. Atoms with anisotropic displacement parameters (ADPs) are rendered as 3D ellipsoids instead of spheres, providing visual representation of atomic vibration thermal parameters.

The **EllipsoidEngine** is a ball-and-stick style renderer specifically for crystallographic data. It is not a general-purpose display mode — users who want sticks-only should use the existing StickEngine.

## Implementation

### OpenBabel Integration

- Updated bundled OpenBabel to commit `c1a91a5e` (from `d8695a26`)
- OpenBabel CIF reader now parses `_atom_site_aniso_U_*` fields and stores them as `OBPairData` with keys:
  - `adp_U_11`, `adp_U_22`, `adp_U_33`, `adp_U_12`, `adp_U_13`, `adp_U_23` (anisotropic U-tensor)
  - `adp_U_iso_or_equiv` (isotropic U)

### Avogadro Changes

#### 1. Atom Class (`atom.h`, `atom.cpp`)

Added U-tensor storage to `AtomPrivate`:
```cpp
Eigen::Matrix3d anisoU;      // 3x3 symmetric anisotropic displacement parameter matrix
bool hasAnisoU;              // flag indicating whether anisotropic U-tensor is available
double uIso;                 // isotropic displacement parameter (fallback)
bool hasUIso;                // flag indicating whether isotropic U is available
```

Public API:
- `bool hasAnisoU() const` — Check if anisotropic U-tensor is available
- `Eigen::Matrix3d anisoU() const` — Get the 3x3 U-matrix
- `void setAnisoU(const Eigen::Matrix3d &U)` — Set the U-matrix
- `double uIso() const` — Get isotropic U
- `bool hasUIso() const` — Check if isotropic U is available

Bridge in `Atom::setOBAtom()` extracts U-tensor from OpenBabel's `OBPairData`.

#### 2. GLPainter (`glpainter_p.h`, `glpainter_p.cpp`)

Implemented `drawEllipsoid()` method with dynamic quality scaling:
- Accepts pre-diagonalized data: eigenvectors, semi-axes, and max semi-axis length
- Computes subdivision level based on apparent size (consistent with sphere/cylinder scaling)
- Uses cached icosphere base meshes (generated once, reused for all ellipsoids)

#### 3. EllipsoidEngine (`ellipsoidengine.h`, `ellipsoidengine.cpp`)

New engine plugin for rendering atoms as displacement ellipsoids:
- **Style**: Ball-and-stick with ellipsoids instead of spheres
- **Color**: CPK element colors (same as BallAndStick)
- **Opacity**: Opaque (no transparency)
- **Bonds**: Connect at ellipsoid surfaces (not atom centers) for geometric correctness

Settings:
- `m_scale` (default 1.73) — **k-factor multiplier** for probability ellipsoid. `1.73` = 50% probability (crystallography standard), `2.58` = 95% probability. Semi-axes = k × √(eigenvalues of U).
- `m_drawIsotropicSpheres` (default true) — Draw isotropic Uiso spheres for atoms without anisotropic data. When false, falls back to VdW spheres.

Fallback behavior:
1. If atom has anisotropic U-tensor → render as ellipsoid
2. Else if `m_drawIsotropicSpheres` and atom has Uiso → render as sphere with radius `k × √(Uiso)`
3. Else → render as sphere with element-specific VdW radius (from OpenBabel)

### Registration

Added to `libavogadro/src/engines/CMakeLists.txt`:
```cmake
avogadro_plugin(ellipsoidengine ellipsoidengine.cpp)
```

## Usage

1. Open a CIF file with anisotropic displacement parameters
2. Select "Crystallographic Ellipsoids" from the Display Settings menu
3. Adjust settings (scale, drawIsotropicSpheres) as needed

## Technical Details

### Ellipsoid Generation

The U-matrix is diagonalized once per atom per render pass using `Eigen::SelfAdjointEigenSolver`:
```cpp
Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(U * scale);
Eigen::Vector3d eigenvalues = solver.eigenvalues();
Eigen::Matrix3d eigenvectors = solver.eigenvectors();
Eigen::Vector3d semiAxes = eigenvalues.cwiseSqrt(); // with 0.01 floor for degenerate cases
```

**Probability level:** The `m_scale` parameter is a **k-factor multiplier**. The semi-axes of the probability ellipsoid are `k × √(eigenvalues of U)`, where `k` controls the probability contour:

| k-factor | Probability | Use case |
|----------|-------------|----------|
| 1.00     | 63.2% (1σ)  | Maximum detail, small ellipsoids |
| **1.73** | **50%**     | **Default — crystallography standard** |
| 2.58     | 95% (2σ)    | Publication-quality, larger ellipsoids |

The 50% probability ellipsoid is the convention used by Olex2, Mercury, Diamond, and most crystallography software.

### Mesh Generation

Icosphere subdivision algorithm:
1. Start with icosahedron (12 vertices, 20 faces)
2. Subdivide each triangle by adding midpoints on edges
3. Project new vertices to unit sphere
4. Repeat for specified subdivision level
5. Result: smooth sphere-like mesh (~160-640 triangles)

**Caching:** Icosphere base meshes are cached in `GLPainterPrivate` and generated once on first use. All ellipsoids at a given subdivision level share the same base mesh — only the per-atom rotation, scale, and translation differ.

### Rendering

- Vertices are transformed: `v_transformed = eigenvectors × (v × semiAxes) + position`
- Normals computed via cross product of edges
- Mesh rendered with `glDrawArrays(GL_TRIANGLES, ...)`

### Bond Surface Intersection

Bonds connect at ellipsoid surfaces, not atom centers. For an ellipsoid with semi-axes `a, b, c` and eigenvectors `v1, v2, v3`, the surface point in direction `d` is:

```
dLocal = eigenvectors^T × d
r = 1 / sqrt( (dLocal·v1)²/a² + (dLocal·v2)²/b² + (dLocal·v3)²/c² )
endpoint = center + r × d
```

This ensures bonds terminate realistically at the ellipsoid surface, which is especially important for highly anisotropic atoms (e.g., flat O atoms in carbonate groups).

### Performance Optimization

- **Cached diagonalization:** The U-matrix is diagonalized once per atom per render pass. Results are stored in per-atom caches and reused for both ellipsoid rendering and bond surface intersection.
- **Cached icosphere meshes:** Base icosphere geometry is generated once and shared across all ellipsoids at a given subdivision level.
- **Dynamic quality:** Subdivision level is computed from apparent size and global painter quality, consistent with sphere/cylinder rendering.
- **Quick mode:** `renderQuick()` draws spheres instead of ellipsoids and bonds as center-to-center cylinders (no surface intersection math).

### Render Passes

| Pass | Atoms | Bonds |
|------|-------|-------|
| `renderOpaque` | Ellipsoids (full mesh) | Surface intersection |
| `renderQuick` | Spheres (max semi-axis) | Center-to-center cylinders |
| `renderPick` | Spheres (max semi-axis) | N/A |

## Files Modified/Created

### Modified
- `CMakeLists.txt` — Updated OpenBabel commit and SHA256
- `libavogadro/src/atom.h` — Added U-tensor API
- `libavogadro/src/atom.cpp` — Added U-tensor storage and bridge
- `libavogadro/src/glpainter_p.h` — Added `drawEllipsoid()` and `getIcosphere()`
- `libavogadro/src/glpainter_p.cpp` — Implemented ellipsoid rendering with caching
- `libavogadro/src/painter.h` — Updated `drawEllipsoid()` signature
- `libavogadro/src/extensions/povpainter.{h,cpp}` — Updated `drawEllipsoid()` signature
- `libavogadro/src/extensions/vrmlpainter.{h,cpp}` — Updated `drawEllipsoid()` signature
- `libavogadro/src/engines/CMakeLists.txt` — Added ellipsoidengine plugin

### Created
- `libavogadro/src/engines/ellipsoidengine.h` — EllipsoidEngine header
- `libavogadro/src/engines/ellipsoidengine.cpp` — EllipsoidEngine implementation
- `testfiles/cod_7122598.cif` — Test structure with ADPs
- `testfiles/LICENSE_CIF_FILES.md` — Licensing documentation

## Testing

Build with:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -- -j6
```

Run tests:
```bash
ctest --test-dir build --output-on-failure
```

## References

- OpenBabel CIF format: `_atom_site_aniso_U_11` through `_atom_site_aniso_U_23`
- Crystallographic ADPs: https://www.iucr.org/__data/iucr/cif_amplitude/
- Icosphere algorithm: https://en.wikipedia.org/wiki/Icosphere
- COD source: https://www.crystallography.net/cod/7122598.html
- Test data license: CC0 1.0 Public Domain
