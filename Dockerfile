# syntax=docker/dockerfile:1
# Dockerfile
# CECE Development and Verification Environment (optionally with ESMF & NUOPC)
# Based on Ubuntu 24.04 with GCC-13, OpenMPI, NetCDF-C, NetCDF-Fortran, Kokkos 5.1.1, RapidCheck, and KokkosKernels
FROM ubuntu:24.04

# Set to ON to build ESMF (with NUOPC support)
ARG BUILD_ESMF=OFF

# Prevent interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install Core HPC, C++20 Toolchain, and NetCDF-C/Fortran
RUN apt-get update && apt-get install -y \
    build-essential \
    g++-13 \
    gcc-13 \
    gfortran-13 \
    cmake \
    ninja-build \
    git \
    wget \
    curl \
    openmpi-bin \
    libopenmpi-dev \
    libnetcdf-mpi-dev \
    libnetcdff-dev \
    libblosc-dev \
    libbz2-dev \
    libxml2-dev \
    libgtest-dev \
    python3 \
    python3-venv \
    vim \
    less \
    tree \
    && rm -rf /var/lib/apt/lists/*

# Set GCC-13 as the default compiler (C, C++, and Fortran)
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && update-alternatives --install /usr/bin/gfortran gfortran /usr/bin/gfortran-13 100

# 2. Compile and Install Google Test globally
RUN cd /usr/src/gtest \
    && cmake CMakeLists.txt \
    && make \
    && cp lib/*.a /usr/lib/ \
    && mkdir -p /usr/local/lib/gtest/ \
    && ln -s /usr/lib/libgtest.a /usr/local/lib/gtest/libgtest.a \
    && ln -s /usr/lib/libgtest_main.a /usr/local/lib/gtest/libgtest_main.a

# 3. Clone and install Kokkos 5.1.1 (C++20 minimum, OpenMP backend)
RUN git clone -b 5.1.1 https://github.com/kokkos/kokkos.git /tmp/kokkos \
    && cd /tmp/kokkos \
    && cmake -B build \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_CXX_STANDARD=20 \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DKokkos_ENABLE_OPENMP=ON \
      -DKokkos_ENABLE_SERIAL=ON \
    && cmake --build build --parallel $(nproc) \
    && cmake --install build \
    && rm -rf /tmp/kokkos

# 4. Clone and install RapidCheck (property-based testing)
RUN git clone https://github.com/emil-e/rapidcheck.git /tmp/rapidcheck \
    && cd /tmp/rapidcheck \
    && cmake -B build \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DRC_ENABLE_GTEST=ON \
      -DRC_INSTALL_ALL_EXTRAS=ON \
    && cmake --build build --parallel $(nproc) \
    && cmake --install build \
    && rm -rf /tmp/rapidcheck

# 5. Clone and install KokkosKernels
RUN git clone --depth 1 https://github.com/kokkos/kokkos-kernels.git /tmp/kokkos-kernels \
    && cd /tmp/kokkos-kernels \
    && cmake -B build \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_CXX_STANDARD=20 \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DKokkosKernels_ENABLE_ALL_COMPONENTS=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_SPARSE=ON \
      -DKokkosKernels_ENABLE_COMPONENT_BLAS=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_GRAPH=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_BATCHED=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_LAPACK=OFF \
      -DKokkosKernels_ENABLE_COMPONENT_ODE=OFF \
      -DKokkosKernels_ADD_DEFAULT_ETI=OFF \
    && cmake --build build --parallel $(nproc) \
    && cmake --install build \
    && rm -rf /tmp/kokkos-kernels

# 6. Clone and install ESMF (with NUOPC support), if BUILD_ESMF=ON
RUN if [ "$BUILD_ESMF" = "ON" ]; then \
      git clone --depth 1 -b v8.9.1 https://github.com/esmf-org/esmf.git /tmp/esmf \
      && cd /tmp/esmf \
      && export ESMF_DIR=/tmp/esmf \
      && export ESMF_COMPILER=gfortran \
      && export ESMF_COMM=openmpi \
      && export ESMF_NETCDF=nc-config \
      && export ESMF_NETCDF_LIBS="-lnetcdf -lnetcdff" \
      && export ESMF_INSTALL_PREFIX=/usr/local \
      && make -j$(nproc) \
      && make install \
      && rm -rf /tmp/esmf; \
    else \
      echo "Skipping ESMF build (BUILD_ESMF=$BUILD_ESMF)"; \
    fi

# Set standard environment variables
# ESMFMKFILE is always set even if ESMF is not built
ENV ESMFMKFILE=/usr/local/lib/libO/Linux.gfortran.32.openmpi.default/esmf.mk
ENV OMPI_ALLOW_RUN_AS_ROOT=1
ENV OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

# Workspace Setup
WORKDIR /work
CMD ["/bin/bash"]
