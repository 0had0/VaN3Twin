FROM ubuntu:20.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Update and install dependencies for ns-3 and VaN3Twin
# Includes build tools, python3, cmake, and libraries usually required by ns-3 modules
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    g++ \
    python3 \
    python3-dev \
    python3-pip \
    python3-setuptools \
    cmake \
    git \
    wget \
    unzip \
    mercurial \
    qt5-default \
    openmpi-bin \
    openmpi-common \
    openmpi-doc \
    libopenmpi-dev \
    autoconf \
    cvs \
    bzr \
    unrar \
    gdb \
    valgrind \
    uncrustify \
    doxygen \
    graphviz \
    imagemagick \
    python3-sphinx \
    dia \
    tcpdump \
    libxml2 \
    libxml2-dev \
    libc6-dev \
    libclang-dev \
    llvm-dev \
    automake \
    libgtk2.0-0 \
    libgtk2.0-dev \
    libgsl-dev \
    libsqlite3-dev \
    flex \
    bison \
    iputils-ping \
    net-tools \
    && rm -rf /var/lib/apt/lists/*

# Install Python packages often required by V2X frameworks
RUN pip3 install --no-cache-dir \
    numpy \
    scipy \
    pandas \
    matplotlib \
    kiwisolver \
    cycler \
    pyparsing \
    python-dateutil

# Set up workspace directory
WORKDIR /workspace

# Add an alias to easily reach the SUMO container if needed
RUN echo "export SUMO_HOST=sumo" >> /root/.bashrc
