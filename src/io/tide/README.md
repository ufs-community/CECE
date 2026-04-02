# TIDE (Temporal Interpolation & Data Extraction)

TIDE is a lean and mean ESMF-enabled library designed to reliably read emission datasets and other earth system modeling datasets. It provides a simple high-level API for use within NUOPC caps, replacing complex XML configurations with a clean YAML-based system.

## Key Features
- **Lean API**: Simple `tide_init`, `tide_advance`, and `tide_get_ptr` calls.
- **YAML Configuration**: Easy-to-manage stream definitions using `yaml-cpp`.
- **ESMF Integration**: Full support for spatial regridding using ESMF Mesh and RouteHandles.
- **Temporal Interpolation**: Flexible time interpolation algorithms (linear, nearest, etc.).
- **PIO Support**: Efficient parallel I/O for NetCDF datasets.

## Build Requirements
- CMake 3.10+
- ESMF
- PIO (ParallelIO)
- MPI
- yaml-cpp (automatically fetched via CMake if not found)

## High-Level API Usage
```fortran
use tide_mod, only: tide_type, tide_init, tide_advance, tide_get_ptr
...
type(tide_type) :: tide
call tide_init(tide, config_yaml="streams.yaml", model_mesh=mesh, clock=clock, rc=rc)
call tide_advance(tide, clock, rc=rc)
call tide_get_ptr(tide, "so2_flux", ptr, rc)
```

## License
TIDE is based on the original CDEPS library.
