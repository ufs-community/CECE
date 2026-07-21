# Welcome to CECE

**CECE** (Community Emissions Computing Engine) is a high-performance C++ component designed for calculating atmospheric emissions. It leverages the **Kokkos** programming model for performance portability across CPUs and GPUs. CECE can run as a **standalone executable** using the HELM library suite (AMIO, AXIS, TICK, HALO, DAGR) or as a **NUOPC-compliant ESMF component** within a coupled Earth system model.

## Key Features

- **Performance Portability**: Write once, run anywhere. CECE uses Kokkos to target NVIDIA GPUs, multi-core CPUs (OpenMP), and more without changing the source code.
- **Hybrid Data Ingestion**: Read static emission inventories from NetCDF via AMIO with AXIS regridding, while also accepting live meteorological fields from a coupled model or configuration-driven inputs.
- **Modular Physics Engine**: Easily extend CECE with new physics schemes. Supports both native C++ (Kokkos) and legacy Fortran plugins.
- **Flexible Stacking Engine**: Combine multiple emission layers using prioritized categories and hierarchy levels. Apply geographical masks and multiple scale factors per layer.
- **Built-in Diagnostics**: Integrated diagnostic manager for registering and writing intermediate variables to NetCDF files.
- **Python Bindings**: High-level Python API via pybind11 with zero-copy NumPy data transfer, automatic GIL release during computation, and a clean exception hierarchy.

## Architecture Overview

CECE supports two execution modes:

### Standalone Mode (HELM-based)

The standalone driver (`src/main.cpp`) orchestrates the full simulation lifecycle using HELM libraries:
- **TICK** — Gregorian calendar and simulation clock
- **HALO** — MPI communicator management
- **AXIS** — Named grid generation and coordinate handling
- **AMIO** — Asynchronous NetCDF I/O with prefetch and staging
- **DAGR** — Directed acyclic graph pipeline orchestration

### Coupled Mode (NUOPC/ESMF)

A Fortran NUOPC cap (`src/driver/nuopc/cece_cap.F90`) wraps CECE as an ESMF Grid Component for use in coupled Earth system models. In this mode, the host model provides the clock, grid, and meteorological import fields.

### Lifecycle

Both modes follow the same core lifecycle:

1.  **Initialize**: Parses YAML configuration, instantiates physics schemes, and initializes the data ingestion pipeline.
2.  **Run**:
    - Ingests data from AMIO data streams with AXIS regridding.
    - Executes the Stacking Engine with fused kernel optimization.
    - Runs active Physics Extensions (MEGAN, sea salt, dust, etc.).
    - Writes output and diagnostics to disk.
    - Synchronizes computed emissions back to the host state.
3.  **Finalize**: Cleans up resources and finalizes Kokkos.

For comprehensive technical details about the Stacking Engine algorithms and performance optimizations, see the [Stacking Engine Documentation](stacking_engine.md).

## Get Started

Check out the [User's Guide](users-guide.md) to learn how to build and run CECE, explore the [Migration Examples](migration_examples.md) to see side-by-side comparisons with HEMCO, dive into the [Tutorial](tutorial.md) to start writing your own physics schemes, or see the [Python Bindings](python_bindings.md) guide for using CECE from Python.
