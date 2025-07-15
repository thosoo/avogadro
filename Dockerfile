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


# Copy the Avogadro source and set the working directory
COPY . /app
WORKDIR /app

CMD ["/bin/bash"]
