# Core Concepts

This page explains the fundamental concepts and architecture of ACES.

## The Stacking Engine

The heart of ACES is the **Stacking Engine**, which calculates final emission fields by combining multiple data layers. It is designed to mimic the priority-based logic of the HEMCO (Harmonized Emissions Component).

### Categories and Hierarchies

Layers are organized into **Categories** (e.g., `anthropogenic`, `biomass_burning`). Within each category, layers are assigned a **Hierarchy** level.

-   **Priority**: When multiple layers provide data for the same species and location, the layer with the **highest hierarchy** takes precedence.
-   **Operations**:
    -   `add`: The layer's emissions are added to the accumulated sum for that species.
    -   `replace`: The layer's emissions overwrite the accumulated sum for that species. This is typically used for regional overrides (e.g., a regional inventory replacing a global one).

### Masks and Scale Factors

Each layer can be modified by:
-   **Geographical Masks**: 2D or 3D fields that restrict where a layer's emissions are applied.
-   **Scale Factors**: Multipliers applied to the base emission field. These can be static values or dynamic fields (like temperature or wind speed) pulled from ESMF or TIDE.

### Optimized Fused Kernels

To ensure high performance on GPUs and CPUs, ACES uses **Kernel Fusion**. Instead of processing each layer and operation in separate steps, the Stacking Engine analyzes the hierarchy and generates a single, fused Kokkos kernel for each species. This significantly reduces memory bandwidth and kernel launch overhead.

---

## The ACES/ESMF Lifecycle

ACES is implemented as a NUOPC-compliant ESMF component. It follows a standard lifecycle:

### 1. Initialize Phase
-   **Configuration Parsing**: Reads the YAML configuration and validates the stacking plan.
-   **Physics Instantiation**: Schemes listed in `physics_schemes` are created and their `Initialize` methods are called.
-   **Field Advertising**: ACES "advertises" the fields it expects to import (e.g., meteorology) and the fields it will export (calculated emissions) to the ESMF framework.

### 2. Run Phase
-   **Dimension Discovery**: ACES dynamically determines the grid dimensions from the ESMF fields at runtime.
-   **Data Ingestion**:
    -   Live meteorological data is pulled from the ESMF Import State.
    -   Static emission inventories are read from disk using the TIDE engine.
-   **Stacking Execution**: The fused Kokkos kernels are launched to compute the base emissions.
-   **Physics Execution**: Active physics schemes (like Sea Salt or MEGAN) are executed to modify or generate new emissions.
-   **Diagnostics**: Intermediate variables are captured by the `AcesDiagnosticManager` and written to NetCDF files if configured.
-   **State Synchronization**: Final emissions are deep-copied back to the ESMF Export State.

### 3. Finalize Phase
-   Resources are released, and Kokkos/TIDE are finalized.

---

## Performance Portability with Kokkos

ACES uses the **Kokkos** programming model to achieve performance portability. By writing algorithms using Kokkos `parallel_for` and `View` abstractions, the same C++ code can be compiled to run on:
-   **NVIDIA GPUs** (via CUDA)
-   **AMD GPUs** (via HIP)
-   **Multi-core CPUs** (via OpenMP or C++ Threads)
-   **ARM/x86 vector units** (via SIMD abstractions)

This ensures that ACES remains efficient on present and future high-performance computing architectures without requiring separate codebases for different hardware.
