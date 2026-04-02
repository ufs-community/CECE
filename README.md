# ACES: Atmospheric Chemistry Emission System

ACES is a high-performance, performance-portable emissions compute component for Earth System Models. It is built using C++20 and the Kokkos programming model, designed to run efficiently on both multi-core CPUs and GPUs.

## Quick Start

### 1. Development Environment
The recommended way to develop ACES is using the provided Docker container:
```bash
./setup.sh
```

### 2. Build
Inside the container:
```bash
mkdir build && cd build
cmake ..
make -j4
```

### 3. Run Examples
ACES provides several example configurations to demonstrate different capabilities:

**Run a basic example:**
```bash
./setup.sh -c "cd /work && ./build/bin/aces_nuopc_app examples/aces_config_ex1.yaml"
```

**Available examples:**
- `aces_config_ex1.yaml` - Basic single species (CO) emission processing
- `aces_config_ex3.yaml` - Minimal configuration for quick testing
- `aces_config_ex5.yaml` - Multi-species production example (CO + NO)

### 4. Configuration

ACES uses a single YAML configuration file with fully configurable parameters:

```yaml
# Timing configuration
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-01T06:00:00"
  timestep_seconds: 3600

# Spatial grid configuration
grid:
  nx: 4
  ny: 4
  lon_min: -135.0
  lon_max: 135.0
  lat_min: -67.5
  lat_max: 67.5

# Species and emission data streams
species:
  co:
    - field: "MACCITY_CO"
      operation: "add"

aces_data:
  streams:
    - name: "MACCITY_CO"
      file: "/work/data/MACCity_4x5.nc"
      # ... stream configuration
```

## Documentation
Comprehensive documentation is available in the `docs/` directory:

- [User's Guide](docs/users-guide.md) - Getting started and basic usage
- [Configuration Reference](docs/configuration.md) - Complete YAML configuration options
- [Tutorial](docs/tutorial.md) - Step-by-step examples
- [Examples Guide](examples/README.md) - Detailed example descriptions

## Architecture

ACES is built on a modular architecture:
- **NUOPC Integration:** Earth System Model component interface
- **TIDE Data Ingestion:** High-performance emission data processing with conservative regridding
- **Kokkos Compute:** Performance-portable parallel execution (CPU/GPU)
- **Physics Schemes:** Extensible emission processing algorithms
- **YAML Configuration:** Runtime configuration without recompilation

## Requirements

- C++20 compiler
- MPI implementation
- ESMF/NUOPC framework
- Kokkos (included)
- yaml-cpp (included)

## Development

See [AGENTS.md](AGENTS.md) for detailed developer guidelines and coding standards.
