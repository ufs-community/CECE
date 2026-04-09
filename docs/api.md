# API Reference

The following sections provide detailed documentation for the ACES C++ API, generated from the source code.

## Core Classes

::: doxy.aces.Class
    name: aces::PhysicsScheme

::: doxy.aces.Class
    name: aces::AcesDiagnosticManager

::: doxy.aces.Class
    name: aces::PhysicsFactory

## Configuration Structures

::: doxy.aces.Class
    name: aces::AcesConfig

::: doxy.aces.Class
    name: aces::PhysicsSchemeConfig

::: doxy.aces.Class
    name: aces::EmissionLayer

## Data Structures

::: doxy.aces.Class
    name: aces::AcesImportState

::: doxy.aces.Class
    name: aces::AcesExportState

## Python API

The ACES Python interface provides a high-level API for configuring and running emission computations from Python. It is built on pybind11 bindings to the C++ core, with automatic NumPy zero-copy data transfer and GIL release during computation.

For full documentation, see the [Python Bindings](python_bindings.md) page.

### Module Functions

| Function | Description |
|----------|-------------|
| `aces.initialize(config)` | Initialize ACES with a configuration (file path, YAML string, dict, or `AcesConfig`) |
| `aces.finalize()` | Clean up ACES resources |
| `aces.is_initialized()` | Check if ACES is initialized |
| `aces.compute(state, config, hour, day_of_week, month)` | Execute emission computation with GIL release |
| `aces.load_config(config)` | Load configuration from various sources |
| `aces.set_execution_space(space)` | Validate a Kokkos execution space is available |
| `aces.get_execution_space()` | Get current Kokkos execution space name |
| `aces.get_available_execution_spaces()` | List compiled-in execution spaces |
| `aces.set_log_level(level)` | Set C++ logging level |
| `aces.get_diagnostics()` | Get performance diagnostics |
| `aces.reset_diagnostics()` | Clear diagnostic data |
| `aces.get_last_error()` | Get last error message |

### Configuration Classes

| Class | Description |
|-------|-------------|
| `aces.AcesConfig` | Top-level configuration container with species, physics schemes, data streams |
| `aces.EmissionLayer` | Single emission layer with operation, scale, masks, vertical distribution |
| `aces.VerticalDistributionConfig` | Vertical distribution parameters (single, range, pressure, height, pbl) |

### State Classes

| Class | Description |
|-------|-------------|
| `aces.AcesState` | Container for 3D import/export fields on a fixed grid |
| `aces.AcesField` | Named NumPy array wrapper with layout metadata |

### Exception Classes

| Exception | Description |
|-----------|-------------|
| `aces.AcesException` | Base exception for all ACES errors |
| `aces.AcesConfigError` | Configuration validation or parsing error |
| `aces.AcesComputationError` | Error during computation execution |
| `aces.AcesStateError` | Error in state management (missing fields, dimension mismatch) |
| `aces.AcesExecutionSpaceError` | Kokkos execution space not available |
