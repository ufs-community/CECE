# Physics Scheme Development Guide

## Overview

This guide provides a comprehensive workflow for developing new physics schemes in ACES. Physics schemes are pluggable modules that compute or modify emissions based on meteorological inputs. ACES provides a scientist-friendly framework that abstracts away ESMF complexity and enables focus on physics implementation.

## Table of Contents

1. [Physics Scheme Lifecycle](#physics-scheme-lifecycle)
2. [Getting Started](#getting-started)
3. [Scheme Generator Workflow](#scheme-generator-workflow)
4. [Writing Kokkos Kernels](#writing-kokkos-kernels)
5. [Calling Fortran Kernels from C++](#calling-fortran-kernels-from-c)
6. [Configuration and Parameters](#configuration-and-parameters)
7. [Field Resolution and Caching](#field-resolution-and-caching)
8. [Diagnostic Fields](#diagnostic-fields)
9. [Performance Optimization](#performance-optimization)
10. [Testing and Validation](#testing-and-validation)
11. [Troubleshooting](#troubleshooting)
12. [Best Practices](#best-practices)

---

## Physics Scheme Lifecycle

Every physics scheme in ACES follows a three-phase lifecycle:

### 1. Initialize Phase

Called once during model initialization. Use this phase to:
- Read configuration parameters from YAML
- Validate parameters and check for errors
- Pre-compute lookup tables or coefficients
- Register diagnostic fields
- Allocate any internal data structures

**Key Points:**
- Called before the first Run phase
- Configuration is immutable after Initialize
- Errors should be reported with descriptive messages
- Pre-computation here improves Run phase performance

### 2. Run Phase

Called once per time step. Use this phase to:
- Resolve input fields from meteorology
- Resolve output fields to be computed or modified
- Execute Kokkos kernels for physics computation
- Mark modified fields for device-to-host synchronization

**Key Points:**
- Must be efficient (called many times)
- All computation should use Kokkos for performance portability
- Avoid I/O or blocking operations
- Field handles are cached to avoid redundant lookups

### 3. Finalize Phase

Called once during model shutdown. Use this phase to:
- Release allocated resources
- Flush any pending I/O operations
- Clean up temporary data structures

**Key Points:**
- Called after the last Run phase
- Should be quick (model is shutting down)
- BasePhysicsScheme provides a default no-op implementation

---

## Getting Started

### Prerequisites

Before developing a physics scheme, ensure you have:
- ACES source code checked out
- JCSDA Docker environment set up (see AGENTS.md)
- Python 3.6+ with jinja2 and pyyaml installed
- Basic understanding of Kokkos and ESMF concepts

### Quick Start Example

Here's a minimal physics scheme that computes emissions from temperature:

```cpp
// include/aces/physics/aces_my_scheme.hpp
#ifndef ACES_MY_SCHEME_HPP
#define ACES_MY_SCHEME_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

class MyScheme : public BasePhysicsScheme {
public:
    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;
};

}  // namespace aces
#endif
```

```cpp
// src/physics/aces_my_scheme.cpp
#include "aces/physics/aces_my_scheme.hpp"
#include "aces/aces_physics_factory.hpp"

namespace aces {

static PhysicsRegistration<MyScheme> register_my_scheme("my_scheme");

void MyScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);
    // Read parameters from config
}

void MyScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto temperature = ResolveImport("temperature", import_state);
    auto emissions = ResolveExport("emissions", export_state);

    if (temperature.data() == nullptr || emissions.data() == nullptr) return;

    int nx = emissions.extent(0);
    int ny = emissions.extent(1);
    int nz = emissions.extent(2);

    Kokkos::parallel_for(
        "MySchemeKernel",
        Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>(
            {0, 0, 0}, {nx, ny, nz}),
        KOKKOS_LAMBDA(int i, int j, int k) {
            emissions(i, j, k) = temperature(i, j, k) * 0.001;
        });

    Kokkos::fence();
    MarkModified("emissions", export_state);
}

}  // namespace aces
```

---

## Scheme Generator Workflow

The ACES scheme generator automates the creation of physics scheme scaffolding from YAML configuration files.

### Step 1: Create Configuration File

Create a YAML file describing your scheme (e.g., `my_scheme_config.yaml`):

```yaml
scheme:
  name: MyEmissionScheme
  scheme_name: my_emission_scheme
  description: "Computes emissions from temperature and solar radiation"
  language: cpp  # or fortran

imports:
  - name: temperature
    var_name: temp
    units: K
    dimensions: 3D
  - name: solar_radiation
    var_name: solar
    units: W/m2
    dimensions: 3D

exports:
  - name: emissions
    var_name: emis
    units: kg/m2/s
    dimensions: 2D

options:
  - name: base_emission_factor
    type: double
    default: 1.0e-6
    description: "Base emission factor"
  - name: temperature_ref
    type: double
    default: 298.15
    description: "Reference temperature"
  - name: q10
    type: double
    default: 2.0
    description: "Q10 temperature sensitivity"

diagnostics:
  - name: temperature_factor
    units: dimensionless
    description: "Temperature scaling factor"
  - name: solar_factor
    units: dimensionless
    description: "Solar radiation scaling factor"
```

### Step 2: Run Scheme Generator

```bash
./scripts/generate_physics_scheme.py my_scheme_config.yaml
```

This generates:
- `include/aces/physics/aces_my_emission_scheme.hpp`
- `src/physics/aces_my_emission_scheme.cpp`
- `src/physics/my_emission_scheme_kernel.F90` (if language=fortran)

### Step 3: Implement Physics Logic

Edit the generated files and implement the physics computation in the Run method.

### Step 4: Update CMakeLists.txt

Add the new scheme to `CMakeLists.txt`:

```cmake
add_library(aces_physics_my_emission_scheme src/physics/aces_my_emission_scheme.cpp)
target_link_libraries(aces_physics_my_emission_scheme PUBLIC aces_core)
target_link_libraries(aces_core PUBLIC aces_physics_my_emission_scheme)
```

### Step 5: Configure in YAML

Add the scheme to your ACES configuration file:

```yaml
physics_schemes:
  - name: my_emission_scheme
    options:
      base_emission_factor: 1.0e-6
      temperature_ref: 298.15
      q10: 2.0
```

### Step 6: Build and Test

```bash
cd build
cmake ..
make -j4
ctest --output-on-failure
```

---

## Writing Kokkos Kernels

Kokkos kernels are the core of physics computation in ACES. They enable single-source code that runs efficiently on CPUs and GPUs.

### Basic Kernel Structure

```cpp
Kokkos::parallel_for(
    "KernelName",
    Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>(
        {0, 0, 0}, {nx, ny, nz}),
    KOKKOS_LAMBDA(int i, int j, int k) {
        // Kernel body: executed for each grid point
        // Access fields through Kokkos::View handles
        output(i, j, k) = input(i, j, k) * scale;
    });

Kokkos::fence();  // Ensure all threads complete
```

### Key Kokkos Concepts

#### 1. Execution Spaces

Kokkos::DefaultExecutionSpace automatically selects the appropriate backend:
- Serial (debugging)
- OpenMP (CPU parallelism)
- CUDA (NVIDIA GPU)
- HIP (AMD GPU)

```cpp
// Automatic dispatch to appropriate backend
Kokkos::parallel_for(
    "KernelName",
    Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>(
        {0, 0, 0}, {nx, ny, nz}),
    KOKKOS_LAMBDA(int i, int j, int k) {
        // Code runs on CPU or GPU automatically
    });
```

#### 2. Memory Layouts

Use Kokkos::LayoutLeft for Fortran-style column-major layout (matches ESMF):

```cpp
Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> field;
// Access: field(i, j, k) where i varies fastest
```

#### 3. Parallel Patterns

**Parallel For Loop:**
```cpp
Kokkos::parallel_for(
    "LoopName",
    Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>(
        {0, 0, 0}, {nx, ny, nz}),
    KOKKOS_LAMBDA(int i, int j, int k) {
        // Loop body
    });
```

**Parallel Reduce (Global Reduction):**
```cpp
double total = 0.0;
Kokkos::parallel_reduce(
    "ReductionName",
    Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>(
        {0, 0, 0}, {nx, ny, nz}),
    KOKKOS_LAMBDA(int i, int j, int k, double& local_sum) {
        local_sum += field(i, j, k);
    }, total);
```

**Atomic Operations (Race-Condition-Free Accumulation):**
```cpp
Kokkos::parallel_for(
    "AtomicName",
    Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<3>>(
        {0, 0, 0}, {nx, ny, nz}),
    KOKKOS_LAMBDA(int i, int j, int k) {
        // Multiple threads can safely accumulate to the same location
        Kokkos::atomic_add(&output(i, j, 0), input(i, j, k));
    });
```

### Performance Tips

#### 1. Minimize Floating-Point Operations

Use Horner's method for polynomial evaluation:

```cpp
// Bad: 4 multiplications
double result = a*x*x*x + b*x*x + c*x + d;

// Good: 3 multiplications (Horner's method)
double result = ((a*x + b)*x + c)*x + d;
```

#### 2. Avoid Function Calls in Kernels

Pre-compute values outside the kernel:

```cpp
// Bad: Expensive function called for every grid point
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    output(i, j, k) = expensive_function(input(i, j, k));
});

// Good: Pre-compute lookup table
std::vector<double> lookup(256);
for (int i = 0; i < 256; ++i) {
    lookup[i] = expensive_function(i / 256.0);
}
auto lookup_device = Kokkos::create_mirror_view_and_copy(
    Kokkos::DefaultExecutionSpace(), lookup);

Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    int idx = (int)(input(i, j, k) * 256);
    output(i, j, k) = lookup_device(idx);
});
```

#### 3. Optimize Memory Access Patterns

Access memory in order of layout:

```cpp
// Good: Coalesced memory access (LayoutLeft)
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    output(i, j, k) = input(i, j, k);  // i varies fastest
});

// Bad: Strided memory access
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    output(k, j, i) = input(i, j, k);  // k varies fastest
});
```

#### 4. Use Local Variables to Cache Values

```cpp
// Good: Cache frequently accessed values
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    double temp = temperature(i, j, k);
    double solar = solar_radiation(i, j, k);
    double factor = temp * solar;
    output(i, j, k) = base_value * factor;
});
```

---

## Calling Fortran Kernels from C++

ACES supports calling Fortran kernels from C++ schemes using Fortran-C interoperability.

### Fortran Kernel Example

```fortran
! src/physics/my_kernel.F90
module my_kernel_mod
    use iso_c_binding
    implicit none
    private
    public :: run_my_kernel

contains

    subroutine run_my_kernel(nx, ny, nz, temperature_ptr, emissions_ptr) &
        bind(C, name="run_my_kernel_fortran")
        integer(c_int), intent(in), value :: nx, ny, nz
        real(c_double), intent(in) :: temperature_ptr(nx, ny, nz)
        real(c_double), intent(inout) :: emissions_ptr(nx, ny, nz)

        integer :: i, j, k

        !$omp parallel do collapse(3)
        do k = 1, nz
            do j = 1, ny
                do i = 1, nx
                    emissions_ptr(i, j, k) = temperature_ptr(i, j, k) * 0.001_c_double
                end do
            end do
        end do
        !$omp end parallel do

    end subroutine run_my_kernel

end module my_kernel_mod
```

### C++ Wrapper

```cpp
// Declare Fortran function
extern "C" void run_my_kernel_fortran(int nx, int ny, int nz,
                                      double* temperature_ptr,
                                      double* emissions_ptr);

// Call from C++ scheme
void MyScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    auto temperature = ResolveImport("temperature", import_state);
    auto emissions = ResolveExport("emissions", export_state);

    if (temperature.data() == nullptr || emissions.data() == nullptr) return;

    int nx = emissions.extent(0);
    int ny = emissions.extent(1);
    int nz = emissions.extent(2);

    // Sync device to host before calling Fortran
    Kokkos::fence();

    // Call Fortran kernel
    run_my_kernel_fortran(nx, ny, nz,
                         const_cast<double*>(temperature.data()),
                         emissions.data());

    // Mark modified for device-to-host sync
    MarkModified("emissions", export_state);
}
```

### Important Notes

- Fortran arrays are 1-indexed; C++ arrays are 0-indexed
- Use `iso_c_binding` for proper type compatibility
- Sync device to host before calling Fortran
- Fortran kernels run on CPU only (no GPU support)
- Use OpenMP for parallelization in Fortran

---

## Configuration and Parameters

All physics parameters must be configurable via YAML. Never hardcode physical constants.

### Reading Parameters

```cpp
void MyScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    // Read scalar parameters
    if (config["emission_factor"]) {
        emission_factor_ = config["emission_factor"].as<double>();
    }

    // Read array parameters
    if (config["diurnal_cycle"]) {
        for (int hour = 0; hour < 24; ++hour) {
            diurnal_cycle_[hour] = config["diurnal_cycle"][hour].as<double>();
        }
    }

    // Read boolean flags
    if (config["apply_mask"]) {
        apply_mask_ = config["apply_mask"].as<bool>();
    }
}
```

### YAML Configuration Example

```yaml
physics_schemes:
  - name: my_scheme
    options:
      emission_factor: 1.0e-6
      temperature_ref: 298.15
      q10: 2.0
      apply_mask: true
      diurnal_cycle:
        - 0.5
        - 0.6
        - 0.7
        # ... 24 values total
```

### Parameter Validation

```cpp
void MyScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (config["emission_factor"]) {
        emission_factor_ = config["emission_factor"].as<double>();
        if (emission_factor_ < 0.0) {
            throw std::invalid_argument("emission_factor must be non-negative");
        }
    }

    if (config["temperature_ref"]) {
        temperature_ref_ = config["temperature_ref"].as<double>();
        if (temperature_ref_ < 200.0 || temperature_ref_ > 330.0) {
            throw std::out_of_range("temperature_ref must be between 200 and 330 K");
        }
    }
}
```

---

## Field Resolution and Caching

BasePhysicsScheme provides efficient field resolution with automatic caching.

### Resolving Import Fields

```cpp
// Resolve meteorological input field
auto temperature = ResolveImport("temperature", import_state);

// Field handle is cached; subsequent calls return cached value
auto temperature_again = ResolveImport("temperature", import_state);
// No ESMF lookup performed; returns cached handle
```

### Resolving Export Fields

```cpp
// Resolve emission output field
auto emissions = ResolveExport("emissions", export_state);

// Modify the field
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    emissions(i, j, k) = compute_emission(i, j, k);
});

// Mark as modified for device-to-host sync
MarkModified("emissions", export_state);
```

### Resolving from Both States

```cpp
// Useful for schemes that depend on previously computed fields
auto base_emissions = ResolveInput("emissions", import_state, export_state);
// Checks import state first, then export state
```

### Field Name Mapping

Map internal names to external field names via YAML:

```yaml
physics_schemes:
  - name: my_scheme
    input_mapping:
      temp: temperature
      solar: solar_radiation
    output_mapping:
      emis: emissions
```

Then use internal names in code:

```cpp
auto temperature = ResolveImport("temp", import_state);  // Maps to "temperature"
auto emissions = ResolveExport("emis", export_state);    // Maps to "emissions"
```

---

## Diagnostic Fields

Diagnostic fields are optional intermediate variables useful for validation and debugging.

### Registering Diagnostic Fields

```cpp
void MyScheme::Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (diag_manager != nullptr) {
        temperature_factor_ = ResolveDiagnostic("temperature_factor", nx, ny, nz,
                                                "dimensionless",
                                                "Temperature scaling factor");
    }
}
```

### Writing to Diagnostic Fields

```cpp
void MyScheme::Run(AcesImportState& import_state, AcesExportState& export_state) {
    // ... resolve fields ...

    Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
        double temp_factor = compute_temperature_factor(temperature(i, j, k));
        emissions(i, j, k) = base_emission * temp_factor;

        // Store diagnostic value
        temperature_factor_(i, j, k) = temp_factor;
    });
}
```

### Diagnostic Output

Diagnostic fields are automatically written to NetCDF output if enabled in configuration:

```yaml
output:
  diagnostics: true
  fields:
    - emissions
    - temperature_factor
```

---

## Performance Optimization

### Profiling

Use Kokkos profiling tools to identify bottlenecks:

```bash
# Enable Kokkos profiling
export KOKKOS_PROFILE_LIBRARY=/path/to/libkokkos_profiling.so
./aces_driver
```

### Common Optimization Techniques

#### 1. Kernel Fusion

Combine multiple kernels into one to reduce memory bandwidth:

```cpp
// Bad: Two separate kernels
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    temp_factor(i, j, k) = compute_temp_factor(temperature(i, j, k));
});
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    emissions(i, j, k) = base_emission * temp_factor(i, j, k);
});

// Good: Fused kernel
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    double temp_factor = compute_temp_factor(temperature(i, j, k));
    emissions(i, j, k) = base_emission * temp_factor;
});
```

#### 2. Lookup Tables

Pre-compute expensive functions:

```cpp
// Pre-compute lookup table in Initialize
std::vector<double> lookup(256);
for (int i = 0; i < 256; ++i) {
    lookup[i] = expensive_function(i / 256.0);
}
auto lookup_device = Kokkos::create_mirror_view_and_copy(
    Kokkos::DefaultExecutionSpace(), lookup);

// Use in kernel
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    int idx = (int)(input(i, j, k) * 256);
    output(i, j, k) = lookup_device(idx);
});
```

#### 3. Vectorization

Structure code to enable compiler vectorization:

```cpp
// Good: Compiler can vectorize
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    output(i, j, k) = input(i, j, k) * scale;
});

// Bad: Compiler cannot vectorize (data dependency)
Kokkos::parallel_for(..., KOKKOS_LAMBDA(int i, int j, int k) {
    output(i, j, k) = output(i-1, j, k) + input(i, j, k);
});
```

---

## Testing and Validation

### Unit Tests

Create unit tests for your scheme:

```cpp
// tests/test_my_scheme.cpp
#include <gtest/gtest.h>
#include "aces/physics/aces_my_scheme.hpp"

class MySchemeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize scheme
        scheme_.Initialize(config_, nullptr);
    }

    MyScheme scheme_;
    YAML::Node config_;
};

TEST_F(MySchemeTest, EmissionComputation) {
    // Create mock import/export states
    // Call scheme_.Run()
    // Verify results
}
```

### Property-Based Testing

Test universal properties with random inputs:

```cpp
// Generate random valid inputs
// Execute scheme
// Verify properties (e.g., mass conservation, physical bounds)
```

### Integration Testing

Test scheme in full ACES context:

```bash
# Build with scheme
cd build && cmake .. && make -j4

# Run single-model driver
./aces_nuopc_single_driver --config aces_config.yaml

# Verify output
ncdump -h aces_output.nc
```

---

## Troubleshooting

### Common Issues

#### 1. Segmentation Fault in Kernel

**Cause:** Accessing invalid memory (out-of-bounds array access)

**Solution:**
- Check array dimensions match grid size
- Verify field handles are not null before use
- Use bounds checking in debug builds

```cpp
if (temperature.data() == nullptr) {
    return;  // Field not found
}
```

#### 2. Incorrect Results

**Cause:** Logic error or incorrect field resolution

**Solution:**
- Add diagnostic fields to inspect intermediate values
- Compare with reference implementation
- Check parameter values in YAML config

#### 3. Poor Performance

**Cause:** Inefficient kernel implementation

**Solution:**
- Profile with Kokkos profiling tools
- Check memory access patterns
- Fuse multiple kernels
- Use lookup tables for expensive functions

#### 4. Device-Host Synchronization Issues

**Cause:** Forgetting to call MarkModified()

**Solution:**
- Always call MarkModified() after modifying export fields
- Ensure Kokkos::fence() is called before CPU access

```cpp
Kokkos::parallel_for(..., KOKKOS_LAMBDA(...) { ... });
Kokkos::fence();
MarkModified("emissions", export_state);
```

---

## Best Practices

### 1. Always Use BasePhysicsScheme Helpers

```cpp
// Good: Use helper methods
auto temperature = ResolveImport("temperature", import_state);
auto emissions = ResolveExport("emissions", export_state);

// Bad: Direct ESMF calls
// ESMC_Field field = ESMC_StateGetField(import_state, "temperature", rc);
```

### 2. Never Hardcode Physical Constants

```cpp
// Good: Read from YAML
if (config["q10"]) {
    q10_ = config["q10"].as<double>();
}

// Bad: Hardcoded constant
double q10 = 2.0;
```

### 3. Use Kokkos for All Compute

```cpp
// Good: Kokkos kernel
Kokkos::parallel_for(..., KOKKOS_LAMBDA(...) { ... });

// Bad: CPU-only loop
for (int i = 0; i < nx; ++i) {
    for (int j = 0; j < ny; ++j) {
        output(i, j, 0) = input(i, j, 0) * scale;
    }
}
```

### 4. Validate Configuration Parameters

```cpp
// Good: Validate parameters
if (emission_factor_ < 0.0) {
    throw std::invalid_argument("emission_factor must be non-negative");
}

// Bad: No validation
// emission_factor_ = config["emission_factor"].as<double>();
```

### 5. Document Your Scheme

```cpp
/**
 * @class MyScheme
 * @brief Computes emissions from temperature and solar radiation
 *
 * This scheme implements the Q10 temperature parameterization:
 * emission = base_factor * Q10^((T-Tref)/10) * solar_factor
 *
 * Configuration parameters:
 * - base_emission_factor: Base emission rate [kg/m2/s]
 * - temperature_ref: Reference temperature [K]
 * - q10: Q10 temperature sensitivity factor
 * - solar_factor: Solar radiation scaling coefficient
 */
class MyScheme : public BasePhysicsScheme { ... };
```

### 6. Test on Both CPU and GPU

```bash
# Test on CPU (OpenMP)
export KOKKOS_DEVICES=OpenMP
ctest --output-on-failure

# Test on GPU (CUDA)
export KOKKOS_DEVICES=Cuda
ctest --output-on-failure
```

### 7. Use Meaningful Kernel Names

```cpp
// Good: Descriptive kernel name
Kokkos::parallel_for(
    "MySchemeEmissionComputationKernel",
    ...);

// Bad: Generic kernel name
Kokkos::parallel_for(
    "Kernel",
    ...);
```

---

## Example: Complete Physics Scheme

See the following example schemes in the ACES repository:

1. **ExampleEmissionGeneration** (`include/aces/physics/aces_example_emission_generation.hpp`)
   - Demonstrates emission generation pattern
   - Shows Q10 temperature parameterization
   - Includes diagnostic field output

2. **ExampleEmissionModification** (`include/aces/physics/aces_example_emission_modification.hpp`)
   - Demonstrates emission modification pattern
   - Shows diurnal cycle application
   - Shows in-place field modification

3. **ExampleDiagnosticComputation** (`include/aces/physics/aces_example_diagnostic_computation.hpp`)
   - Demonstrates diagnostic field computation
   - Shows validation and quality flagging
   - Shows reading from export state

---

## Additional Resources

- **ESMF User Guide:** https://earthsystemmodeling.org/docs/release/latest/ESMF_usrdoc
- **Kokkos Documentation:** https://kokkos.github.io/kokkos-core-wiki/
- **ACES Architecture:** See `docs/api.md`
- **Physics Scheme Examples:** See `include/aces/physics/aces_example_*.hpp`

---

## Getting Help

If you encounter issues:

1. Check the troubleshooting section above
2. Review example schemes in the repository
3. Check ACES documentation in `docs/`
4. Run tests to verify your implementation
5. Use diagnostic fields to inspect intermediate values
6. Profile with Kokkos profiling tools

Good luck with your physics scheme development!
