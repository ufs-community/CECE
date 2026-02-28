# User's Guide

This guide covers the prerequisites, build process, and execution of the ACES component.

## Prerequisites

To build ACES, you need the following dependencies:

- **C++17 Compiler** (GCC 9+, Clang 10+)
- **CMake** (3.20+)
- **Kokkos**
- **ESMF** (Earth System Modeling Framework)
- **MPI** (OpenMPI, MPICH, etc.)
- **yaml-cpp**
- **CDEPS** (inline version)
- **NetCDF** (C and Fortran interfaces)

### Docker Environment (Recommended)

The easiest way to get started is using the JCSDA development container, which comes with all dependencies pre-installed.

1.  **Run the Setup Script**:
    ```bash
    ./setup.sh
    ```
2.  **Activate the Environment**:
    Inside the container, ensure the Spack environment is active:
    ```bash
    source /opt/spack-environment/activate.sh
    ```

## Building ACES

### Standard Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Build Options

| Option | Description | Default |
| --- | --- | --- |
| `ACES_HAS_FORTRAN` | Enable Fortran bridge support | `ON` |
| `Kokkos_ENABLE_CUDA` | Enable NVIDIA GPU support | `OFF` |
| `Kokkos_ENABLE_OPENMP`| Enable multi-core CPU support | `OFF` |

Example for targeting NVIDIA GPUs:
```bash
cmake .. -DKokkos_ENABLE_CUDA=ON -DKokkos_ARCH_AMPERE80=ON
```

### Local Development (Mock ESMF)

If you don't have ESMF installed and want to test the C++ logic, you can build with a mock implementation:
```bash
cmake .. -DACES_USE_MOCK_ESMF=ON
```

## Running the Example

ACES includes an `example_driver` that demonstrates how the component is initialized and run within an ESMF-like environment.

1.  **Configure**: Edit `aces_config.yaml` to specify your species and layers.
2.  **Run**:
    ```bash
    ./example_driver
    ```

## Testing

Run the test suite using CTest:
```bash
ctest --output-on-failure
```
