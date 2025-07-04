This repository requires Qt5 and other packages to build. Install dependencies on Ubuntu with:

```
sudo apt-get update
sudo apt-get install -y build-essential cmake qtbase5-dev qttools5-dev qttools5-dev-tools libqt5opengl5-dev libeigen3-dev zlib1g-dev libglu1-mesa-dev libglew-dev
```

The OpenBabel library is built from source in the instructions below, so the
`libopenbabel-dev` package is not required.

Compilation is tested with GitHub Actions.

To compile Avogadro locally and run the test suite, build OpenBabel from
<https://github.com/thosoo/openbabel> and then configure the project with CMake
similar to the CI workflow:

```
# Install runtime dependencies
sudo apt-get update
sudo xargs -a .github/apt-packages.txt apt-get install -y

# Build OpenBabel from source
src=/tmp/openbabel-src
mkdir -p "$src"
curl -L https://github.com/thosoo/openbabel/tarball/master | tar -xz --strip-components=1 -C "$src"
cmake -S "$src" -B "$src/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$src/build/install" \
  -DBUILD_SHARED=ON \
  -DOB_USE_PREBUILT_BINARIES=OFF \
  -DOPENBABEL_USE_SYSTEM_INCHI=OFF \
  -DWITH_INCHI=ON
cmake --build "$src/build" --target install -j$(nproc)

# Configure Avogadro
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=ON

# Build and test
cmake --build build -- -j$(nproc)
ctest --test-dir build --output-on-failure
```

Running the above commands should produce the same results as the CI builds.
