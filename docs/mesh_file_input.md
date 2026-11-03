# GRIDSPEC File Input Support

## Source geometry for regridding

`driver.gridspec_file` selects the model/destination grid used by the standalone
driver. A stream's source geometry is configured separately under
`cece_data.streams` with `input_gridspec_file` (or its `gridspec_file` alias):

```yaml
cece_data:
  streams:
    - name: emissions
      file: emissions.nc
      input_gridspec_file: emissions_grid.nc
      longitude_variable: lon
      latitude_variable: lat
      geometry_reconstruction: none
```

When both stream path keys are present, they must have the same value. The
source coordinate variable names are optional; without them CECE detects
supported conventional names. Selected gridspec cell geometry takes
precedence over reconstruction. CECE does not infer cell edges
from centers by default. Use a non-default `geometry_reconstruction` only when
the input grid meets that model's assumptions; `curvilinear_approximate` is
explicitly approximate and does not qualify as authoritative conservative
geometry. Conservative methods require usable source and destination cell
boundaries with compatible geometry models. Center-only input remains valid for
sample methods such as nearest-neighbor.

### Geometry reconstruction policies

`geometry_reconstruction` is an explicit declaration for center-only grids; it
does not override usable bounds or gridspec geometry. The same values are
available on a stream (source geometry) and under `driver` (destination
geometry):

```yaml
driver:
  geometry_reconstruction: rectilinear_midpoint # destination grid

cece_data:
  streams:
    - name: emissions
      file: emissions.nc
      geometry_reconstruction: rectilinear_midpoint # this stream's source grid
```

Choose one of these exact scalar values: `none`, `rectilinear_midpoint`,
`gaussian_latlon`, and `curvilinear_approximate`. Their meanings are:

| Value | Meaning and appropriate use |
| --- | --- |
| `none` | Default. Do not infer boundaries. Supplied bounds/gridspec geometry are still used; center-based methods can work without cell boundaries. |
| `rectilinear_midpoint` | Infer edges halfway between separable 1-D longitude and latitude centers; extrapolate the outer edges from end spacing. For known rectilinear grids such as MACCity 4x5. |
| `gaussian_latlon` | Declares Gaussian latitude-band geometry. For a named `F<N>` destination grid, AXIS `NamedGridRegistry` already supplies its authoritative mesh, so this option is unnecessary. For other center-only grids, Gaussian weights or explicit bounds must be supplied by a supported geometry source; this setting alone cannot derive them. |
| `curvilinear_approximate` | Approximate corners from flattened 2-D curvilinear centers. Approximate only; not trusted for conservative remapping. |

For conservative mapping, prefer authoritative cell bounds or a supported
gridspec. Reconstruction is suitable only when the declared model accurately
describes the grid. In particular, do not choose midpoint reconstruction just
because it makes a center-only file pass validation.

### CF and UGRID metadata

CECE automatically recognizes supported geometry conventions in NetCDF files;
it does not require every file to have a separate `geometry_reconstruction`
setting:

- **CF rectilinear bounds:** 1-D `lon` and `lat` axes with `bounds` attributes
  naming two-vertex bounds arrays are read automatically. Both axes must have
  valid geographic units and bounds. Malformed or incomplete declared bounds
  cause an error instead of silently falling back to inferred edges.
- **UGRID/MPAS:** The mesh-topology route reads supported MPAS-style
  `latVertex`/`lonVertex`, `verticesOnCell`, and `nEdgesOnCell` arrays and uses
  their polygon connectivity. This is a specific supported layout, not a
  promise to read every possible UGRID encoding.
- **Other gridspecs:** CECE also has adapters for supported SCRIP, FV3/UFS
  `grid_spec`, and FV3 supergrid layouts. Use the relevant gridspec as the
  source or destination geometry.

When a CF `bounds` attribute is present, CECE validates the named arrays; it
does not ignore bad bounds and substitute a reconstruction policy. For other
CF curvilinear bounds layouts or UGRID variants, use a supported gridspec
adapter or provide a compatible explicit geometry file.

For rectilinear source data, the shared geometry loader also follows each CF
coordinate's `bounds` attribute when both longitude and latitude bounds are
present as two-vertex arrays. These supplied cell bounds are retained even for
center-only methods. If only one axis advertises bounds, or a selected bounds
variable has an invalid shape/value, geometry loading fails. A configured
`input_gridspec_file` takes precedence over bounds attached to the source data.
Curvilinear CF bounds and other non-rectilinear conventions should be selected
through a gridspec adapter until their ordering and boundary models are
supported directly.

Older stream configurations that put a source path in `gridspec_file` may keep
using that alias. For clarity, migrate it to `input_gridspec_file`; keep
`driver.gridspec_file` for the destination grid.

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

  # Grid dimensions — still used for the AXIS regridding mesh
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

1. **If `gridspec_file` is set**: the shared C++ grid resolver (`GridSpec::from_yaml`)
   reads the coordinate variables from the file via AMIO — CF packing unpacked,
   radians converted, longitudes wrapped — and classifies the topology from the
   coordinate shapes (1-D -> rectilinear, 2-D flattened -> curvilinear, `ny = 1`
   node arrays -> unstructured). Runtime grid generation is skipped. If the file
   cannot be loaded, the run fails loudly.
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

### Fortran cap (`src/driver/nuopc/cece_cap.F90`)

The cap does **not** open the GRIDSPEC file itself. In `InitializeRealize`:

1. With no parent grid (standalone launch), the cap calls
   `cece_sim_create_from_yaml`, which resolves the grid — including the
   `gridspec_file` coordinates — through the same shared C++ code as the
   standalone driver (`GridSpec::from_yaml`).
2. The resolved grid is read back via `cece_sim_grid_info` and the component
   is associated with a matching uniform ESMF grid (extents only), so
   framework consumers see the correct geometry.

## Curvilinear and Unstructured Targets Through the NUOPC Cap

The cap runs on all three grid topologies the writer supports, from either
grid source:

**Config-built (standalone cap launch).** Point the config at a 2-D-coordinate
GRIDSPEC file for a curvilinear target:

```yaml
driver:
  gridspec_file: "data/C96_grid_spec.tile1.nc"
  grid:
    nx: 96
    ny: 96
    nz: 1
```

or declare a flattened `ny = 1` node row for an unstructured target (uniform
extents, no gridspec file — see `examples/cece_config_ceds_oc_unstructured.yaml`):

```yaml
driver:
  grid:
    nx: 72
    ny: 1
```

**Parent-provided (coupled run).** When the host model associates an
`ESMF_Grid` or `ESMF_Mesh` with the component before realization, the cap
extracts the parent's global coordinates across PETs and builds the
simulation on them:

- 1-D `lon[nx]`/`lat[ny]` center coordinates -> rectilinear
- 2-D coordinates (flattened to `nx*ny` across the decomposition) ->
  curvilinear
- `ESMF_Mesh` node coordinates -> unstructured (`ny = 1` convention)

Radian grids are converted to degrees and longitudes wrapped to
`[-180, 180)` in the shared C++ code. The vertical layer count `nz` always
comes from `driver.grid.nz` in the config, never from the flat parent grid.
When both a parent grid and a YAML grid are present, the parent wins and a
single warning names the ignored YAML grid. An unsupported parent shape (a
coordinate length matching neither 1-D, nor `nx*ny`, nor the `ny = 1`
node-list convention) fails loudly at `InitializeRealize` — there is no
fallback to a uniform grid.

Parity of each topology between the cap and the C++ driver is verified by the
tests described in [NUOPC Cap Parity](nuopc_cap_parity.md).
