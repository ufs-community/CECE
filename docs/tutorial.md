# Tutorials

This section provides step-by-step guides for common CECE extension tasks.

## Tutorial 1: Adding a New Emission Species

This tutorial shows how to add a new emission species to CECE without recompilation.

### Step 1: Define the Species in YAML

Edit your `cece_config.yaml` and add the new species to the `species` section:

```yaml
species:
  - name: CO
    units: kg/m2/s
    long_name: "Carbon Monoxide"

  - name: PM25
    units: kg/m2/s
    long_name: "Fine Particulate Matter"
```

### Step 2: Add Emission Layers

Add layers that contribute to the new species:

```yaml
layers:
  - name: anthropogenic_pm25
    species: PM25
    hierarchy: 1
    operation: add
    file: /data/emissions/CEDS_PM25_2020.nc
    variable: PM25_emis
    vertical_distribution:
      method: SINGLE
      layer: 0
    scale_factors:
      - name: temporal_scale
        file: /data/scales/diurnal_pm25.nc
        variable: DIURNAL_SCALE

  - name: biomass_burning_pm25
    species: PM25
    hierarchy: 2
    operation: add
    file: /data/emissions/GFED4_PM25_2020.nc
    variable: PM25_emis
    vertical_distribution:
      method: PBL
    scale_factors:
      - name: seasonal_scale
        file: /data/scales/seasonal_pm25.nc
        variable: SEASONAL_SCALE
```

### Step 3: Configure Output

Add the new species to the output configuration:

```yaml
output:
  directory: ./cece_output
  filename_pattern: "cece_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1
  fields:
    - CO
    - PM25
  diagnostics: false
```

### Step 4: Run CECE

```bash
./build/bin/cece_nuopc_driver --config cece_config.yaml
```

The new species will be automatically created and computed without recompilation!

---

## Tutorial 2: Adding a New Physics Parameterization

This tutorial shows how to add a new physics scheme to CECE.

### Step 1: Using the Scheme Generator (Recommended)

The easiest way is to use the automatic scheme generator:

```bash
# Create a configuration file for your scheme
cat > my_scheme_config.yaml << 'EOF'
scheme:
  name: MyEmissionScheme
  language: cpp
  description: "My custom emission parameterization"

imports:
  - name: temperature
    units: K
    dimensions: 3D
  - name: wind_speed
    units: m/s
    dimensions: 3D
  - name: humidity
    units: kg/kg
    dimensions: 3D

exports:
  - name: my_emissions
    units: kg/m2/s
    dimensions: 2D

diagnostics:
  - name: intermediate_value
    units: kg/m3
    dimensions: 3D

options:
  - name: emission_factor
    type: double
    default: 1.0e-6
    description: "Base emission factor"
  - name: temperature_threshold
    type: double
    default: 273.15
    description: "Minimum temperature for emissions"
EOF

# Generate the scheme
python3 scripts/generate_physics_scheme.py my_scheme_config.yaml
```

This creates:
- `include/cece/physics/cece_my_emission_scheme.hpp`
- `src/physics/cece_my_emission_scheme.cpp`
- Updated `CMakeLists.txt` entries

### Step 2: Implement the Physics Kernel

Edit `src/physics/cece_my_emission_scheme.cpp` and implement the `Run` method:

```cpp
void MyEmissionScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    // Resolve input fields
    auto temperature = ResolveImport("temperature", import_state);
    auto wind_speed = ResolveImport("wind_speed", import_state);
    auto humidity = ResolveImport("humidity", import_state);

    // Resolve output fields
    auto emissions = ResolveExport("my_emissions", export_state);

    // Resolve diagnostic fields
    auto intermediate = ResolveDiagnostic("intermediate_value", nx, ny, nz);

    // Get grid dimensions
    int nx = emissions.extent(0);
    int ny = emissions.extent(1);

    // Implement physics kernel using Kokkos
    Kokkos::parallel_for("MyEmissionScheme_Kernel",
        Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0,0}, {nx,ny}),
        KOKKOS_LAMBDA(int i, int j) {
            // Get meteorological inputs at surface (layer 0)
            double T = temperature(i, j, 0);
            double U = wind_speed(i, j, 0);
            double Q = humidity(i, j, 0);

            // Compute emissions based on temperature
            double temp_factor = 0.0;
            if (T > emission_factor_) {
                temp_factor = std::exp(0.1 * (T - 298.15));
            }

            // Compute emissions based on wind speed
            double wind_factor = 1.0 + 0.01 * U;

            // Combine factors
            emissions(i, j) = emission_factor_ * temp_factor * wind_factor;

            // Store intermediate value for diagnostics
            intermediate(i, j, 0) = temp_factor;
        });

    // Mark fields as modified
    MarkModified("my_emissions", export_state);
}
```

### Step 3: Configure the Scheme

Add the scheme to your `cece_config.yaml`:

```yaml
physics_schemes:
  - name: MyEmissionScheme
    enabled: true
    options:
      emission_factor: 1.5e-6
      temperature_threshold: 280.0
```

### Step 4: Build and Run

```bash
cd build
cmake ..
make -j4
ctest --output-on-failure
./bin/cece_nuopc_driver --config ../cece_config.yaml
```

### Step 5: Manual Implementation (Advanced)

If you prefer manual implementation without the generator:

**Header** (`include/cece/physics/cece_my_scheme.hpp`):
```cpp
#include "cece/physics_scheme.hpp"

namespace cece {

class MyScheme : public BasePhysicsScheme {
public:
    void Initialize(const YAML::Node& config,
                   CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state,
            CeceExportState& export_state) override;
    void Finalize() override;

private:
    double emission_factor_ = 1.0e-6;
    double temperature_threshold_ = 273.15;
};

// Self-registration
static PhysicsRegistration<MyScheme> registration_my_scheme("MyScheme");

}  // namespace cece
```

**Implementation** (`src/physics/cece_my_scheme.cpp`):
```cpp
#include "cece/physics/cece_my_scheme.hpp"

namespace cece {

void MyScheme::Initialize(const YAML::Node& config,
                         CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    if (config["emission_factor"]) {
        emission_factor_ = config["emission_factor"].as<double>();
    }
    if (config["temperature_threshold"]) {
        temperature_threshold_ = config["temperature_threshold"].as<double>();
    }
}

void MyScheme::Run(CeceImportState& import_state,
                  CeceExportState& export_state) {
    auto temperature = ResolveImport("temperature", import_state);
    auto emissions = ResolveExport("my_emissions", export_state);

    int nx = emissions.extent(0);
    int ny = emissions.extent(1);

    Kokkos::parallel_for("MyScheme_Kernel",
        Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0,0}, {nx,ny}),
        KOKKOS_LAMBDA(int i, int j) {
            double T = temperature(i, j, 0);
            if (T > temperature_threshold_) {
                emissions(i, j) = emission_factor_ * std::exp(0.1 * (T - 298.15));
            }
        });

    MarkModified("my_emissions", export_state);
}

void MyScheme::Finalize() {
    // Cleanup if needed
}

}  // namespace cece
```

---

## Tutorial 3: Adding a New Physics Scheme (Legacy Fortran)

If you have existing Fortran code, you can wrap it in a C++ bridge.

### Step 1: Create Fortran Kernel

Create `src/physics/my_fortran_kernel.F90`:

```fortran
module my_fortran_kernel_mod
    use iso_c_binding
    implicit none
    private
    public :: compute_emissions

contains

    subroutine compute_emissions(temperature, emissions, nx, ny) bind(c, name="compute_emissions")
        integer(c_int), intent(in) :: nx, ny
        real(c_double), intent(in) :: temperature(nx, ny)
        real(c_double), intent(inout) :: emissions(nx, ny)
        integer :: i, j

        do j = 1, ny
            do i = 1, nx
                if (temperature(i, j) > 273.15) then
                    emissions(i, j) = 1.0e-6 * exp(0.1 * (temperature(i, j) - 298.15))
                else
                    emissions(i, j) = 0.0
                end if
            end do
        end do
    end subroutine compute_emissions

end module my_fortran_kernel_mod
```

### Step 2: Create C++ Bridge

Create `src/physics/cece_my_fortran_scheme.cpp`:

```cpp
#include "cece/physics_scheme.hpp"

extern "C" {
    void compute_emissions(double* temperature, double* emissions, int nx, int ny);
}

namespace cece {

class MyFortranScheme : public BasePhysicsScheme {
public:
    void Initialize(const YAML::Node& config,
                   CeceDiagnosticManager* diag_manager) override {
        BasePhysicsScheme::Initialize(config, diag_manager);
    }

    void Run(CeceImportState& import_state,
            CeceExportState& export_state) override {
        auto& temp_dv = import_state.fields["temperature"];
        auto& emis_dv = export_state.fields["my_emissions"];

        int nx = emis_dv.view_host().extent(0);
        int ny = emis_dv.view_host().extent(1);

        // Sync to host (Fortran runs on CPU)
        temp_dv.sync<Kokkos::HostSpace>();
        emis_dv.sync<Kokkos::HostSpace>();

        // Call Fortran kernel
        compute_emissions(temp_dv.view_host().data(),
                         emis_dv.view_host().data(),
                         nx, ny);

        // Mark modified and sync back to device
        emis_dv.modify<Kokkos::HostSpace>();
        emis_dv.sync<Kokkos::DefaultExecutionSpace>();

        MarkModified("my_emissions", export_state);
    }
};

static PhysicsRegistration<MyFortranScheme> registration_my_fortran("MyFortranScheme");

}  // namespace cece
```

### Step 3: Update CMakeLists.txt

Add the Fortran source to the build:

```cmake
add_library(cece_my_fortran_kernel src/physics/my_fortran_kernel.F90)
target_link_libraries(cece_my_fortran_kernel PUBLIC cece)

add_library(cece_my_fortran_scheme src/physics/cece_my_fortran_scheme.cpp)
target_link_libraries(cece_my_fortran_scheme PUBLIC cece cece_my_fortran_kernel)
```

---

## Tutorial 4: Adding New Physics Schemes
