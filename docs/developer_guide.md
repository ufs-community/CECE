# CECE Developer Guide

## Project Overview
CECE (Community Emissions Computing Engine) is a C++20 emissions compute component designed for high performance using Kokkos and ESMF.

## Core Architecture & Philosophy
CECE is designed as a modular, performance-portable emissions framework. Key components include:

*   **StackingEngine:** Manages the aggregation of emission layers using sophisticated hierarchy-based processing, kernel fusion optimization, and advanced temporal/spatial scaling. See the [Stacking Engine Documentation](stacking_engine.md) for comprehensive technical details.
*   **Vertical Distribution:** Multiple algorithms for mapping 2D emissions to 3D atmospheric grids with strict mass conservation. See the [Vertical Distribution Documentation](vertical_distribution.md) for complete algorithm descriptions and usage examples.
*   **PhysicsFactory:** A self-registration registry for physics schemes. New schemes should inherit from `BasePhysicsScheme` and use the `PhysicsRegistration<T>` pattern.
*   **Internal State:** Persisted via `CeceInternalData` to maintain field handles and metadata across ESMF phases, avoiding redundant lookups.
*   **TIDE Integration:** High-performance data ingestion for external emission inventories via the TIDE (Temporal Interpolation & Data Extraction) library with smart caching and regridding capabilities.

## Global Coding Standards
*   **Language:** C++20 and Fortran 2008+.
*   **Style:** Google C++ Style Guide for C++.
*   **Namespace:** `cece::` (defined in `include/cece/cece.hpp`).
*   **Documentation:** Doxygen format (`/** ... */`) required for all public APIs.
*   **Memory:** Use `Kokkos::View` for data. Avoid raw pointers.
*   **ESMF:** Use `ESMC_` C API for C++ bridge. Wrap data in `Kokkos::View` with `Kokkos::MemoryTraits<Kokkos::Unmanaged>`.
*   **Performance Portability:**
    *   All compute kernels MUST use Kokkos parallel primitives (`parallel_for`, `parallel_reduce`).
    *   Avoid hardware-specific code (e.g., Cuda intrinsics).
    *   Use `Kokkos::DefaultExecutionSpace` for dispatch.
    *   Strictly avoid `std::cout` or blocking I/O inside kernels.

## Physics Scheme Development
When implementing or modifying physics schemes:
1.  **Configurability:** NEVER hardcode physical constants or tuning factors. All parameters must be read from the YAML `options` block in `Initialize`.
2.  **BasePhysicsScheme Helpers:** Use provided scientist-friendly helpers:
    *   `ResolveImport(name, state)`: Retrieve input fields.
    *   `ResolveExport(name, state)`: Retrieve output fields.
    *   `MarkModified(name, state)`: Signal that an export field has been updated.
    *   `ResolveDiagnostic(name, nx, ny, nz)`: Register/retrieve diagnostic fields.
3.  **Optimization:** Use Horner's Method for evaluating polynomials (e.g., Schmidt numbers, SST scaling) to minimize floating-point operations.

## Vertical Distribution
CECE supports multiple vertical distribution methods for mapping 2D emissions to 3D grids:
*   **SINGLE:** Place all emissions in a single specific layer.
*   **RANGE:** Distribute evenly over a range of layer indices.
*   **PRESSURE:** Distribute based on a pressure range (Pa).
*   **HEIGHT:** Distribute based on a height range (m).
*   **PBL:** Distribute within the Planetary Boundary Layer.
*   **Conservation:** All methods ensure strict column mass conservation.

For complete algorithm descriptions, performance characteristics, and usage examples, see the [Vertical Distribution Documentation](vertical_distribution.md).

## Configuration System
CECE uses a comprehensive YAML configuration system that supports:
- Hierarchical emission layer processing with categories and priorities
- Temporal scaling profiles (diurnal, weekly, seasonal)
- Environmental dependencies and dynamic scaling factors
- Multiple vertical distribution algorithms
- TIDE data stream integration with smart caching
- Physics scheme configuration and parameter tuning

For complete configuration reference with all available options and examples, see the [Configuration Documentation](configuration.md).

## Development Environment
The required development environment is the `jcsda/docker-gnu-openmpi-dev:1.9` Docker container. This container provides the necessary compilers, MPI, and ESMF dependencies.

### ⚠️ IMPORTANT: No Mocking Policy
**Mocking ESMF or NUOPC is strictly forbidden.** All development and verification must be performed using real ESMF dependencies within the JCSDA Docker environment to ensure real-world parity and stability.

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
    cmake ..
    make -j4
    ```

 3.  **Run Example Driver:**
    To see CECE in action with external data (ESMF fields):
    ```bash
    ./example_driver
    ```

4.  **Test:**
    ```bash
    ctest --output-on-failure
    ```

### Python Dependencies
The physics scheme generator and other scripts require `jinja2`, `pyyaml`, and `pytest`.
```bash
python3 -m pip install jinja2 pyyaml pytest
```

## Documentation Resources
*   **ESMF User Guide:** [https://earthsystemmodeling.org/docs/release/latest/ESMF_usrdoc](https://earthsystemmodeling.org/docs/release/latest/ESMF_usrdoc)
*   **ESMF Reference Manual:** [https://earthsystemmodeling.org/docs/release/latest/ESMF_refdoc/](https://earthsystemmodeling.org/docs/release/latest/ESMF_refdoc/)
*   **NUOPC Reference Manual:** [https://earthsystemmodeling.org/docs/release/latest/NUOPC_refdoc](https://earthsystemmodeling.org/docs/release/latest/NUOPC_refdoc)
*   **TIDE Documentation:** Located at `src/io/tide` in the CECE repository
*   **Fortran Standards:** [Fortran 2008 Standard (ISO/IEC 1539-1:2010)](https://www.iso.org/standard/44473.html)
