# Avogadro Crystallography Context

This context covers crystallographic data handling and rendering in Avogadro — specifically anisotropic displacement parameters (ADPs), CIF file processing, and ellipsoid rendering for crystal structures.

## Language

**Displacement Ellipsoid**:
A 3D ellipsoid representing the probability distribution of an atom's position due to thermal vibration and static disorder, derived from the anisotropic displacement parameter (U-tensor). The semi-axes are `k × √(eigenvalues of U)`, where `k` is the k-factor controlling the probability contour level.
_Avoid_: Thermal ellipsoid, vibration ellipsoid, U-ellipsoid

**U-tensor** (Anisotropic Displacement Parameter):
A 3×3 symmetric positive semi-definite matrix (in Å²) describing the anisotropic mean-square displacement of an atom. Stored in Avogadro as `Eigen::Matrix3d` on the Atom class. The diagonal elements are `U11, U22, U33`; the off-diagonals are `U12, U13, U23`.
_Avoid_: ADP matrix, thermal parameter tensor, B-tensor

**Uiso** (Isotropic Displacement Parameter):
A single scalar value (in Å²) representing the isotropic average of the U-tensor: `Uiso = (U11 + U22 + U33) / 3`. Used as a fallback when anisotropic data is unavailable. Renders as a sphere with radius `k × √(Uiso)`.
_Avoid_: Biso, isotropic B-factor, temperature factor

**k-factor**:
A multiplier applied to the semi-axes of a displacement ellipsoid to control the probability contour level. `k = 1.73` gives the 50% probability ellipsoid (crystallography standard); `k = 2.58` gives 95%. Default in Avogadro is 1.73.
_Avoid_: Scale factor (ambiguous — could mean display scale), probability factor

**Probability Ellipsoid**:
The contour surface of the atomic displacement probability distribution at a given confidence level. For a 3D Gaussian, the 50% probability ellipsoid has k ≈ 1.73. This is the conventional visualization in crystallography software (Olex2, Mercury, Diamond).
_Avoid_: Vibration ellipsoid (implies only thermal motion, not static disorder)

**CIF** (Crystallographic Information File):
The standard file format for crystal structure data. Contains atomic positions, unit cell parameters, symmetry operations, and anisotropic displacement parameters (`_atom_site_aniso_U_*` fields). Read via OpenBabel's CIF parser.
_Avoid_: Cif file, crystal file

**Engine** (Rendering):
A pluggable rendering module that draws atoms and bonds using OpenGL. Each Engine implements `renderOpaque()`, `renderQuick()`, and `renderPick()`. Examples: SphereEngine, StickEngine, EllipsoidEngine.
_Avoid_: Display mode, visualization plugin, renderer

**EllipsoidEngine**:
A rendering engine that draws atoms with anisotropic displacement parameters as 3D ellipsoids. Uses ball-and-stick style with CPK element coloring. Falls back to Uiso spheres or element-specific VdW spheres for atoms without anisotropic data. Diagonalizes the U-matrix once per atom per render pass (cached), and bonds connect at ellipsoid surfaces rather than atom centers.

**Semi-axes** (of a displacement ellipsoid):
The three lengths `√(eigenvalues of U) × k` that define the ellipsoid's extent along its principal axes. The longest semi-axis determines the apparent size for dynamic quality scaling.

**Eigenvectors** (in ellipsoid rendering):
The 3×3 rotation matrix whose columns are the principal axes of the U-tensor. Used to orient the icosphere mesh before scaling by semi-axes. Cached per atom after one diagonalization per render pass.

**Bond Surface Intersection**:
The point where a bond cylinder meets an ellipsoid surface. Computed by evaluating the ellipsoid equation in the direction from atom center to bonded neighbor. Ensures bonds terminate realistically at the ellipsoid surface rather than at atom centers.

**Render Passes**:
- **Opaque pass**: Full ellipsoids with bond surface intersection
- **Quick pass**: Spheres (max semi-axis) with center-to-center bonds
- **Pick pass**: Spheres (max semi-axis) for ray-intersection picking
