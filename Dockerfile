FROM ubuntu:24.04

# Install dependencies for building Avogadro
COPY .github/apt-packages.txt /tmp/apt-packages.txt
RUN apt-get update && \
    xargs -a /tmp/apt-packages.txt apt-get install -y \
    git curl libglew-dev && \
    rm /tmp/apt-packages.txt

# Build OpenBabel from source (as in CI)
RUN src=/tmp/openbabel-src && \
    mkdir -p "$src" && \
    curl -L https://github.com/thosoo/openbabel/tarball/master | tar -xz --strip-components=1 -C "$src" && \
    cmake -S "$src" -B "$src/build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$src/build/install" \
      -DBUILD_SHARED=ON \
      -DOB_USE_PREBUILT_BINARIES=OFF \
      -DOPENBABEL_USE_SYSTEM_INCHI=OFF \
      -DWITH_INCHI=ON && \
    cmake --build "$src/build" --target install -j$(nproc)

ENV OpenBabel3_DIR=/tmp/openbabel-src/build/install/lib/cmake/openbabel3
ENV PKG_CONFIG_PATH=/tmp/openbabel-src/build/install/lib/pkgconfig:$PKG_CONFIG_PATH
ENV LD_LIBRARY_PATH=/tmp/openbabel-src/build/install/lib:$LD_LIBRARY_PATH
ENV OPENBABEL_INSTALL_DIR=/tmp/openbabel-src/build/install
ENV PATH=/tmp/openbabel-src/build/install/bin:$PATH

# Copy the Avogadro source and set the working directory
WORKDIR /opt/avogadro
COPY . /opt/avogadro
CMD ["bash"]
