# Core Concepts

This page explains the fundamental concepts and architecture of CECE.

## The Stacking Engine

The heart of CECE is the **Stacking Engine**, which calculates final emission fields by combining multiple data layers. It is designed to mimic and extend the priority-based logic of the HEMCO (Harmonized Emissions Component) while providing significant performance optimizations for modern HPC architectures.

For comprehensive technical details about the Stacking Engine implementation, algorithms, and performance characteristics, see the [Stacking Engine Documentation](stacking_engine.md).

### Key Features

- **Hierarchy-Based Processing**: Layers are organized by categories and numerical priorities
- **Kernel Fusion Optimization**: Single optimized compute kernel per species for maximum performance
- **Flexible Operations**: Support for add and replace operations with dynamic field-based scaling
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

## The CECE Lifecycle

CECE supports two execution modes that share the same core compute engine:

### Standalone Mode

The standalone driver (`src/main.cpp`) manages the full simulation using the HELM library suite. It reads the YAML configuration, sets up the clock (TICK), grid (AXIS NamedGridRegistry), MPI environment (HALO), and orchestrates data I/O through a DAGR pipeline with AMIO reads and AXIS regridding.

### Coupled Mode (NUOPC/ESMF)

A Fortran NUOPC cap wraps CECE as an ESMF Grid Component. The host model provides the clock, grid, and meteorological import fields. CECE advertises its import/export fields following the NUOPC Initialize Phase Definitions (IPDv00).

### Shared Lifecycle Phases

Both modes follow the same three-phase lifecycle:

### 1. Initialize Phase
-   **Configuration Parsing**: Reads the YAML configuration and validates the stacking plan.
-   **Physics Instantiation**: Schemes listed in `physics_schemes` are created and their `Initialize` methods are called.
-   **Clock Construction**: Per-component scheduling intervals are compiled into a CeceClock.
-   **Stacking Engine Compilation**: Emission layers are pre-compiled and sorted by hierarchy.

### 2. Run Phase
-   **Data Ingestion**: External emission inventories are read from NetCDF via AMIO, regridded by AXIS, and injected into the import state.
-   **Stacking Execution**: The fused Kokkos kernels are launched to compute the base emissions.
-   **Physics Execution**: Active physics schemes (like Sea Salt or MEGAN) are executed to modify or generate new emissions.
-   **Diagnostics**: Intermediate variables are captured by the `CeceDiagnosticManager` and written to NetCDF files if configured.
-   **State Synchronization**: Final emissions are synced to the host for output or coupled exchange.

### 3. Finalize Phase
-   Resources are released, and Kokkos is finalized.

---

## Performance Portability with Kokkos

CECE uses the **Kokkos** programming model to achieve performance portability. By writing algorithms using Kokkos `parallel_for` and `View` abstractions, the same C++ code can be compiled to run on:
-   **NVIDIA GPUs** (via CUDA)
-   **AMD GPUs** (via HIP)
-   **Multi-core CPUs** (via OpenMP or C++ Threads)
-   **ARM/x86 vector units** (via SIMD abstractions)

This ensures that CECE remains efficient on present and future high-performance computing architectures without requiring separate codebases for different hardware.
