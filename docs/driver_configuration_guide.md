# CECE Driver Configuration Guide

## Overview

The CECE standalone NUOPC driver (`cece_nuopc_single_driver`) supports configurable execution modes for both single-process and MPI multi-process simulations. This guide documents all configuration options and usage patterns.

### Shared Orchestration Contract

CECE exposes two entry points — the C++ standalone driver (`cece_standalone_driver`,
built from `src/main.cpp`) and the Fortran NUOPC cap (`cece_nuopc_app`, built from
`src/driver/nuopc/`) — but they are **not two implementations**. Both drive a single
shared C++ orchestration core, `CeceSimulation` (declared in `include/cece/cece_simulation.hpp`),
through a small C ABI (`include/cece/cece_sim_c_abi.h`, implemented in
`src/driver/cece_sim_c_abi.cpp` and exported from the `cece_driver` library):

- `cece_sim_create_from_yaml` / `cece_sim_create_from_esmf` — resolve the grid and build
  the simulation (core init, export-field registration, driver orchestrator, output writer).
- `cece_sim_step` — advance one timestep (ingest, regrid, physics, stack, write).
- `cece_sim_finalize` — release resources.

Because grid resolution, the run loop, and NetCDF export all live in that shared core,
the two drivers produce **byte-for-byte identical output** for the same configuration,
and every feature of the C++ driver is available through the NUOPC cap. The cap adds
only the ESMF/NUOPC lifecycle glue (clock, state import/export, parent-grid extraction).
See [NUOPC Cap Parity](nuopc_cap_parity.md) for how to run the parity checks.

### Time Convention

Both drivers use the same converged time convention, so a given config yields the same
ingest instants and output stamps regardless of which driver runs it:

- **Ingest at step-start.** The data stream is read/regridded at the *start* instant of
  the step (the time the step represents).
- **Stamp at step-end.** The output record is timestamped at the *end* of the step — the
  elapsed seconds are measured from `start_time`, matching the historical standalone
  driver. For a step from `T` to `T + dt`, ingest happens at `T` and the file is stamped
  `T + dt`.

The core clock owns the derived calendar quantities (hour, day-of-week, month); the
drivers supply only the ingest instant and the stamp elapsed. `start_time`, `end_time`,
and `timestep_seconds` below are interpreted under this convention on both paths.

## Configuration File Format

Driver configuration is specified in the CECE YAML configuration file under the optional `driver` section:

```yaml
driver:
  start_time: "2020-01-01T00:00:00"      # ISO8601 format (optional, default: 2020-01-01T00:00:00)
  end_time: "2020-06-16T12:00:00"        # ISO8601 format (must be after start_time)
  timestep_seconds: 3600                 # Positive integer (optional, default: 3600)
  stacking_refresh_interval_seconds: 0   # Positive multiple of timestep_seconds (optional, default: 0 = use timestep_seconds)
  gridspec_file: null                    # Path to ESMF GRIDSPEC NetCDF file (optional, default: null - generate grid)
  grid:
    nx: 4                                # Positive integer (optional, default: 4)
    ny: 4                                # Positive integer (optional, default: 4)
```

## Configuration Parameters

### start_time

**Type:** String (ISO8601 format)
**Format:** `YYYY-MM-DDTHH:MM:SS`
**Default:** `2020-01-01T00:00:00`
**Description:** Simulation start time in ISO8601 format

**Example:**
```yaml
driver:
  start_time: "2020-06-15T12:00:00"
  end_time: "2020-06-16T12:00:00"
```

### end_time

**Type:** String (ISO8601 format)
**Format:** `YYYY-MM-DDTHH:MM:SS`
**Default:** `2020-01-02T00:00:00`
**Description:** Simulation end time in ISO8601 format. Must be after start_time.

**Example:**
```yaml
driver:
  start_time: "2020-06-15T12:00:00"
  end_time: "2020-06-16T12:00:00"
```

### timestep_seconds

**Type:** Integer
**Range:** > 0
**Default:** `3600` (1 hour)
**Description:** Base simulation timestep duration in seconds. All component `refresh_interval_seconds` values must be integer multiples of this value.

**Example:**
```yaml
driver:
  timestep_seconds: 1800  # 30 minutes
```

### stacking_refresh_interval_seconds

**Type:** Integer
**Range:** > 0, must be a multiple of `timestep_seconds`
**Default:** `0` (use `timestep_seconds`, i.e., stacking runs every step)
**Description:** Execution interval for the stacking engine in seconds. When set, the stacking engine only combines emission layers at this cadence rather than every timestep. Useful when stacking is expensive and input data changes slowly.

**Example:**
```yaml
driver:
  timestep_seconds: 300
  stacking_refresh_interval_seconds: 3600  # Stack hourly instead of every 5 min
```

### gridspec_file

**Type:** String (file path) or null
**Default:** `null`
**Description:** Path to an ESMF GRIDSPEC NetCDF file for spatial discretization. If set, the grid is loaded from this file and grid generation is skipped. Generate one with `scripts/cece_make_gridspec.py`.

**Example:**
```yaml
driver:
  gridspec_file: "/path/to/grid.nc"
```

### grid.nx

**Type:** Integer
**Range:** > 0
**Default:** `4`
**Description:** Number of grid points in X direction (longitude). Only used if gridspec_file is null.

**Example:**
```yaml
driver:
  grid:
    nx: 360
```

### grid.ny

**Type:** Integer
**Range:** > 0
**Default:** `4`
**Description:** Number of grid points in Y direction (latitude). Only used if gridspec_file is null.

**Example:**
```yaml
driver:
  grid:
    ny: 180
```

## Execution Modes

### Single-Process Execution

For single-process execution (petCount == 1):

- Driver creates a global ESMF_Grid covering the full domain
- Grid dimensions are [nx, ny] as specified in configuration
- No domain decomposition is performed
- All grid points are available on the single process

**Example Configuration:**
```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  grid:
    nx: 360
    ny: 180
```

**Usage:**
```bash
./cece_nuopc_single_driver
```

### MPI Multi-Process Execution

For MPI multi-process execution (petCount > 1):

- Driver creates a distributed ESMF_Grid
- ESMF automatically decomposes the domain across processes
- Each process gets local bounds [lbnd(1):ubnd(1), lbnd(2):ubnd(2)]
- Processes synchronize using ESMF_VMBarrier before and after Run phases

**Example Configuration:**
```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  grid:
    nx: 360
    ny: 180
```

**Usage:**
```bash
mpirun -np 4 ./cece_nuopc_single_driver
```

### Coupled Mode Execution

When the cap is invoked by a NUOPC_Driver framework (coupled mode), the grid can come
from two sources, resolved in this order at `InitializeRealize`:

1. **Parent-provided grid (wins).** If the host associates an `ESMF_Grid` or `ESMF_Mesh`
   with the component before realization, the cap extracts its global coordinates across
   PETs and builds the simulation on that grid. Supported parent topologies:
   - 1-D `lon[nx]` x `lat[ny]` center coordinates -> rectilinear
   - 2-D (GRIDSPEC) coordinates flattened to `nx*ny` -> curvilinear
   - an unstructured `ESMF_Mesh` node list -> unstructured (`ny = 1`)
   Radian coordinate systems are converted to degrees and longitudes wrapped to
   `[-180, 180)` in the shared C++ code. An unsupported parent shape fails loudly
   with a named diagnostic — there is **no fallback to a uniform grid**.
2. **Config-built grid (fallback).** With no parent grid, the cap resolves the grid from
   the CECE YAML through the exact same code path as the C++ driver (named grid,
   `gridspec_file`, stream-inferred coordinates, or uniform `driver.grid` extents).

**Precedence:** when both a parent grid and a YAML grid are present, the parent wins and
a single warning names the ignored YAML grid. The two sources are never merged.

**Vertical layers:** `nz` is always taken from `driver.grid.nz` in the config — never
inferred from the flat 2-D parent grid, which carries no vertical dimension.

**Behavior:**
- If the `driver` section is absent, driver uses documented defaults
- If the `driver` section is present but incomplete, missing values use defaults
- No errors are raised for missing configuration in coupled mode

## Default Configuration

When no driver configuration is specified, the following defaults are used:

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  gridspec_file: null
  grid:
    nx: 4
    ny: 4
```

This configuration runs a 1-day simulation with 1-hour timesteps on a 4×4 grid.

## Validation Rules

The driver validates configuration parameters and exits with error if:

1. **Invalid ISO8601 format:** Start/end times must be in YYYY-MM-DDTHH:MM:SS format
2. **Start time >= end time:** Start time must be strictly before end time
3. **Non-positive timestep:** Timestep must be > 0 seconds
4. **Invalid grid dimensions:** nx and ny must be > 0
5. **Missing gridspec file:** If gridspec_file is specified, the file must exist and be a valid ESMF GRIDSPEC NetCDF file
6. **Invalid refresh interval:** Any `refresh_interval_seconds` or `stacking_refresh_interval_seconds` must be a positive integer multiple of `timestep_seconds`. Error messages name the offending component.

## Grid/Mesh Selection Logic

Grid resolution is performed by the shared C++ core (`GridSpec::from_yaml`), so both
drivers follow identical rules. In the standalone driver and in the cap's config-built
branch, the target grid is selected as:

1. **If `grid_name` is specified** (e.g. `F360`, `R360`):
   - Generate the mesh via AXIS and extract 1-D center coordinates -> rectilinear
   - `nx`/`ny` must match the named grid's expected dimensions (or be omitted)

2. **If `gridspec_file` is specified:**
   - Load coordinates from the GRIDSPEC/UGRID NetCDF file (CF-packed values unpacked,
     radians converted to degrees when the source is a cell-centered cubed-sphere name,
     longitudes wrapped)
   - Classify topology from the coordinate shapes: 1-D `lon[nx]`/`lat[ny]` ->
     rectilinear; 2-D flattened `nx*ny` -> curvilinear; `ny = 1` node arrays ->
     unstructured
   - If the file cannot be loaded, the run fails loudly (no silent substitution)

3. **If neither is specified:**
   - Generate a uniform grid from `driver.grid.nx`/`ny`/`nz` and the `lon_min`/`lon_max`/
     `lat_min`/`lat_max` extents
   - `ny = 1` yields a flattened 1-D node row (unstructured topology)

4. **In coupled mode (cap only):** a parent-provided ESMF Grid/Mesh takes precedence
   over all of the above (see Coupled Mode Execution); the YAML grid is used only when
   no parent grid is associated. This is the use case of a host model (NUOPC driver or
   mediator) that owns the discretization: CECE must emit on the host's grid so the
   exchanged fields are geographically consistent without regridding, and the YAML
   grid section then only supplies the vertical layer count.

**Validation:** `nx`, `ny`, and `nz` must all be positive, and the coordinate array
lengths must match the declared topology. A configured `nz` that contradicts the input
data's layer requirement is rejected at simulation creation.

## Large Grid Synchronization

For grids larger than 50,000 points, the driver performs grid-size-dependent synchronization:

| Grid Size | Synchronization Strategy |
|-----------|--------------------------|
| ≤ 50,000 points | Single VM barrier |
| 50,001 - 100,000 points | Enhanced: 2 VM barriers |
| 100,001 - 500,000 points | Extended: 3 VM barriers |
| > 500,000 points | Maximum: 4 VM barriers |

This ensures that large grids have sufficient time for all async operations to complete before resource cleanup.

## Example Configurations

### Minimal Configuration (Default)

```yaml
# Minimal configuration - uses all defaults
driver: {}
```

### Full Configuration

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  gridspec_file: null
  grid:
    nx: 360
    ny: 180
```

### GRIDSPEC File Configuration

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  gridspec_file: "/path/to/grid.nc"
```

### Large Grid Configuration

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 1800  # 30-minute timesteps
  grid:
    nx: 1440  # 0.25° resolution
    ny: 720
```

### Multi-Day Simulation

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-31T23:59:59"
  timestep_seconds: 3600
  grid:
    nx: 360
    ny: 180
```

## Logging and Diagnostics

The driver logs all configuration values at startup:

```
INFO: [Driver] Clock: 2020-01-01 -> 2020-01-02 dt=3600s
INFO: [Driver] Grid created: 360 x 180 (nx x ny)
INFO: [Driver] === Phase: Advertise+Init (IPDv01p1) ===
INFO: [Driver] === Phase: Realize+Bind (IPDv01p3) ===
INFO: [Driver] === Phase: Run Loop ===
INFO: [Driver] Run loop complete: 24 steps
```

For MPI execution, each process logs its local bounds:

```
INFO: [Driver] Process 0 local grid: [1:90, 1:180] of global [360x180]
INFO: [Driver] Process 1 local grid: [91:180, 1:180] of global [360x180]
INFO: [Driver] Process 2 local grid: [181:270, 1:180] of global [360x180]
INFO: [Driver] Process 3 local grid: [271:360, 1:180] of global [360x180]
```

## Error Handling

The driver provides clear error messages for configuration issues:

```
ERROR: [Driver] Invalid ISO8601 format: 2020-01-01 (expected YYYY-MM-DDTHH:MM:SS)
ERROR: [Driver] Start time must be before end time
ERROR: [Driver] Timestep must be positive
ERROR: [Driver] Grid dimensions must be positive
ERROR: [Driver] Mesh file not found: /path/to/mesh.nc
```

## Performance Considerations

- **Single-process execution:** Suitable for testing and debugging
- **MPI execution:** Recommended for large grids (>100k points)
- **Timestep selection:** Smaller timesteps increase computational cost
- **Grid resolution:** Higher resolution grids require more memory and computation

## Troubleshooting

### Clock Configuration Issues

**Problem:** "Failed to create clock"
**Solution:** Verify start_time < end_time and timestep_seconds > 0

### Grid Creation Issues

**Problem:** "Failed to create grid"
**Solution:** Verify grid.nx > 0 and grid.ny > 0

### GRIDSPEC File Issues

**Problem:** "Failed to load GRIDSPEC file"
  gridspec_file path does not exist or is not a valid GRIDSPEC NetCDF file

### MPI Synchronization Issues

**Problem:** "VM barrier failed"
**Solution:** Check MPI configuration and ensure all processes are healthy

## References

- ESMF User Guide: https://earthsystemmodeling.org/docs/release/latest/ESMF_usrdoc
- NUOPC Reference Manual: https://earthsystemmodeling.org/docs/release/latest/NUOPC_refdoc
- CECE Documentation: See docs/index.md
