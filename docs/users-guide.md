# User's Guide

This guide covers the prerequisites, build process, configuration, and execution of the ACES component.

## Prerequisites

To build ACES, you need the following dependencies:

- **C++20 Compiler** (GCC 10+, Clang 12+)
- **CMake** (3.20+)
- **Kokkos** (4.0+)
- **ESMF** (8.0+)
- **MPI** (OpenMPI, MPICH, etc.)
- **yaml-cpp** (0.7+)
- **CDEPS** (inline version)
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

## Building ACES

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

## YAML Configuration Format

ACES is configured using a YAML file (default: `aces_config.yaml`). Here's a comprehensive example:

```yaml
# ACES Configuration File

# Metadata
metadata:
  version: "1.0"
  description: "Example ACES configuration"
  author: "ACES Team"

# Emission species definitions
species:
  - name: CO
    units: kg/m2/s
    long_name: "Carbon Monoxide"
  - name: NOx
    units: kg/m2/s
    long_name: "Nitrogen Oxides"
  - name: ISOP
    units: kg/m2/s
    long_name: "Isoprene"

# Emission layers (base emissions, scale factors, masks)
layers:
  - name: anthropogenic_co
    species: CO
    hierarchy: 1
    operation: add
    file: /data/emissions/CEDS_CO_2020.nc
    variable: CO_emis
    vertical_distribution:
      method: SINGLE
      layer: 0
    scale_factors:
      - name: temporal_scale
        file: /data/scales/diurnal_co.nc
        variable: DIURNAL_SCALE
    masks:
      - name: land_mask
        file: /data/masks/land_mask.nc
        variable: LAND_MASK

  - name: biogenic_isop
    species: ISOP
    hierarchy: 1
    operation: add
    file: /data/emissions/MEGAN_ISOP_2020.nc
    variable: ISOP_emis
    vertical_distribution:
      method: PBL
    scale_factors:
      - name: temperature_scale
        type: computed
        formula: "exp(0.1 * (T - 298.15))"

# Physics schemes
physics_schemes:
  - name: DMS
    enabled: true
    options:
      emission_factor: 1.0e-6
      temperature_threshold: 273.15

  - name: Dust
    enabled: true
    options:
      dust_source_strength: 1.0

# CDEPS configuration (optional)
cdeps:
  streams_file: /path/to/streams.txt
  data_root: /data/emissions

# Output configuration (for standalone mode)
output:
  directory: ./aces_output
  filename_pattern: "aces_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1
  fields:
    - CO
    - NOx
    - ISOP
  diagnostics: false

# Diagnostics configuration
diagnostics:
  enabled: true
  output_interval: 3600  # seconds
  fields:
    - intermediate_emissions
    - scale_factors_applied
```

## CDEPS Streams Configuration

CDEPS streams are configured in ESMF Config format. Example `streams.txt`:

```
# CDEPS Streams Configuration for ACES

streams:
  - name: anthro_emissions
    file_paths:
      - /data/emissions/CEDS_CO_anthro_2020.nc
    variables:
      - name_in_file: CO_emis
        name_in_model: CEDS_CO
    taxmode: cycle
    tintalgo: linear
    yearFirst: 2020
    yearLast: 2020
    yearAlign: 2020

  - name: biogenic_emissions
    file_paths:
      - /data/emissions/MEGAN_ISOP_2020.nc
    variables:
      - name_in_file: ISOP_emis
        name_in_model: MEGAN_ISOP
    taxmode: cycle
    tintalgo: linear
    yearFirst: 2020
    yearLast: 2020
    yearAlign: 2020
```

## Running ACES

### Standalone NUOPC Driver

The standalone NUOPC driver (`aces_nuopc_driver`) demonstrates the standard NUOPC lifecycle and how to manage ACES as a child model.

1.  **Configure**: Edit `aces_config.yaml` to specify your species, layers, and simulation parameters.
    The driver can be controlled via a `driver` block in `aces_config.yaml`:
    ```yaml
    driver:
      nx: 72
      ny: 46
      nz: 1
      start_year: 2024
      start_month: 1
      start_day: 1
      start_hour: 0
      stop_year: 2024
      stop_month: 1
      stop_day: 2
      stop_hour: 0
      timestep_seconds: 3600
    ```

2.  **Build**: The driver is built as part of the main project.

3.  **Run**:
    ```bash
    cd build
    ./bin/aces_nuopc_driver
    ```

### Basic Example Driver

ACES also provides a simpler `example_driver` for basic C++ integration tests.

1.  **Configure**: Edit `aces_config.yaml` to specify your species and layers.
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

# Property-based tests
ctest -L "property" --output-on-failure

# Integration tests
ctest -L "integration" --output-on-failure

# HEMCO parity tests
ctest -L "hemco" --output-on-failure
```

### Run Individual Tests

```bash
# Run a specific test
ctest -R test_mass_conservation_property --output-on-failure

# Run tests matching a pattern
ctest -R "vertical_distribution" --output-on-failure
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

**Problem**: CDEPS fails to read streams file
```
Error: Cannot open streams file /path/to/streams.txt
```

**Solution**: Verify the streams file path is correct and the file exists:
```bash
ls -la /path/to/streams.txt
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
./build/bin/aces_nuopc_driver
```

### GPU Performance

For GPU execution, set the device ID:
```bash
export ACES_DEVICE_ID=0
./build/bin/aces_nuopc_driver
```

## Output Files

When running in standalone mode with output enabled, ACES writes NetCDF files to the configured output directory. Files follow the naming pattern:
```
aces_YYYYMMDD_HHmmss.nc
```

Each file contains:
- Time coordinate variable (seconds since start time)
- All configured emission species fields
- Optional diagnostic fields (if enabled)

Files are CF-1.8 compliant and can be inspected with standard tools:
```bash
ncdump -h aces_20240101_000000.nc
```

## Next Steps

- Read the [Developer Guide](developer_guide.md) for architecture details
- Check [Physics Scheme Development](physics_scheme_development.md) for adding new schemes
- Review [HEMCO Migration Guide](hemco_migration.md) for migrating from HEMCO
- See [Examples](examples.md) for common use cases
