This repository requires Qt5 and other packages to build. Install dependencies on Ubuntu with:

```
sudo apt-get update
sudo apt-get install -y build-essential cmake qtbase5-dev qttools5-dev qttools5-dev-tools libqt5opengl5-dev libeigen3-dev zlib1g-dev libglu1-mesa-dev libglew-dev
```

OpenBabel will be downloaded automatically when configuring the build, so the
`libopenbabel-dev` package is not required.

Compilation is tested with GitHub Actions.

To compile Avogadro locally and run the test suite, simply configure the project
with CMake (OpenBabel will be fetched and built automatically) similar to the CI
workflow:

```
# Install runtime dependencies
sudo apt-get update
sudo xargs -a .github/apt-packages.txt apt-get install -y

# Configure Avogadro (this step downloads and builds OpenBabel)
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=ON

# Build and test
cmake --build build -- -j$(nproc)
ctest --test-dir build --output-on-failure
```

Running the above commands should produce the same results as the CI builds.
