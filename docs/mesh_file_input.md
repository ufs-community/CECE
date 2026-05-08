# GRIDSPEC File Input Support

## Overview

The CECE driver supports loading a pre-generated ESMF GRIDSPEC NetCDF file for spatial discretization.
This avoids constructing the structured grid at runtime, which can be slow for large grids (e.g., global 0.1° resolution).

## Configuration

Set `gridspec_file` under the `driver:` section of the CECE YAML config:

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600

  # Load pre-generated ESMF GRIDSPEC file (optional)
  gridspec_file: "/path/to/grid.nc"

  # Grid dimensions — still used for the TIDE regridding mesh
  grid:
    nx: 3600
    ny: 1800
    lon_min: -180.0
    lon_max:  180.0
    lat_min:  -90.0
    lat_max:   90.0
```

If `gridspec_file` is absent or empty the grid is generated at runtime from `driver.grid` as usual.

## Generating a GRIDSPEC File

Use the provided Python script:

```bash
python scripts/cece_make_gridspec.py cece_config.yaml [output.nc]
```

If no output filename is given, one is auto-generated from the grid parameters, e.g.:
`cece_grid_nx3600_ny1800_nz1_lonN180_180_latN90_90.nc`

Requirements: `pyyaml`, `netCDF4`, `numpy`.

## Grid/Mesh Selection Logic

1. **If `gridspec_file` is set**: Load grid via `ESMF_GridCreate(filename=..., fileformat=ESMF_FILEFORMAT_GRIDSPEC)`; skip runtime grid generation.
   A TIDE regridding mesh is still built from `driver.grid` parameters.
2. **If `gridspec_file` is absent/empty**: Generate structured grid from `driver.grid.nx`/`ny`/bounds as usual.

## GRIDSPEC File Requirements

The NetCDF file must follow CF conventions as written by `cece_make_gridspec.py`:

- Variables `lon` (dim `lon`) and `lat` (dim `lat`) with `units="degrees_east"`/`"degrees_north"`
- Optional `lon_bnds` / `lat_bnds` for cell bounds
- Global attribute `Conventions: CF-1.8`

## Example Configurations

### Using a pre-generated GRIDSPEC file

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  gridspec_file: "/data/grids/global_0.1deg.nc"
  grid:
    nx: 3600
    ny: 1800
    lon_min: -180.0
    lon_max:  180.0
    lat_min:  -90.0
    lat_max:   90.0
```

### Generating a grid at runtime (default)

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  grid:
    nx: 360
    ny: 180
```

## Implementation Details

### C++ config struct (`include/cece/cece_config.hpp`)

```cpp
struct DriverConfig {
    std::string gridspec_file;   // empty = generate from driver.grid
    DriverGridConfig grid;
    ...
};
```

### C accessor (`src/cece_core_field_helpers.cpp`)

```cpp
void cece_core_get_gridspec_file_path(void* data_ptr, char* path, int* path_len, int* rc);
```

### Fortran cap (`src/cece_cap.F90`)

In `CECE_InitializeRealize` standalone branch:
1. Call `cece_core_get_gridspec_file_path` to retrieve the path.
2. If non-empty, call `ESMF_GridCreate(filename=..., fileformat=ESMF_FILEFORMAT_GRIDSPEC)`.
3. Regardless, call `CreateMeshFromConfig` to build the TIDE regridding mesh.


1. Support for multiple mesh file formats (SCRIP, UGRID, etc.)
2. Automatic mesh generation from grid specifications
3. Mesh refinement and coarsening
4. Mesh partitioning for MPI decomposition
5. Mesh quality metrics and diagnostics
