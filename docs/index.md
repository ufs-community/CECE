# Welcome to ACES

**ACES** (Atmospheric Chemistry Emission System) is a high-performance C++ component designed for calculating atmospheric emissions. It leverages the **Kokkos** programming model for performance portability across CPUs and GPUs and integrates seamlessly with the **ESMF** (Earth System Modeling Framework).

## Key Features

- **Performance Portability**: Write once, run anywhere. ACES uses Kokkos to target NVIDIA GPUs, multi-core CPUs (OpenMP), and more without changing the source code.
- **Hybrid Data Ingestion**: Pull live meteorological data (temperature, wind speed, etc.) from ESMF while simultaneously reading static emission inventories from disk using TIDE.
- **Modular Physics Engine**: Easily extend ACES with new physics schemes. Supports both native C++ (Kokkos) and legacy Fortran plugins.
- **Flexible Stacking Engine**: Combine multiple emission layers using prioritized categories and hierarchy levels. Apply geographical masks and multiple scale factors per layer.
- **Built-in Diagnostics**: Integrated diagnostic manager for registering and writing intermediate variables to NetCDF files.

## Architecture Overview

ACES operates as an ESMF Grid Component with a sophisticated **Stacking Engine** at its core. The Stacking Engine combines multiple emission data layers using hierarchical processing, kernel fusion optimization, and advanced temporal/spatial scaling. It follows the standard NUOPC/ESMF lifecycle:

1.  **Initialize**: Parses YAML configuration, instantiates physics schemes, and initializes TIDE.
2.  **Run**:
    - Discovers grid dimensions from ESMF fields.
    - Ingests data from ESMF and TIDE data streams.
    - Executes the Stacking Engine with fused kernel optimization.
    - Runs active Physics Extensions (MEGAN, sea salt, dust, etc.).
    - Writes diagnostics to disk.
    - Synchronizes computed emissions back to the ESMF host state.
3.  **Finalize**: Cleans up resources and finalizes Kokkos/TIDE.

For comprehensive technical details about the Stacking Engine algorithms and performance optimizations, see the [Stacking Engine Documentation](stacking_engine.md).

## Get Started

Check out the [User's Guide](users-guide.md) to learn how to build and run ACES, explore the [Migration Examples](migration_examples.md) to see side-by-side comparisons with HEMCO, or dive into the [Tutorial](tutorial.md) to start writing your own physics schemes.
