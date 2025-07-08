FROM ubuntu:24.04

RUN apt-get update && apt install git -y --no-install-recommends

RUN apt-get update && apt install build-essential \
cmake \
qtbase5-dev \
qttools5-dev \
qttools5-dev-tools \
libqt5opengl5-dev \
libeigen3-dev \
zlib1g-dev \
libglu1-mesa-dev \
libqt5test5t64 \
curl \
libglew-dev -y

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
COPY . /app
WORKDIR /app

CMD ["/bin/bash"]
