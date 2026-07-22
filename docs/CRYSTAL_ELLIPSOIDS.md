# Crystal Structure Ellipsoids

## Overview

This feature adds displacement ellipsoid rendering for crystal structures in Avogadro. Atoms with anisotropic displacement parameters (ADPs) are rendered as 3D ellipsoids instead of spheres, providing visual representation of atomic vibration thermal parameters.

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
- `bool hasAnisoU() const` - Check if anisotropic U-tensor is available
- `Eigen::Matrix3d anisoU() const` - Get the 3x3 U-matrix
- `void setAnisoU(const Eigen::Matrix3d &U)` - Set the U-matrix
- `double uIso() const` - Get isotropic U
- `bool hasUIso() const` - Check if isotropic U is available

Bridge in `Atom::setOBAtom()` extracts U-tensor from OpenBabel's `OBPairData`.

#### 2. GLPainter (`glpainter_p.h`, `glpainter_p.cpp`)

Implemented `drawEllipsoid()` method:
- Diagonalizes the 3x3 U-matrix to get eigenvalues/eigenvectors
- Computes semi-axes lengths as √(eigenvalues)
- Generates icosphere mesh (subdivision level 2-3)
- Transforms vertices: rotate by eigenvectors, scale by semi-axes, translate
- Renders as filled mesh with per-vertex normals

#### 3. EllipsoidEngine (`ellipsoidengine.h`, `ellipsoidengine.cpp`)

New engine plugin for rendering atoms as displacement ellipsoids:
- **Style**: Ball-and-stick with ellipsoids instead of spheres
- **Color**: CPK element colors (same as BallAndStick)
- **Opacity**: Opaque (no transparency)
- **Picking**: Full ellipsoid ray-intersection test

Settings:
- `m_scale` (default 1.0) - Scale multiplier for ellipsoid size
- `m_meshQuality` (default 3) - Icosphere subdivision level
- `m_opacity` (default 1.0) - Opacity
- `m_showEllipsoids` (default true) - Show/hide ellipsoids
- `m_useIsotropicFallback` (default true) - Use Uiso spheres for atoms without anisotropic data
- `m_showAxes` (default false) - Show eigenvector axes

Fallback behavior:
1. If atom has anisotropic U-tensor → render as ellipsoid
2. Else if `m_useIsotropicFallback` and atom has Uiso → render as sphere with radius √(Uiso) × scale × 10
3. Else → render as sphere with VdW radius (1.7 Å)

### Registration

Added to `libavogadro/src/engines/CMakeLists.txt`:
```cmake
avogadro_plugin(ellipsoidengine ellipsoidengine.cpp)
```

## Usage

1. Open a CIF file with anisotropic displacement parameters
2. Select "Crystallographic Ellipsoids" from the Display Settings menu
3. Adjust settings (scale, quality, etc.) as needed

## Technical Details

### Ellipsoid Generation

The U-matrix is diagonalized using `Eigen::SelfAdjointEigenSolver`:
```cpp
Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(U);
Eigen::Vector3d eigenvalues = solver.eigenvalues();
Eigen::Matrix3d eigenvectors = solver.eigenvectors();
Eigen::Vector3d semiAxes = eigenvalues.cwiseSqrt();
```

### Mesh Generation

Icosphere subdivision algorithm:
1. Start with icosahedron (12 vertices, 20 faces)
2. Subdivide each triangle by adding midpoints on edges
3. Project new vertices to unit sphere
4. Repeat for specified subdivision level
5. Result: smooth sphere-like mesh (~160-640 triangles)

### Rendering

- Vertices are transformed: `v_transformed = eigenvectors × (v × semiAxes) + position`
- Normals computed via cross product of edges
- Mesh rendered with `glDrawArrays(GL_TRIANGLES, ...)`

## Future Enhancements

- GPU shader path for ellipsoid rendering (currently CPU-only)
- Axis display (eigenvector visualization)
- Per-atom color customization
- Isosurface rendering option
- Performance optimization with instanced rendering

## Files Modified/Created

### Modified
- `CMakeLists.txt` - Updated OpenBabel commit and SHA256
- `libavogadro/src/atom.h` - Added U-tensor API
- `libavogadro/src/atom.cpp` - Added U-tensor storage and bridge
- `libavogadro/src/glpainter_p.h` - Added `drawEllipsoid()` and `generateIcosphereBase()`
- `libavogadro/src/glpainter_p.cpp` - Implemented ellipsoid rendering

### Created
- `libavogadro/src/engines/ellipsoidengine.h` - EllipsoidEngine header
- `libavogadro/src/engines/ellipsoidengine.cpp` - EllipsoidEngine implementation
- `libavogadro/src/engines/CMakeLists.txt` - Added ellipsoidengine plugin

## Testing

Build with:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -- -j4
```

Run tests:
```bash
ctest --test-dir build --output-on-failure
```

## References

- OpenBabel CIF format: `_atom_site_aniso_U_11` through `_atom_site_aniso_U_23`
- Crystallographic ADPs: https://www.iucr.org/__data/iucr/cif_amplitude/
- Icosphere algorithm: https://en.wikipedia.org/wiki/Icosphere
