# Welcome to ACES

**ACES** (Atmospheric Chemistry Emission System) is a high-performance C++ component designed for calculating atmospheric emissions. It leverages the **Kokkos** programming model for performance portability across CPUs and GPUs and integrates seamlessly with the **ESMF** (Earth System Modeling Framework).

## Key Features

- **Performance Portability**: Write once, run anywhere. ACES uses Kokkos to target NVIDIA GPUs, multi-core CPUs (OpenMP), and more without changing the source code.
- **Hybrid Data Ingestion**: Pull live meteorological data (temperature, wind speed, etc.) from ESMF while simultaneously reading static emission inventories from disk using CDEPS-inline.
- **Modular Physics Engine**: Easily extend ACES with new physics schemes. Supports both native C++ (Kokkos) and legacy Fortran plugins.
- **Flexible Stacking Engine**: Combine multiple emission layers using prioritized categories and hierarchy levels. Apply geographical masks and multiple scale factors per layer.
- **Built-in Diagnostics**: Integrated diagnostic manager for registering and writing intermediate variables to NetCDF files.

## Architecture Overview

ACES operates as an ESMF Grid Component. It follows the standard NUOPC/ESMF lifecycle:

1.  **Initialize**: Parses YAML configuration, instantiates physics schemes, and initializes CDEPS-inline.
2.  **Run**:
    - Discovers grid dimensions from ESMF fields.
    - Ingests data from ESMF and CDEPS.
    - Executes the Stacking Engine to process base emissions.
    - Runs active Physics Extensions.
    - Writes diagnostics to disk.
    - Synchronizes computed emissions back to the ESMF host state.
3.  **Finalize**: Cleans up resources and finalizes Kokkos/CDEPS.

## Get Started

Check out the [User's Guide](users-guide.md) to learn how to build and run ACES, explore the [Migration Examples](migration_examples.md) to see side-by-side comparisons with HEMCO, or dive into the [Tutorial](tutorial.md) to start writing your own physics schemes.
