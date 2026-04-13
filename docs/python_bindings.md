# Python Bindings

## Overview

CECE provides a Python interface through pybind11 bindings that directly expose the C++ core to Python. This replaces the previous ctypes + C-linkage architecture (`cece_c_bindings.cpp` + `_bindings.py`) with a single compiled extension module (`_cece_core`) that offers type-safe bindings, automatic memory management, and zero-copy NumPy interop.

The migration consolidates the 600+ line C-linkage wrapper (with `void*` casting and manual `malloc`/`free` string management) and the 250+ line ctypes signature declaration module into a single pybind11 module compiled at build time. The existing pure-Python API layer (`config.py`, `state.py`, `exceptions.py`, `utils.py`) is preserved as the user-facing interface.

## Architecture

```text
┌─────────────────────────────────────────────────────┐
│                  Python User Code                    │
│         cece.initialize() / cece.compute()           │
└──────────────────────┬──────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────┐
│              Python API Layer (preserved)             │
│                                                      │
│  __init__.py   config.py   state.py   exceptions.py  │
│                          utils.py                    │
└──────────────────────┬──────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────┐
│          pybind11 Module (_cece_core.so)             │
│                                                      │
│  ┌──────────┐ ┌──────────┐ ┌───────────────────┐    │
│  │  Config   │ │  State   │ │     Compute       │    │
│  │ Bindings  │ │ Bindings │ │    Bindings       │    │
│  └──────────┘ └──────────┘ └───────────────────┘    │
│  ┌──────────┐ ┌──────────┐ ┌───────────────────┐    │
│  │  Enum    │ │Exception │ │  Logger/Validator  │    │
│  │ Bindings │ │Translators│ │    Bindings       │    │
│  └──────────┘ └──────────┘ └───────────────────┘    │
└──────────────────────┬──────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────┐
│              C++ Core (libcece.so)                    │
│                                                      │
│  cece_config.hpp    cece_state.hpp                   │
│  cece_compute.hpp   cece_stacking_engine.hpp         │
│  cece_config_validator.hpp   cece_logger.hpp         │
│  cece_kokkos_config.hpp                              │
└─────────────────────────────────────────────────────┘
```

## Quick Start

```python
import cece
import numpy as np

# Load configuration from YAML file
config = cece.load_config("cece_config.yaml")

# Validate configuration
result = config.validate()
if not result.is_valid:
    for error in result.errors:
        print(f"Config error: {error}")

# Initialize CECE
cece.initialize(config)

# Create state with grid dimensions
state = cece.CeceState(nx=144, ny=96, nz=72)

# Add import fields (meteorological data, scale factors, etc.)
temperature = np.asfortranarray(np.random.rand(144, 96, 72))
state.add_import_field("TEMPERATURE", temperature)

# Run computation (GIL is released automatically)
cece.compute(state, hour=12, day_of_week=3, month=7)

# Retrieve results
co_emissions = state.get_export_field("CO_EMIS")
print(f"CO emissions shape: {co_emissions.shape}")

# Clean up
cece.finalize()
```

## Public Python API

### Module-Level Functions

#### `cece.initialize(config)`

Initialize CECE with a configuration. Parses and validates the configuration, initializes Kokkos if needed, and prepares the C++ core.

- `config` — File path (str), YAML string, dict, or `CeceConfig` object.
- Raises `CeceConfigError` if configuration is invalid.
- Raises `RuntimeError` if CECE is already initialized.

#### `cece.finalize()`

Clean up CECE resources and reset module state. Must be called before re-initializing.

- Raises `RuntimeError` if CECE is not initialized.

#### `cece.is_initialized()`

Returns `True` if CECE has been initialized and not yet finalized.

#### `cece.compute(state, config=None, hour=0, day_of_week=0, month=0)`

Execute the CECE emission computation. The GIL is released during the C++ computation.

- `state` — `CeceState` object with import fields populated.
- `config` — Optional configuration override. Uses initialized config if `None`.
- `hour` — Hour of day (0-23) for temporal scaling.
- `day_of_week` — Day of week (0-6) for temporal scaling.
- `month` — Month (1-12) for temporal scaling.
- Raises `RuntimeError` if not initialized or computation fails.
- Raises `CeceStateError` if state is invalid.

#### `cece.load_config(config)`

Load configuration from a file path, YAML string, dict, or existing `CeceConfig`.

- Returns an `CeceConfig` object.
- Raises `CeceConfigError` if the source is invalid.

#### `cece.set_execution_space(space)`

Validate that a Kokkos execution space is available. Accepts `"Serial"`, `"OpenMP"`, `"CUDA"`, or `"HIP"`.

- Raises `CeceExecutionSpaceError` if the space is not available.

#### `cece.get_execution_space()`

Returns the name of the current default Kokkos execution space.

#### `cece.get_available_execution_spaces()`

Returns a list of execution space names compiled into the current build.

#### `cece.set_log_level(level)`

Set the C++ logging level. Accepts `"DEBUG"`, `"INFO"`, `"WARNING"`, or `"ERROR"`.

#### `cece.get_diagnostics()`

Returns a dict with performance and timing diagnostics.

#### `cece.reset_diagnostics()`

Clear accumulated diagnostic data.

#### `cece.get_last_error()`

Returns the last error message string, or `None` if no error has occurred.

## Configuration Management

### CeceConfig

The top-level configuration container. Manages species definitions, physics schemes, data streams, and temporal cycles.

```python
# Create from YAML file
config = cece.load_config("config.yaml")

# Create from dictionary
config = cece.CeceConfig.from_dict({
    "species": {
        "CO": [{"field": "CO_ANTHRO", "operation": "add", "scale": 1.0}]
    }
})

# Create programmatically
config = cece.CeceConfig()
layer = cece.EmissionLayer(field_name="CO_ANTHRO", operation="add", scale=1.5)
config.add_species("CO", [layer])
config.add_physics_scheme("megan", language="fortran")

# Validate
result = config.validate()
assert result.is_valid

# Serialize
yaml_str = config.to_yaml()
config_dict = config.to_dict()
```

### EmissionLayer

Represents a single emission data layer contributing to a species.

| Attribute | Type | Default | Description |
| --- | --- | --- | --- |
| `field_name` | str | (required) | Import field name |
| `operation` | str | `"add"` | `"add"`, `"replace"`, or `"scale"` |
| `scale` | float | `1.0` | Multiplicative scale factor |
| `hierarchy` | int | `0` | Priority level for stacking |
| `masks` | list | `[]` | Mask field names |
| `diurnal_cycle` | str | `None` | Diurnal cycle name |
| `weekly_cycle` | str | `None` | Weekly cycle name |
| `seasonal_cycle` | str | `None` | Seasonal cycle name |
| `vdist` | VerticalDistributionConfig | default | Vertical distribution |

### VerticalDistributionConfig

Controls vertical distribution of emissions across model levels.

```python
from cece import VerticalDistributionConfig, EmissionLayer

# Distribute across a layer range
vdist = VerticalDistributionConfig(method="range", layer_start=0, layer_end=5)
layer = EmissionLayer(field_name="SO2_VOLC", vdist=vdist)

# Distribute by pressure range
vdist = VerticalDistributionConfig(method="pressure", p_start=500.0, p_end=900.0)
```

Supported methods: `"single"`, `"range"`, `"pressure"`, `"height"`, `"pbl"`.

## State Management

### CeceState

Container for 3D import and export fields on a fixed grid.

```python
state = cece.CeceState(nx=144, ny=96, nz=72)

# Add import fields
temp = np.asfortranarray(np.zeros((144, 96, 72)))
state.add_import_field("TEMPERATURE", temp)

# Access fields via dictionary-like interface
state.import_fields["TEMPERATURE"]  # returns numpy array
"TEMPERATURE" in state.import_fields  # True

# After compute(), access export fields
co = state.get_export_field("CO_EMIS")

# Check dimensions
print(state.dimensions)  # (144, 96, 72)
```

### CeceField

Wraps a NumPy array with metadata (name, layout). Typically not constructed directly — created internally by `CeceState`.

```python
field = cece.CeceField("TEMPERATURE", temp_array, layout="fortran")
print(field.shape)   # (144, 96, 72)
print(field.dtype)   # float64
print(field.name)    # "TEMPERATURE"
```

## The `_cece_core` Low-Level Module

The `_cece_core` module is the compiled pybind11 extension (`_cece_core.cpython-*.so`) that directly wraps C++ types. Most users should use the high-level Python API above, but the low-level module is available for advanced use cases.

### Enums

- `_cece_core.VerticalDistributionMethod` — `SINGLE`, `RANGE`, `PRESSURE`, `HEIGHT`, `PBL`
- `_cece_core.VerticalCoordType` — `NONE`, `FV3`, `MPAS`, `WRF`

### Config Types

- `_cece_core.CeceConfig` — C++ config struct with `species_layers`, `met_mapping`, `scale_factor_mapping`, `mask_mapping`, `temporal_cycles`, `physics_schemes` as read-write properties
- `_cece_core.EmissionLayer` — C++ emission layer struct with all fields as read-write properties
- `_cece_core.ParseConfig(path)` — Parse a YAML config file, returns `CeceConfig`
- `_cece_core.AddSpecies(config, name, layers)` — Add species layers to a config
- `_cece_core.AddScaleFactor(config, name, field)` — Add a scale factor mapping
- `_cece_core.AddMask(config, name, field)` — Add a mask mapping

### State Types

- `_cece_core.CeceImportState` — Import state with `set_field(name, array)` and `get_field_names()`
- `_cece_core.CeceExportState` — Export state with `get_field(name)`, `set_field(name, array)`, and `get_field_names()`
- `_cece_core.CeceStateResolver` — Constructed from `(import_state, export_state, met_mapping, sf_mapping, mask_mapping)`

### Compute

- `_cece_core.compute_emissions(config, resolver, nx, ny, nz, hour, day_of_week, month)` — Run emission computation with GIL release
- `_cece_core.StackingEngine(config)` — Stacking engine with `Execute()` (GIL release), `ResetBindings()`, `AddSpecies()`

### Validator

- `_cece_core.ConfigValidator.validate_config(yaml_str)` — Validate YAML config string, returns `ValidationResult`
- `_cece_core.ValidationResult` — `is_valid`, `errors`, `warnings`, `IsValid()`, `GetErrorCount()`, `GetWarningCount()`
- `_cece_core.ValidationError` — `field`, `message`, `suggestion`

### Logger and Execution Space

- `_cece_core.set_log_level(level)` — Set C++ log level (`"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"`)
- `_cece_core.get_log_level()` — Get current log level as string
- `_cece_core.get_default_execution_space_name()` — Current Kokkos execution space
- `_cece_core.get_available_execution_spaces()` — List of compiled-in execution spaces
- `_cece_core.is_kokkos_initialized()` — Check if Kokkos is initialized
- `_cece_core.initialize_kokkos()` — Initialize Kokkos runtime

## NumPy Zero-Copy Data Transfer

The pybind11 bindings use zero-copy data transfer between NumPy arrays and Kokkos Views where possible. All field arrays use Fortran-contiguous (column-major) layout to match Kokkos `LayoutLeft`.

### Import Path (Python to C++)

1. A Fortran-contiguous (`order='F'`) float64 NumPy array is wrapped directly by an `UnmanagedHostView3D` — no data copy on the host side.
2. The data is then deep-copied into a `DualView3D` and synced to the device (GPU) for computation.
3. If a C-contiguous array is provided, it is first converted to Fortran order (with a warning about the copy overhead).

### Export Path (C++ to Python)

1. After computation, the `DualView3D` is synced back to the host.
2. A NumPy array is returned that directly references the host View's data pointer — zero-copy.
3. A `py::capsule` prevents the Kokkos View from being deallocated while the NumPy array is alive.

### Best Practices

- Always provide Fortran-contiguous arrays to avoid unnecessary copies:

  ```python
  data = np.asfortranarray(np.zeros((nx, ny, nz), dtype=np.float64))
  ```

- All arrays must be `float64` (double precision).
- Arrays must be 3D with shape `(nx, ny, nz)` matching the `CeceState` dimensions.

## GIL Release During Compute

The `compute_emissions` and `StackingEngine.Execute` functions release the Python GIL during execution via pybind11's `py::call_guard<py::gil_scoped_release>()`. This means:

- Other Python threads can run concurrently during CECE computation.
- You can use Python's `threading` module to overlap I/O with computation.
- NumPy arrays passed to the C++ layer must not be modified from other threads during computation.

```python
import threading

def run_cece(state, config):
    cece.compute(state, config)

# Computation runs without blocking other threads
thread = threading.Thread(target=run_cece, args=(state, config))
thread.start()
# ... do other work ...
thread.join()
```

## Exception Handling

All C++ exceptions are translated to the CECE Python exception hierarchy via `py::register_exception_translator`:

| C++ Exception | Python Exception | When |
| --- | --- | --- |
| `std::invalid_argument` | `CeceConfigError` | Invalid configuration |
| `std::out_of_range` | `CeceStateError` | Missing field, dimension mismatch |
| `std::runtime_error` | `CeceException` | General runtime errors |
| Unknown | `CeceException` | Any other C++ exception |

All exceptions include recovery suggestions:

```python
try:
    cece.initialize(bad_config)
except cece.CeceConfigError as e:
    print(e.message)
    print(e.error_code)
    for suggestion in e.recovery_suggestions:
        print(f"  - {suggestion}")
```

Exception hierarchy:

```text
Exception
└── CeceException
    ├── CeceConfigError
    ├── CeceComputationError
    ├── CeceStateError
    └── CeceExecutionSpaceError
```

## Kokkos Execution Space Configuration

CECE uses Kokkos for performance portability. The execution space is determined at compile time but can be queried from Python:

```python
# Check current execution space
print(cece.get_execution_space())  # e.g., "Serial" or "OpenMP"

# List all available spaces
spaces = cece.get_available_execution_spaces()
print(spaces)  # e.g., ["Serial", "OpenMP"]

# Validate a space is available
cece.set_execution_space("OpenMP")  # raises if not available
```

Supported execution spaces (compile-time selection):

- `Serial` — Single-threaded CPU execution
- `OpenMP` — Multi-threaded CPU execution
- `CUDA` — NVIDIA GPU execution
- `HIP` — AMD GPU execution

## Logging Configuration

Control the C++ logging verbosity from Python:

```python
# Set log level
cece.set_log_level("DEBUG")    # Most verbose
cece.set_log_level("INFO")     # Default
cece.set_log_level("WARNING")  # Warnings and errors only
cece.set_log_level("ERROR")    # Errors only
```

For Python-side logging, use the utility function:

```python
from cece.utils import setup_logging, get_logger

setup_logging("DEBUG")
logger = get_logger(__name__)
logger.info("Starting CECE computation")
```

## Build Instructions

### Prerequisites

- CMake 3.16+
- C++17 compiler
- Python 3.8+ with development headers
- Kokkos (fetched automatically via CMake FetchContent)
- pybind11 v2.12.0 (fetched automatically via CMake FetchContent)

### Building with Python Bindings

Enable Python bindings with the `BUILD_PYTHON_BINDINGS` option:

```bash
./setup.sh -c "mkdir -p build && cd build && cmake .. -DBUILD_PYTHON_BINDINGS=ON && make -j4"
```

The compiled `_cece_core.cpython-*.so` extension is placed in the `cece` package directory alongside the Python source files.

### Running Tests

```bash
./setup.sh -c "cd build && ctest --output-on-failure"
```

### CMake Configuration

The `src/python/CMakeLists.txt` handles:

- Fetching pybind11 v2.12.0 via `FetchContent`
- Building the `_cece_core` extension module with `pybind11_add_module`
- Linking against `cece`, `Kokkos::kokkos`, and `yaml-cpp`
- Copying Python source files into the package output directory

## Migration Guide

If you had code using the old ctypes-based `_bindings` module directly, here are the key changes:

### What Changed

- The `_bindings.py` ctypes wrapper and `cece_c_bindings.cpp` C-linkage layer have been removed.
- All C++ interop now goes through the `_cece_core` pybind11 module.
- `void*` handle management and manual `malloc`/`free` string handling are eliminated.
- GIL release is handled automatically by pybind11 instead of manual `_release_gil` calls.

### What Stayed the Same

- The public Python API (`cece.initialize`, `cece.compute`, `cece.finalize`, etc.) is identical.
- `CeceConfig`, `CeceState`, `CeceField`, `EmissionLayer`, and all exception classes are unchanged.
- Configuration loading from YAML files, strings, and dicts works the same way.
- All existing Python scripts using the public API require no changes.

### For Direct Binding Users

If you imported from `cece._bindings` directly (not recommended), update your imports:

```python
# Old (removed)
from cece._bindings import CeceBindings

# New
from cece import _cece_core
# Use _cece_core.CeceConfig, _cece_core.CeceImportState, etc.
```

The pybind11 module exposes C++ types directly as Python classes, so there is no need for ctypes function signatures or manual memory management.
