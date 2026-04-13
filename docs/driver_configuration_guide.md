# CECE Driver Configuration Guide

## Overview

The CECE standalone NUOPC driver (`cece_nuopc_single_driver`) supports configurable execution modes for both single-process and MPI multi-process simulations. This guide documents all configuration options and usage patterns.

## Configuration File Format

Driver configuration is specified in the CECE YAML configuration file under the optional `driver` section:

```yaml
driver:
  start_time: "2020-01-01T00:00:00"      # ISO8601 format (optional, default: 2020-01-01T00:00:00)
  end_time: "2020-01-02T00:00:00"        # ISO8601 format (optional, default: 2020-01-02T00:00:00)
  timestep_seconds: 3600                 # Positive integer (optional, default: 3600)
  mesh_file: null                        # Path to ESMF mesh file (optional, default: null - generate grid)
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
```

### end_time

**Type:** String (ISO8601 format)
**Format:** `YYYY-MM-DDTHH:MM:SS`
**Default:** `2020-01-02T00:00:00`
**Description:** Simulation end time in ISO8601 format. Must be after start_time.

**Example:**
```yaml
driver:
  end_time: "2020-06-16T12:00:00"
```

### timestep_seconds

**Type:** Integer
**Range:** > 0
**Default:** `3600` (1 hour)
**Description:** Simulation timestep duration in seconds

**Example:**
```yaml
driver:
  timestep_seconds: 1800  # 30 minutes
```

### mesh_file

**Type:** String (file path) or null
**Default:** `null`
**Description:** Path to ESMF mesh file for spatial discretization. If null or absent, a Gaussian grid is generated based on grid.nx and grid.ny.

**Example:**
```yaml
driver:
  mesh_file: "/path/to/mesh.nc"
```

### grid.nx

**Type:** Integer
**Range:** > 0
**Default:** `4`
**Description:** Number of grid points in X direction (longitude). Only used if mesh_file is null.

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
**Description:** Number of grid points in Y direction (latitude). Only used if mesh_file is null.

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

When the driver is used in coupled mode (invoked by NUOPC_Driver framework):

- Clock is provided by the framework (driver skips clock creation)
- Grid is provided by the framework (driver skips grid creation)
- Driver configuration is ignored (graceful degradation)
- Driver operates normally without driver configuration section

**Behavior:**
- If `driver` section is absent, driver uses documented defaults
- If `driver` section is present but incomplete, missing values use defaults
- No errors are raised for missing configuration in coupled mode

## Default Configuration

When no driver configuration is specified, the following defaults are used:

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  mesh_file: null
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
5. **Missing mesh file:** If mesh_file is specified, the file must exist and be valid

## Grid/Mesh Selection Logic

The driver uses the following logic to select spatial discretization:

1. **If mesh_file is specified and valid:**
   - Read ESMF mesh from file
   - Use mesh for spatial discretization
   - Skip grid generation
   - Log mesh file source and dimensions

2. **If mesh_file is null or absent:**
   - Generate Gaussian grid based on grid.nx and grid.ny
   - Create mesh from grid with proper node/element connectivity
   - Log grid configuration to stdout

3. **In coupled mode:**
   - Skip grid/mesh creation
   - Use framework-provided grid/mesh

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
  mesh_file: null
  grid:
    nx: 360
    ny: 180
```

### Mesh File Configuration

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  mesh_file: "/path/to/mesh.nc"
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

### Mesh File Issues

**Problem:** "Failed to read mesh file"
**Solution:** Verify mesh_file path exists and is a valid ESMF mesh file

### MPI Synchronization Issues

**Problem:** "VM barrier failed"
**Solution:** Check MPI configuration and ensure all processes are healthy

## References

- ESMF User Guide: https://earthsystemmodeling.org/docs/release/latest/ESMF_usrdoc
- NUOPC Reference Manual: https://earthsystemmodeling.org/docs/release/latest/NUOPC_refdoc
- CECE Documentation: See docs/index.md
