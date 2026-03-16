# Developer Guide

This guide provides an overview of the ACES architecture, component interactions, and best practices for extending the system.

## ACES Architecture Overview

ACES (Accelerated Component for Emission System) is a modular, performance-portable emissions compute component designed for Earth System Models. The architecture consists of several key components:

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    NUOPC Driver Layer                        │
│  (Fortran: aces_cap.F90, standalone_nuopc/single_driver.F90)│
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│              C++ Bridge Layer (aces_core_*.cpp)              │
│  - Advertise Phase: Declare fields                           │
│  - Realize Phase: Create fields                              │
│  - Initialize Phase (P1, P2): Setup state                    │
│  - Run Phase: Execute computation                            │
│  - Finalize Phase: Cleanup                                   │
└────────────────────┬────────────────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        ▼            ▼            ▼
    ┌────────┐  ┌──────────┐  ┌──────────────┐
    │ Config │  │  CDEPS   │  │ StackEngine  │
    │ Parser │  │ Ingestor │  │ + Physics    │
    └────────┘  └──────────┘  └──────────────┘
```

### Core Components

#### 1. NUOPC Cap (Fortran)
- **File**: `src/aces_cap.F90`
- **Purpose**: Fortran interface to ESMF/NUOPC
- **Responsibilities**:
  - Register ACES as a NUOPC model component
  - Manage NUOPC phase transitions
  - Bridge between Fortran ESMF and C++ core
  - Handle CDEPS initialization

#### 2. C++ Bridge Layer
- **Files**: `src/aces_core_*.cpp`
- **Purpose**: Implement NUOPC phase logic in C++
- **Phases**:
  - `aces_core_advertise.cpp`: Declare import/export fields
  - `aces_core_realize.cpp`: Create and allocate fields
  - `aces_core_initialize_p1.cpp`: Initialize Kokkos, config, physics
  - `aces_core_initialize_p2.cpp`: Initialize CDEPS, bind fields
  - `aces_core_run.cpp`: Execute emission computation
  - `aces_core_finalize.cpp`: Cleanup resources

#### 3. Configuration Parser
- **File**: `src/aces_config_parser.cpp`
- **Purpose**: Parse YAML configuration files
- **Responsibilities**:
  - Parse species definitions
  - Parse layer configurations
  - Parse physics scheme options
  - Validate configuration

#### 4. CDEPS Data Ingestor
- **File**: `src/io/aces_data_ingestor.cpp`
- **Purpose**: Hybrid data ingestion from CDEPS and ESMF
- **Responsibilities**:
  - Initialize CDEPS with streams configuration
  - Resolve fields with priority: CDEPS > ESMF
  - Cache field handles for performance
  - Wrap data in Kokkos::View

#### 5. StackingEngine
- **File**: `src/aces_stacking_engine.cpp`
- **Purpose**: Aggregate emission layers with priorities, scales, and masks
- **Responsibilities**:
  - Apply layer hierarchy prioritization
  - Apply scale factors (temporal, spatial)
  - Apply masks (geographical, volumetric)
  - Perform vertical distribution (SINGLE, RANGE, PRESSURE, HEIGHT, PBL)
  - Ensure mass conservation

#### 6. Physics Factory
- **File**: `src/aces_physics_factory.cpp`
- **Purpose**: Self-registration registry for physics schemes
- **Responsibilities**:
  - Register physics schemes at compile time
  - Instantiate schemes from configuration
  - Manage scheme lifecycle

#### 7. Physics Schemes
- **Files**: `src/physics/aces_*.cpp`
- **Purpose**: Compute or modify emissions based on meteorology
- **Examples**:
  - DMS (Dimethyl Sulfide)
  - Dust
  - Lightning NOx
  - MEGAN (biogenic)
  - Sea Salt
  - Soil NOx
  - Volcano

## Component Interactions and Data Flow

### Initialization Sequence

```
1. NUOPC Driver creates ACES component
2. ACES_SetServices registers phase methods
3. Advertise Phase:
   - Parse YAML config
   - Declare import fields (meteorology)
   - Declare export fields (emissions)
4. Realize Phase:
   - Create export fields
   - Verify import fields exist
5. Initialize Phase P1:
   - Initialize Kokkos
   - Create internal state (AcesInternalData)
   - Instantiate physics schemes
6. Initialize Phase P2:
   - Initialize CDEPS
   - Bind frequently accessed fields
   - Prepare for run loop
```

### Run Loop Sequence

```
For each time step:
1. NUOPC Driver advances clock
2. Run Phase:
   a. Advance CDEPS to current time
   b. Extract current hour and day-of-week
   c. Execute StackingEngine:
      - Aggregate layers with priorities
      - Apply scale factors (temporal, spatial)
      - Apply masks
      - Perform vertical distribution
   d. Execute physics schemes in registration order:
      - Each scheme reads import fields
      - Each scheme modifies export fields
   e. Synchronize device to host (for GPU)
   f. Write output (if standalone mode)
3. NUOPC Driver continues to next time step
```

### Data Flow

```
External Data (NetCDF)
        │
        ▼
    CDEPS_Inline
        │
        ├─────────────────┐
        │                 │
        ▼                 ▼
   CDEPS Fields    ESMF ImportState
        │                 │
        └────────┬────────┘
                 │
                 ▼
        Field Resolver (Priority: CDEPS > ESMF)
                 │
                 ▼
        StackingEngine (Layer Aggregation)
                 │
                 ├─ Base Emissions
                 ├─ Scale Factors
                 ├─ Masks
                 └─ Vertical Distribution
                 │
                 ▼
        Physics Schemes (DMS, Dust, etc.)
                 │
                 ▼
        ESMF ExportState
                 │
                 ├─ To Coupled Model
                 └─ To Output Files (standalone)
```

## NUOPC Phase Handling Patterns

### Phase Dependency Declaration

ACES declares phase dependencies using NUOPC_CompAttributeSet:

```fortran
! In ACES_SetServices
call NUOPC_CompAttributeSet(comp, "InitializePhaseMap", &
    "IPDv00p1=1,IPDv00p2=2", rc=rc)
```

This ensures:
- Phase 1 (IPDv00p1): Core initialization
- Phase 2 (IPDv00p2): CDEPS and field binding

### Multi-Phase Initialization

ACES uses two-phase initialization to separate concerns:

**Phase 1 (aces_core_initialize_p1)**:
- Initialize Kokkos
- Parse configuration
- Create internal state
- Instantiate physics schemes

**Phase 2 (aces_core_initialize_p2)**:
- Initialize CDEPS
- Bind frequently accessed fields
- Prepare for run loop

This separation allows other components to initialize in between phases if needed.

### Error Handling in Phases

Each phase returns an error code (rc):
- `ESMF_SUCCESS` (0): Phase completed successfully
- Non-zero: Phase failed, error message logged

ACES logs all errors with descriptive messages including:
- Phase name
- Error location (file, line)
- Error description
- Suggested corrective action

## Coding Standards and Best Practices

### C++ Style Guide

ACES follows the Google C++ Style Guide with these additions:

**Namespace**:
```cpp
namespace aces {
    // All ACES code in aces namespace
}
```

**Documentation**:
```cpp
/**
 * @brief Brief description of function
 * @param param1 Description of param1
 * @return Description of return value
 */
void MyFunction(int param1);
```

**Memory Management**:
```cpp
// Use Kokkos::View for data
Kokkos::View<double***> data("data", nx, ny, nz);

// Avoid raw pointers
// double* ptr = new double[size];  // BAD
```

**ESMF Integration**:
```cpp
// Use ESMC_ C API
ESMC_Field field = ESMC_FieldCreate(...);

// Wrap in Kokkos::View with Unmanaged trait
auto view = Kokkos::View<double***, Kokkos::MemoryTraits<Kokkos::Unmanaged>>(
    data_ptr, nx, ny, nz);
```

### Performance Portability with Kokkos

All compute kernels must use Kokkos parallel primitives:

```cpp
// Parallel for loop
Kokkos::parallel_for("kernel_name",
    Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0,0,0}, {nx,ny,nz}),
    KOKKOS_LAMBDA(int i, int j, int k) {
        // Kernel body - runs on CPU or GPU
        result(i,j,k) = input(i,j,k) * scale;
    });

// Parallel reduction
double sum = 0.0;
Kokkos::parallel_reduce("reduction_name",
    Kokkos::RangePolicy<>(0, n),
    KOKKOS_LAMBDA(int i, double& local_sum) {
        local_sum += data(i);
    }, sum);
```

**Important Rules**:
- Use `Kokkos::DefaultExecutionSpace` for dispatch
- Avoid `std::cout` or file I/O inside kernels
- Use `Kokkos::atomic_add` for race-condition-free accumulation
- Use `DualView` for automatic host/device synchronization

## Adding New Physics Schemes

### Using the Scheme Generator

The easiest way to add a new physics scheme is using the scheme generator:

```bash
python3 scripts/generate_physics_scheme.py new_scheme_config.yaml
```

Configuration file (`new_scheme_config.yaml`):
```yaml
scheme:
  name: MyScheme
  language: cpp
  description: "My custom emission scheme"

imports:
  - name: temperature
    units: K
    dimensions: 3D
  - name: wind_speed
    units: m/s
    dimensions: 3D

exports:
  - name: my_emissions
    units: kg/m2/s
    dimensions: 2D

options:
  - name: emission_factor
    type: double
    default: 1.0e-6
```

### Manual Implementation

To manually implement a physics scheme:

1. **Create header** (`include/aces/physics/aces_my_scheme.hpp`):
```cpp
#include "aces/physics_scheme.hpp"

namespace aces {

class MyScheme : public BasePhysicsScheme {
public:
    void Initialize(const YAML::Node& config,
                   AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state,
            AcesExportState& export_state) override;
    void Finalize() override;
};

static PhysicsRegistration<MyScheme> registration_my_scheme("MyScheme");

}  // namespace aces
```

2. **Create implementation** (`src/physics/aces_my_scheme.cpp`):
```cpp
#include "aces/physics/aces_my_scheme.hpp"

namespace aces {

void MyScheme::Initialize(const YAML::Node& config,
                         AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    // Read configuration parameters
}

void MyScheme::Run(AcesImportState& import_state,
                  AcesExportState& export_state) {
    // Resolve input fields
    auto temperature = ResolveImport("temperature", import_state);

    // Resolve output fields
    auto emissions = ResolveExport("my_emissions", export_state);

    // Compute emissions using Kokkos kernel
    Kokkos::parallel_for("MyScheme_Kernel",
        Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0,0}, {nx,ny}),
        KOKKOS_LAMBDA(int i, int j) {
            emissions(i,j) = temperature(i,j,0) * 0.001;
        });

    MarkModified("my_emissions", export_state);
}

void MyScheme::Finalize() {
    // Cleanup if needed
}

}  // namespace aces
```

3. **Update CMakeLists.txt** (automatic with scheme generator):
```cmake
add_library(aces_my_scheme src/physics/aces_my_scheme.cpp)
target_link_libraries(aces_my_scheme PUBLIC aces)
```

## Testing

### Unit Tests

Unit tests verify specific examples and edge cases:

```cpp
TEST_F(MySchemeTest, BasicEmissionComputation) {
    // Setup
    MyScheme scheme;
    scheme.Initialize(config, nullptr);

    // Execute
    scheme.Run(import_state, export_state);

    // Verify
    EXPECT_NEAR(export_state.emissions(0,0), expected_value, tolerance);
}
```

### Property-Based Tests

Property-based tests verify universal properties with 100+ iterations:

```cpp
TEST_F(MySchemeTest, MassConservation) {
    for (int iter = 0; iter < 100; ++iter) {
        // Generate random inputs
        auto [nx, ny, nz] = gen.GenerateGridDimensions();
        auto emissions = gen.GenerateEmissions(nx * ny);

        // Execute
        scheme.Run(import_state, export_state);

        // Verify property
        double input_mass = std::accumulate(emissions.begin(), emissions.end(), 0.0);
        double output_mass = ComputeTotalMass(export_state);
        EXPECT_NEAR(input_mass, output_mass, 1e-10 * input_mass);
    }
}
```

## Debugging

### Enable Debug Logging

Set environment variable:
```bash
export ACES_LOG_LEVEL=DEBUG
./build/bin/aces_nuopc_driver
```

### Use GDB

```bash
gdb ./build/bin/aces_nuopc_driver
(gdb) run
(gdb) bt  # backtrace on crash
```

### Profile with Kokkos

```bash
export KOKKOS_PROFILE=1
./build/bin/aces_nuopc_driver
```

## Performance Optimization

### Identify Bottlenecks

1. Profile with Kokkos profiling tools
2. Check memory bandwidth utilization
3. Verify kernel launch parameters

### Optimize Memory Access

- Use coalesced memory access patterns
- Minimize data copies between host and device
- Use DualView for automatic synchronization

### Tune Kernel Parameters

- Adjust thread block size for GPU
- Tune loop unrolling for CPU
- Use Kokkos tuning parameters

## References

- [ESMF User Guide](https://earthsystemmodeling.org/docs/release/latest/ESMF_usrdoc)
- [NUOPC Reference Manual](https://earthsystemmodeling.org/docs/release/latest/NUOPC_refdoc)
- [Kokkos Documentation](https://kokkos.github.io/kokkos-core-wiki/)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
