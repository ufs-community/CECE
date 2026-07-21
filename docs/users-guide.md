# User's Guide

This guide covers the prerequisites, build process, configuration, and execution of the CECE component.

## Prerequisites

To build CECE, you need the following dependencies:

- **C++20 Compiler** (GCC 10+, Clang 12+)
- **CMake** (3.20+)
- **Kokkos** (4.0+)
- **ESMF** (8.0+)
- **MPI** (OpenMPI, MPICH, etc.)
- **yaml-cpp** (0.7+)
- **AMIO** (Asynchronous Multidimensional I/O, via HELM)
- **NetCDF** (C and Fortran interfaces)
- **Python 3.8+** (for scripts and testing)

### Docker Environment (Recommended)

The easiest way to get started is using the JCSDA development container, which comes with all dependencies pre-installed.

1.  **Run the Setup Script**:
    ```bash
    ./setup.sh
    ```
2.  **Activate the Environment**:
    Inside the container, ensure the Spack environment is active:
    ```bash
    source /opt/spack-environment/activate.sh
    ```

#### Troubleshooting Docker Issues
If you encounter `overlayfs` errors or other Docker-related environment issues when running the setup script, you can use the provided fix utility:
```bash
./scripts/fix_docker_and_setup.sh
```

## Building CECE

### Standard Build in JCSDA Docker

```bash
# Inside the JCSDA Docker container
source /opt/spack-environment/activate.sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Build Options

| Option | Description | Default |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | Build type (Release, Debug) | `Release` |
| `Kokkos_ENABLE_SERIAL` | Enable Serial execution space | `ON` |
| `Kokkos_ENABLE_OPENMP` | Enable OpenMP multi-core support | `ON` |
| `Kokkos_ENABLE_CUDA` | Enable NVIDIA GPU support | `OFF` |
| `Kokkos_ENABLE_HIP` | Enable AMD GPU support | `OFF` |

Example for targeting NVIDIA GPUs:
```bash
cmake .. -DKokkos_ENABLE_CUDA=ON -DKokkos_ARCH_AMPERE80=ON
```

Example for CPU-only with OpenMP:
```bash
cmake .. -DKokkos_ENABLE_SERIAL=ON -DKokkos_ENABLE_OPENMP=ON
```

## YAML Configuration

CECE is configured using a YAML file that defines emission species, data sources, scaling factors, and processing parameters. The configuration system is built around the powerful **Stacking Engine** that combines multiple emission layers with sophisticated hierarchy and scaling rules.

For complete configuration reference with all available options, see the [Configuration Documentation](configuration.md).

For technical details about how the Stacking Engine processes these configurations, see the [Stacking Engine Documentation](stacking_engine.md).

### Basic Configuration Structure

```yaml
# Driver timing configuration
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-01T06:00:00"
  timestep_seconds: 3600

# Computational grid specification
grid:
  nx: 144
  ny: 91
  lon_min: -180.0
  lon_max: 177.5
  lat_min: -90.0
  lat_max: 90.0

# Species with hierarchical emission layers
species:
  co:
    - field: "global_co_inventory"
      operation: "add"
      scale: 1.0
      category: "anthropogenic"
      hierarchy: 1

  nox:
    - field: "surface_nox"
      operation: "add"
      category: "anthropogenic"
      hierarchy: 1
      vdist_method: "PBL"        # Distribute in boundary layer

    - field: "aircraft_nox"
      operation: "add"
      category: "transportation"
      hierarchy: 1
      vdist_method: "HEIGHT"     # Distribute by altitude
      vdist_h_start: 9000.0      # 9-12 km cruise altitude
      vdist_h_end: 12000.0

# Physics schemes for process-based emissions
physics_schemes:
  - name: "sea_salt"
    language: "cpp"
    options:
      r_sala_min: 0.01
      r_sala_max: 0.5

# Data streams for external inventories
cece_data:
  streams:
    - name: "GLOBAL_INVENTORY"
      file: "/data/inventories/global_emissions.nc"
      yearFirst: 2020
      yearLast: 2020
      yearAlign: 2020
      taxmode: "cycle"
      variables:
        - file: "CO_total"
          model: "global_co_inventory"

# Diagnostic and output configuration
diagnostics:
  output_interval_seconds: 3600
  variables: ["co", "nox"]

output:
  enabled: true
  directory: "./output"
  filename_pattern: "cece_{YYYY}{MM}{DD}_{HH}.nc"
  frequency_steps: 1
  fields: ["co", "nox"]
```

### Key Configuration Concepts

- **Hierarchical Processing**: Layers within categories are processed by hierarchy (higher numbers take precedence)
- **Operations**: `add` (accumulate), `replace` (override with mask)
- **Vertical Distribution**: Multiple algorithms for mapping 2D emissions to 3D grids
- **Temporal Scaling**: Diurnal, weekly, and seasonal variation profiles
- **Environmental Dependencies**: Dynamic scaling based on meteorological fields
- **Data Stream Integration**: External data ingestion with AMIO and AXIS regridding

For complete configuration reference with all available options, see the [Configuration Documentation](configuration.md).

## Data Streams Configuration

Data streams are configured inline in the `cece_data` section of the main YAML config file:

```yaml
cece_data:
  streams:
    - name: "anthro_emissions"
      file: "/data/emissions/CEDS_CO_anthro_2020.nc"
      yearFirst: 2020
      yearLast: 2020
      yearAlign: 2020
      taxmode: "cycle"
      tintalgo: "linear"
      mapalgo: "consd"
      variables:
        - file: "CO_emis"
          model: "CEDS_CO"

    - name: "biogenic_emissions"
      file: "/data/emissions/MEGAN_ISOP_2020.nc"
      yearFirst: 2020
      yearLast: 2020
      yearAlign: 2020
      taxmode: "cycle"
      tintalgo: "linear"
      mapalgo: "consd"
      variables:
        - file: "ISOP_emis"
          model: "MEGAN_ISOP"
```

For the full set of stream options (cadence, data_model, refresh_interval_seconds, etc.), see the [Configuration Documentation](configuration.md#cece_data).

## Running CECE

### Standalone NUOPC Driver

The standalone NUOPC driver (`cece_nuopc_driver`) demonstrates the standard NUOPC lifecycle and how to manage CECE as a child model.

1.  **Configure**: Edit `cece_config.yaml` to specify your species, layers, and simulation parameters.
    The driver can be controlled via a `driver` block in `cece_config.yaml`:
    ```yaml
    driver:
      start_time: "2024-01-01T00:00:00"
      end_time: "2024-01-02T00:00:00"
      timestep_seconds: 3600
      grid:
        nx: 72
        ny: 46
        nz: 1
    ```

2.  **Build**: The driver is built as part of the main project.

3.  **Run**:
    ```bash
    cd build
    ./bin/cece_nuopc_driver
    ```

### Basic Example Driver

CECE also provides a simpler `example_driver` for basic C++ integration tests.

1.  **Configure**: Edit `cece_config.yaml` to specify your species and layers.
2.  **Run**:
    ```bash
    cd build
    ./example_driver
    ```

## Testing

### Run All Tests

```bash
cd build
ctest --output-on-failure
```

### Run Specific Test Categories

```bash
# Unit tests only
ctest -L "unit" --output-on-failure

# Integration tests
ctest -L "integration" --output-on-failure

# HEMCO parity tests
ctest -L "hemco" --output-on-failure
```

### Run Individual Tests

```bash
# Run a specific test
ctest -R test_driver_configuration --output-on-failure

# Run tests matching a pattern
ctest -R "driver" --output-on-failure
```

## Troubleshooting

### Build Issues

**Problem**: CMake cannot find ESMF
```
CMake Error: Could not find ESMF
```

**Solution**: Set the ESMF_ROOT environment variable:
```bash
export ESMF_ROOT=/path/to/esmf
cmake ..
```

**Problem**: Kokkos compilation fails
```
error: Kokkos requires C++17 or later
```

**Solution**: Ensure your compiler supports C++20:
```bash
cmake .. -DCMAKE_CXX_COMPILER=g++-10
```

### Runtime Issues

**Problem**: Data stream fails to read file
```
Error: Cannot open streams file /path/to/data.nc
```

**Solution**: Verify the file path in your `cece_data.streams` configuration exists and is readable:
```bash
ls -la /path/to/data.nc
```

**Problem**: ESMF field not found
```
Error: Field 'CO' not found in ImportState
```

**Solution**: Verify the field name matches the YAML configuration and is provided by the coupling component.

### Performance Issues

**Problem**: Slow execution on GPU
```
GPU kernel execution is slower than CPU
```

**Solution**:
1. Verify GPU is being used: Check Kokkos initialization output
2. Profile the code: Use Kokkos profiling tools
3. Check memory bandwidth: Ensure data is not being copied unnecessarily

## Performance Tuning

### CPU Performance

For multi-core CPU execution, set the number of OpenMP threads:
```bash
export OMP_NUM_THREADS=16
./build/bin/cece_nuopc_driver
```

### GPU Performance

For GPU execution, set the device ID:
```bash
export CECE_DEVICE_ID=0
./build/bin/cece_nuopc_driver
```

## Output Files

When running in standalone mode with output enabled, CECE writes NetCDF files to the configured output directory. Files follow the naming pattern:
```
cece_YYYYMMDD_HHmmss.nc
```

Each file contains:
- Time coordinate variable (seconds since start time)
- All configured emission species fields
- Optional diagnostic fields (if enabled)

Files are CF-1.8 compliant and can be inspected with standard tools:
```bash
ncdump -h cece_20240101_000000.nc
```

## Physics Schemes

CECE includes a suite of process-based physics schemes for computing emissions from natural and anthropogenic sources. Each scheme is available as both a native C++ (Kokkos) implementation and a Fortran bridge variant. Schemes are enabled and configured through the `physics_schemes` block in your YAML configuration.

| Scheme | Description | Documentation |
| --- | --- | --- |
| DMS | Dimethyl sulfide sea-air exchange fluxes | [DMS](dms.md) |
| Sea Salt | Size-resolved sea salt aerosol emissions (Gong 2003) | [Sea Salt](sea_salt.md) |
| Dust (Ginoux Legacy) | Single-bin mineral dust emissions (Ginoux 2001) | [Dust](dust.md) |
| Ginoux (GOCART2G) | Multi-bin dust emissions with Marticorena threshold | [Ginoux](ginoux.md) |
| FENGSHA | Physically-based saltation dust model with Fécan correction | [FENGSHA](fengsha.md) |
| K14 | Kok et al. (2014) dust scheme with full soil physics | [K14](k14.md) |
| MEGAN | Biogenic isoprene emissions (Model of Emissions of Gases and Aerosols from Nature) | [MEGAN](megan.md) |
| Lightning NOx | Lightning-produced NOx from convective cloud top height | [Lightning NOx](lightning_nox.md) |
| Soil NOx | Soil NOx from microbial nitrification/denitrification | [Soil NOx](soil_nox.md) |
| Volcano | Volcanic SO₂ point-source emissions with vertical distribution | [Volcano](volcano.md) |

For guidance on developing your own physics schemes, see the [Physics Scheme Development Guide](physics_scheme_development.md).

## Python Interface

CECE provides Python bindings through pybind11 that expose the full C++ core to Python with type-safe access, automatic memory management, and zero-copy NumPy interop.

```python
import cece
import numpy as np

config = cece.load_config("cece_config.yaml")
cece.initialize(config)

state = cece.CeceState(nx=144, ny=96, nz=72)
state.add_import_field("TEMPERATURE", np.asfortranarray(np.zeros((144, 96, 72))))

cece.compute(state, hour=12, day_of_week=3, month=7)
co_emissions = state.get_export_field("CO_EMIS")

cece.finalize()
```

To build with Python support, enable the `BUILD_PYTHON_BINDINGS` CMake option:

```bash
cmake .. -DBUILD_PYTHON_BINDINGS=ON
make -j$(nproc)
```

For the full API reference, configuration management, state handling, NumPy zero-copy details, and migration notes, see the [Python Bindings Documentation](python_bindings.md).

## Next Steps

- Read the [Developer Guide](developer_guide.md) for architecture details
- Check [Physics Scheme Development](physics_scheme_development.md) for adding new schemes
- Review [HEMCO Migration Guide](hemco_migration.md) for migrating from HEMCO
- See [Examples](examples.md) for common use cases
- Explore the [Python Bindings](python_bindings.md) for scripting and integration
- Browse individual [physics scheme docs](#physics-schemes) for algorithm details and configuration
