# Core Concepts

This page explains the fundamental concepts and architecture of CECE.

## The Stacking Engine

The heart of CECE is the **Stacking Engine**, which calculates final emission fields by combining multiple data layers. It is designed to mimic and extend the priority-based logic of the HEMCO (Harmonized Emissions Component) while providing significant performance optimizations for modern HPC architectures.

For comprehensive technical details about the Stacking Engine implementation, algorithms, and performance characteristics, see the [Stacking Engine Documentation](stacking_engine.md).

### Key Features

- **Hierarchy-Based Processing**: Layers are organized by categories and numerical priorities
- **Kernel Fusion Optimization**: Single optimized compute kernel per species for maximum performance
- **Flexible Operations**: Support for add, multiply, replace, and set operations
- **Advanced Scaling**: Multiple simultaneous scale factors (temporal, spatial, field-based)
- **Vertical Distribution**: Multiple algorithms for 2D→3D emission mapping
- **Complete Provenance**: Full scientific traceability of emission calculations

### Quick Example

```yaml
species:
  co:
    - field: "global_co_inventory"
      category: "anthropogenic"
      hierarchy: 1
      operation: "add"
      scale: 1.0
    - field: "regional_co_override"
      category: "anthropogenic"
      hierarchy: 10         # Higher priority replaces base
      operation: "replace"
      mask: "regional_mask"
```

---

## The CECE/ESMF Lifecycle

CECE is implemented as a NUOPC-compliant ESMF component. It follows a standard lifecycle:

### 1. Initialize Phase
-   **Configuration Parsing**: Reads the YAML configuration and validates the stacking plan.
-   **Physics Instantiation**: Schemes listed in `physics_schemes` are created and their `Initialize` methods are called.
-   **Field Advertising**: CECE "advertises" the fields it expects to import (e.g., meteorology) and the fields it will export (calculated emissions) to the ESMF framework.

### 2. Run Phase
-   **Dimension Discovery**: CECE dynamically determines the grid dimensions from the ESMF fields at runtime.
-   **Data Ingestion**:
    -   Live meteorological data is pulled from the ESMF Import State.
    -   Static emission inventories are read from disk using the TIDE engine.
-   **Stacking Execution**: The fused Kokkos kernels are launched to compute the base emissions.
-   **Physics Execution**: Active physics schemes (like Sea Salt or MEGAN) are executed to modify or generate new emissions.
-   **Diagnostics**: Intermediate variables are captured by the `CeceDiagnosticManager` and written to NetCDF files if configured.
-   **State Synchronization**: Final emissions are deep-copied back to the ESMF Export State.

### 3. Finalize Phase
-   Resources are released, and Kokkos/TIDE are finalized.

---

## Performance Portability with Kokkos

CECE uses the **Kokkos** programming model to achieve performance portability. By writing algorithms using Kokkos `parallel_for` and `View` abstractions, the same C++ code can be compiled to run on:
-   **NVIDIA GPUs** (via CUDA)
-   **AMD GPUs** (via HIP)
-   **Multi-core CPUs** (via OpenMP or C++ Threads)
-   **ARM/x86 vector units** (via SIMD abstractions)

This ensures that CECE remains efficient on present and future high-performance computing architectures without requiring separate codebases for different hardware.
