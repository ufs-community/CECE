# ACES Developer Guide

## Project Overview
ACES (Accelerated Component for Emission System) is a C++17 emissions compute component designed for high performance using Kokkos and ESMF.

## Global Coding Standards
*   **Language:** C++17.
*   **Style:** Google C++ Style Guide.
*   **Namespace:** `aces::` (defined in `include/aces/aces.hpp`).
*   **Documentation:** Doxygen format (`/** ... */`) required for all public APIs.
*   **Memory:** Use `Kokkos::View` for data. Avoid raw pointers.
*   **ESMF:** Use `ESMC_` C API. Wrap data in `Kokkos::View` with `Kokkos::MemoryTraits<Kokkos::Unmanaged>`.

## Development Environment
The recommended development environment is the `jcsda/docker-gnu-openmpi-dev:1.9` Docker container. This container provides pre-installed compilers, MPI, and ESMF.

### Quick Start
1.  **Run the Setup Script:**
    Execute the provided `setup.sh` script to pull the Docker image and drop into a shell.
    ```bash
    ./setup.sh
    ```
    If you encounter Docker overlayfs issues or need to fix the environment, run:
    ```bash
    ./scripts/fix_docker_and_setup.sh
    ```

2.  **Build:**
    Inside the container:
    ```bash
    mkdir build && cd build
    cmake .. -DACES_USE_MOCK_ESMF=OFF
    make -j4
    ```

 3.  **Run Example Driver:**
    To see ACES in action with external data (ESMF fields):
    ```bash
    ./example_driver
    ```

3.  **Test:**
    ```bash
    ctest --output-on-failure
    ```

## Local Development (Without Docker)
If you cannot use Docker, the project can build using a Mock ESMF implementation.
```bash
cmake -S . -B build -DACES_USE_MOCK_ESMF=ON
cmake --build build
ctest --test-dir build
```
