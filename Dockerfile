FROM ubuntu:24.04

RUN apt-get update && apt install git -y --no-install-recommends

RUN apt-get update && apt install build-essential \
cmake \
qt6-base-dev \
qt6-base-dev-tools \
qt6-tools-dev \
qt6-tools-dev-tools \
libqt6opengl6-dev \
libeigen3-dev \
zlib1g-dev \
libglu1-mesa-dev \
curl \
libglew-dev -y


# Copy the Avogadro source and set the working directory
COPY . /app
WORKDIR /app

CMD ["/bin/bash"]
