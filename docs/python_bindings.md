# Python Bindings

## Overview

ACES provides a Python interface through pybind11 bindings that directly expose the C++ core to Python. This replaces the previous ctypes + C-linkage architecture (`aces_c_bindings.cpp` + `_bindings.py`) with a single compiled extension module (`_aces_core`) that offers type-safe bindings, automatic memory management, and zero-copy NumPy interop.

The migration consolidates the 600+ line C-linkage wrapper (with `void*` casting and manual `malloc`/`free` string management) and the 250+ line ctypes signature declaration module into a single pybind11 module compiled at build time. The existing pure-Python API layer (`config.py`, `state.py`, `exceptions.py`, `utils.py`) is preserved as the user-facing interface.

## Architecture

```text
┌─────────────────────────────────────────────────────┐
│                  Python User Code                    │
│         aces.initialize() / aces.compute()           │
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
│          pybind11 Module (_aces_core.so)             │
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
│              C++ Core (libaces.so)                    │
│                                                      │
│  aces_config.hpp    aces_state.hpp                   │
│  aces_compute.hpp   aces_stacking_engine.hpp         │
│  aces_config_validator.hpp   aces_logger.hpp         │
│  aces_kokkos_config.hpp                              │
└─────────────────────────────────────────────────────┘
```

## Quick Start

```python
import aces
import numpy as np

# Load configuration from YAML file
config = aces.load_config("aces_config.yaml")

# Validate configuration
result = config.validate()
if not result.is_valid:
    for error in result.errors:
        print(f"Config error: {error}")

# Initialize ACES
aces.initialize(config)

# Create state with grid dimensions
state = aces.AcesState(nx=144, ny=96, nz=72)

# Add import fields (meteorological data, scale factors, etc.)
temperature = np.asfortranarray(np.random.rand(144, 96, 72))
state.add_import_field("TEMPERATURE", temperature)

# Run computation (GIL is released automatically)
aces.compute(state, hour=12, day_of_week=3, month=7)

# Retrieve results
co_emissions = state.get_export_field("CO_EMIS")
print(f"CO emissions shape: {co_emissions.shape}")

# Clean up
aces.finalize()
```

## Public Python API

### Module-Level Functions

#### `aces.initialize(config)`

Initialize ACES with a configuration. Parses and validates the configuration, initializes Kokkos if needed, and prepares the C++ core.

- `config` — File path (str), YAML string, dict, or `AcesConfig` object.
- Raises `AcesConfigError` if configuration is invalid.
- Raises `RuntimeError` if ACES is already initialized.

#### `aces.finalize()`

Clean up ACES resources and reset module state. Must be called before re-initializing.

- Raises `RuntimeError` if ACES is not initialized.

#### `aces.is_initialized()`

Returns `True` if ACES has been initialized and not yet finalized.

#### `aces.compute(state, config=None, hour=0, day_of_week=0, month=0)`

Execute the ACES emission computation. The GIL is released during the C++ computation.

- `state` — `AcesState` object with import fields populated.
- `config` — Optional configuration override. Uses initialized config if `None`.
- `hour` — Hour of day (0-23) for temporal scaling.
- `day_of_week` — Day of week (0-6) for temporal scaling.
- `month` — Month (1-12) for temporal scaling.
- Raises `RuntimeError` if not initialized or computation fails.
- Raises `AcesStateError` if state is invalid.

#### `aces.load_config(config)`

Load configuration from a file path, YAML string, dict, or existing `AcesConfig`.

- Returns an `AcesConfig` object.
- Raises `AcesConfigError` if the source is invalid.

#### `aces.set_execution_space(space)`

Validate that a Kokkos execution space is available. Accepts `"Serial"`, `"OpenMP"`, `"CUDA"`, or `"HIP"`.

- Raises `AcesExecutionSpaceError` if the space is not available.

#### `aces.get_execution_space()`

Returns the name of the current default Kokkos execution space.

#### `aces.get_available_execution_spaces()`

Returns a list of execution space names compiled into the current build.

#### `aces.set_log_level(level)`

Set the C++ logging level. Accepts `"DEBUG"`, `"INFO"`, `"WARNING"`, or `"ERROR"`.

#### `aces.get_diagnostics()`

Returns a dict with performance and timing diagnostics.

#### `aces.reset_diagnostics()`

Clear accumulated diagnostic data.

#### `aces.get_last_error()`

Returns the last error message string, or `None` if no error has occurred.

## Configuration Management

### AcesConfig

The top-level configuration container. Manages species definitions, physics schemes, data streams, and temporal cycles.

```python
# Create from YAML file
config = aces.load_config("config.yaml")

# Create from dictionary
config = aces.AcesConfig.from_dict({
    "species": {
        "CO": [{"field": "CO_ANTHRO", "operation": "add", "scale": 1.0}]
    }
})

# Create programmatically
config = aces.AcesConfig()
layer = aces.EmissionLayer(field_name="CO_ANTHRO", operation="add", scale=1.5)
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
from aces import VerticalDistributionConfig, EmissionLayer

# Distribute across a layer range
vdist = VerticalDistributionConfig(method="range", layer_start=0, layer_end=5)
layer = EmissionLayer(field_name="SO2_VOLC", vdist=vdist)

# Distribute by pressure range
vdist = VerticalDistributionConfig(method="pressure", p_start=500.0, p_end=900.0)
```

Supported methods: `"single"`, `"range"`, `"pressure"`, `"height"`, `"pbl"`.

## State Management

### AcesState

Container for 3D import and export fields on a fixed grid.

```python
state = aces.AcesState(nx=144, ny=96, nz=72)

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

### AcesField

Wraps a NumPy array with metadata (name, layout). Typically not constructed directly — created internally by `AcesState`.

```python
field = aces.AcesField("TEMPERATURE", temp_array, layout="fortran")
print(field.shape)   # (144, 96, 72)
print(field.dtype)   # float64
print(field.name)    # "TEMPERATURE"
```

## The `_aces_core` Low-Level Module

The `_aces_core` module is the compiled pybind11 extension (`_aces_core.cpython-*.so`) that directly wraps C++ types. Most users should use the high-level Python API above, but the low-level module is available for advanced use cases.

### Enums

- `_aces_core.VerticalDistributionMethod` — `SINGLE`, `RANGE`, `PRESSURE`, `HEIGHT`, `PBL`
- `_aces_core.VerticalCoordType` — `NONE`, `FV3`, `MPAS`, `WRF`

### Config Types

- `_aces_core.AcesConfig` — C++ config struct with `species_layers`, `met_mapping`, `scale_factor_mapping`, `mask_mapping`, `temporal_cycles`, `physics_schemes` as read-write properties
- `_aces_core.EmissionLayer` — C++ emission layer struct with all fields as read-write properties
- `_aces_core.ParseConfig(path)` — Parse a YAML config file, returns `AcesConfig`
- `_aces_core.AddSpecies(config, name, layers)` — Add species layers to a config
- `_aces_core.AddScaleFactor(config, name, field)` — Add a scale factor mapping
- `_aces_core.AddMask(config, name, field)` — Add a mask mapping

### State Types

- `_aces_core.AcesImportState` — Import state with `set_field(name, array)` and `get_field_names()`
- `_aces_core.AcesExportState` — Export state with `get_field(name)`, `set_field(name, array)`, and `get_field_names()`
- `_aces_core.AcesStateResolver` — Constructed from `(import_state, export_state, met_mapping, sf_mapping, mask_mapping)`

### Compute

- `_aces_core.compute_emissions(config, resolver, nx, ny, nz, hour, day_of_week, month)` — Run emission computation with GIL release
- `_aces_core.StackingEngine(config)` — Stacking engine with `Execute()` (GIL release), `ResetBindings()`, `AddSpecies()`

### Validator

- `_aces_core.ConfigValidator.validate_config(yaml_str)` — Validate YAML config string, returns `ValidationResult`
- `_aces_core.ValidationResult` — `is_valid`, `errors`, `warnings`, `IsValid()`, `GetErrorCount()`, `GetWarningCount()`
- `_aces_core.ValidationError` — `field`, `message`, `suggestion`

### Logger and Execution Space

- `_aces_core.set_log_level(level)` — Set C++ log level (`"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"`)
- `_aces_core.get_log_level()` — Get current log level as string
- `_aces_core.get_default_execution_space_name()` — Current Kokkos execution space
- `_aces_core.get_available_execution_spaces()` — List of compiled-in execution spaces
- `_aces_core.is_kokkos_initialized()` — Check if Kokkos is initialized
- `_aces_core.initialize_kokkos()` — Initialize Kokkos runtime

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
- Arrays must be 3D with shape `(nx, ny, nz)` matching the `AcesState` dimensions.

## GIL Release During Compute

The `compute_emissions` and `StackingEngine.Execute` functions release the Python GIL during execution via pybind11's `py::call_guard<py::gil_scoped_release>()`. This means:

- Other Python threads can run concurrently during ACES computation.
- You can use Python's `threading` module to overlap I/O with computation.
- NumPy arrays passed to the C++ layer must not be modified from other threads during computation.

```python
import threading

def run_aces(state, config):
    aces.compute(state, config)

# Computation runs without blocking other threads
thread = threading.Thread(target=run_aces, args=(state, config))
thread.start()
# ... do other work ...
thread.join()
```

## Exception Handling

All C++ exceptions are translated to the ACES Python exception hierarchy via `py::register_exception_translator`:

| C++ Exception | Python Exception | When |
| --- | --- | --- |
| `std::invalid_argument` | `AcesConfigError` | Invalid configuration |
| `std::out_of_range` | `AcesStateError` | Missing field, dimension mismatch |
| `std::runtime_error` | `AcesException` | General runtime errors |
| Unknown | `AcesException` | Any other C++ exception |

All exceptions include recovery suggestions:

```python
try:
    aces.initialize(bad_config)
except aces.AcesConfigError as e:
    print(e.message)
    print(e.error_code)
    for suggestion in e.recovery_suggestions:
        print(f"  - {suggestion}")
```

Exception hierarchy:

```text
Exception
└── AcesException
    ├── AcesConfigError
    ├── AcesComputationError
    ├── AcesStateError
    └── AcesExecutionSpaceError
```

## Kokkos Execution Space Configuration

ACES uses Kokkos for performance portability. The execution space is determined at compile time but can be queried from Python:

```python
# Check current execution space
print(aces.get_execution_space())  # e.g., "Serial" or "OpenMP"

# List all available spaces
spaces = aces.get_available_execution_spaces()
print(spaces)  # e.g., ["Serial", "OpenMP"]

# Validate a space is available
aces.set_execution_space("OpenMP")  # raises if not available
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
aces.set_log_level("DEBUG")    # Most verbose
aces.set_log_level("INFO")     # Default
aces.set_log_level("WARNING")  # Warnings and errors only
aces.set_log_level("ERROR")    # Errors only
```

For Python-side logging, use the utility function:

```python
from aces.utils import setup_logging, get_logger

setup_logging("DEBUG")
logger = get_logger(__name__)
logger.info("Starting ACES computation")
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

The compiled `_aces_core.cpython-*.so` extension is placed in the `aces` package directory alongside the Python source files.

### Running Tests

```bash
./setup.sh -c "cd build && ctest --output-on-failure"
```

### CMake Configuration

The `src/python/CMakeLists.txt` handles:

- Fetching pybind11 v2.12.0 via `FetchContent`
- Building the `_aces_core` extension module with `pybind11_add_module`
- Linking against `aces`, `Kokkos::kokkos`, and `yaml-cpp`
- Copying Python source files into the package output directory

## Migration Guide

If you had code using the old ctypes-based `_bindings` module directly, here are the key changes:

### What Changed

- The `_bindings.py` ctypes wrapper and `aces_c_bindings.cpp` C-linkage layer have been removed.
- All C++ interop now goes through the `_aces_core` pybind11 module.
- `void*` handle management and manual `malloc`/`free` string handling are eliminated.
- GIL release is handled automatically by pybind11 instead of manual `_release_gil` calls.

### What Stayed the Same

- The public Python API (`aces.initialize`, `aces.compute`, `aces.finalize`, etc.) is identical.
- `AcesConfig`, `AcesState`, `AcesField`, `EmissionLayer`, and all exception classes are unchanged.
- Configuration loading from YAML files, strings, and dicts works the same way.
- All existing Python scripts using the public API require no changes.

### For Direct Binding Users

If you imported from `aces._bindings` directly (not recommended), update your imports:

```python
# Old (removed)
from aces._bindings import AcesBindings

# New
from aces import _aces_core
# Use _aces_core.AcesConfig, _aces_core.AcesImportState, etc.
```

The pybind11 module exposes C++ types directly as Python classes, so there is no need for ctypes function signatures or manual memory management.
