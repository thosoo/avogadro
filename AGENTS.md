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
