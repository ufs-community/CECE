# API Reference

The following sections provide detailed documentation for the CECE C++ API, generated from the source code.

## Core Classes

::: doxy.cece.Class
    name: cece::PhysicsScheme

::: doxy.cece.Class
    name: cece::CeceDiagnosticManager

::: doxy.cece.Class
    name: cece::PhysicsFactory

## Configuration Structures

::: doxy.cece.Class
    name: cece::CeceConfig

::: doxy.cece.Class
    name: cece::PhysicsSchemeConfig

::: doxy.cece.Class
    name: cece::EmissionLayer

## Data Structures

::: doxy.cece.Class
    name: cece::CeceImportState

::: doxy.cece.Class
    name: cece::CeceExportState

## Python API

The CECE Python interface provides a high-level API for configuring and running emission computations from Python. It is built on pybind11 bindings to the C++ core, with automatic NumPy zero-copy data transfer and GIL release during computation.

For full documentation, see the [Python Bindings](python_bindings.md) page.

### Module Functions

| Function | Description |
|----------|-------------|
| `cece.initialize(config)` | Initialize CECE with a configuration (file path, YAML string, dict, or `CeceConfig`) |
| `cece.finalize()` | Clean up CECE resources |
| `cece.is_initialized()` | Check if CECE is initialized |
| `cece.compute(state, config, hour, day_of_week, month)` | Execute emission computation with GIL release |
| `cece.load_config(config)` | Load configuration from various sources |
| `cece.set_execution_space(space)` | Validate a Kokkos execution space is available |
| `cece.get_execution_space()` | Get current Kokkos execution space name |
| `cece.get_available_execution_spaces()` | List compiled-in execution spaces |
| `cece.set_log_level(level)` | Set C++ logging level |
| `cece.get_diagnostics()` | Get performance diagnostics |
| `cece.reset_diagnostics()` | Clear diagnostic data |
| `cece.get_last_error()` | Get last error message |

### Configuration Classes

| Class | Description |
|-------|-------------|
| `cece.CeceConfig` | Top-level configuration container with species, physics schemes, data streams |
| `cece.EmissionLayer` | Single emission layer with operation, scale, masks, vertical distribution |
| `cece.VerticalDistributionConfig` | Vertical distribution parameters (single, range, pressure, height, pbl) |

### State Classes

| Class | Description |
|-------|-------------|
| `cece.CeceState` | Container for 3D import/export fields on a fixed grid |
| `cece.CeceField` | Named NumPy array wrapper with layout metadata |

### Exception Classes

| Exception | Description |
|-----------|-------------|
| `cece.CeceException` | Base exception for all CECE errors |
| `cece.CeceConfigError` | Configuration validation or parsing error |
| `cece.CeceComputationError` | Error during computation execution |
| `cece.CeceStateError` | Error in state management (missing fields, dimension mismatch) |
| `cece.CeceExecutionSpaceError` | Kokkos execution space not available |
