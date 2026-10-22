This repository requires Qt6 and other packages to build. Install dependencies on Ubuntu with:

```
sudo apt-get update
sudo apt-get install -y build-essential cmake qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools libqt6opengl6-dev libeigen3-dev zlib1g-dev libglu1-mesa-dev libglew-dev
```

OpenBabel is provided as a git submodule (`extern/openbabel`) pinned to a
commit of the companion fork `thosoo/openbabel`, so the `libopenbabel-dev`
package is not required. Clone with submodules:

```
git clone --recurse-submodules <repository>
```

If you cloned without `--recurse-submodules`, initialize it before configuring:

```
git submodule update --init
```

Compilation is tested with GitHub Actions.

To compile Avogadro locally and run the test suite, simply configure the project
with CMake (OpenBabel is built from the submodule source) similar to the CI
workflow:

```
# Install runtime dependencies
sudo apt-get update
sudo xargs -a .github/apt-packages.txt apt-get install -y

# Configure Avogadro (this step builds OpenBabel from the submodule)
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_TESTS=ON

# Build and test
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Running the above commands should produce the same results as the CI builds.

> **Note:** After pulling this change, configure from a clean build directory
> (e.g. remove any existing `build/` and run `cmake -S . -B build`). Old build
> directories retain stale ExternalProject download state and can confuse the
> first configure.

## CI and cross-platform portability

Two independent workflows run on every push and every pull request, and a PR
**cannot be merged while either is red**:

- **`Qt6 CI`** (`.github/workflows/ci.yml`) — builds and tests on **Linux**
  (GCC/Clang), including an AddressSanitizer leg.
- **`Windows Installer`** (`.github/workflows/windows-installer.yml`) — builds
  on **Windows/MSVC** (`msvc2019_64`, NMake) from scratch (zlib, Eigen, libxml2,
  Qt, GLEW, Boost), packages an `.exe` installer, and runs the full test suite
  under `QT_QPA_PLATFORM=offscreen`.

**Linux CI passing does NOT prove the change builds on Windows.** The two
compilers enforce different rules, and a patch that is green on the Linux leg
can still fail the Windows installer build. When you touch shared C++, Qt, or
CMake, treat MSVC compatibility as a first-class requirement, not an afterthought.

The pitfalls Linux CI does not catch (each has bitten this repo):

- **Const-correctness of mutexes.** A mutex locked from a `const` method must be
  declared `mutable` (`mutable QMutex m_mutex;`). A non-`mutable` `QMutex` becomes
  `const QMutex*` inside a `const` method, and MSVC rejects
  `QMutexLocker<const QMutex>` (error C2662) while GCC/Clang tolerate it. The
  state-guarding mutexes in `autoopttool.h` are all `mutable` on purpose.
- **Aggregate initialization of classes with user-provided special members.**
  GCC/Clang accept e.g. `struct X { int a; ~X(); } x{ptr};`; MSVC rejects it.
  Give the type an explicit constructor and use `X(ptr)` instead.
- **`thread_local`, `std::thread`, and other C++11 threading primitives** are
  fine on MSVC 2019+, but verify any new standard feature compiles under
  `-std:c++17 -permissive-` (the flag set the Windows build uses).

If a change adds or modifies code compiled by the Windows installer job and you
cannot run MSVC locally, trigger the job to verify it before opening a PR:

```
gh workflow run 168745168 --ref <branch> --repo thosoo/avogadro
```

Then watch run #1052-style for both `build-installer` jobs to complete and the
`autooptreproTest` (and the rest of the ctest suite) to pass on Windows. A red
Windows leg means the fix is not MSVC-safe and must be corrected.
