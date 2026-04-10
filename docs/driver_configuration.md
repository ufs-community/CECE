# CECE Standalone NUOPC Driver Configuration Guide

## Overview

The CECE standalone NUOPC driver (`cece_nuopc_single_driver`) executes the CECE emissions component for a configurable number of timesteps with proper clock management, grid/mesh creation, and synchronization. The driver is designed for both standalone testing and future coupling readiness.

## Configuration Methods

The driver supports two configuration approaches:

### 1. YAML Configuration File (Recommended)

The driver reads an optional `driver` section from the CECE YAML configuration file. This is the recommended approach for reproducible simulations.

**Default config file**: `cece_config.yaml`

**Command-line override**: `--config /path/to/config.yaml`

### 2. Command-Line Arguments (Legacy)

For backward compatibility, the driver also supports command-line arguments. These override YAML configuration values.

## YAML Configuration Reference

### Minimal Configuration

If the `driver` section is omitted, all default values are used:

```yaml
species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
```

### Full Configuration

```yaml
driver:
  # Simulation start time (ISO8601 format: YYYY-MM-DDTHH:MM:SS)
  # Default: "2020-01-01T00:00:00"
  # Requirements: 1.1, 1.3
  start_time: "2020-06-01T00:00:00"

  # Simulation end time (ISO8601 format: YYYY-MM-DDTHH:MM:SS)
  # Default: "2020-01-02T00:00:00"
  # Requirements: 2.1, 2.3
  end_time: "2020-06-02T00:00:00"

  # Timestep duration in seconds (must be positive)
  # Default: 3600 (1 hour)
  # Requirements: 3.1, 3.3
  timestep_seconds: 1800

  # Path to ESMF mesh file (optional)
  # If specified, the driver reads the mesh from this file and skips grid generation
  # If null or absent, the driver generates a Gaussian grid based on grid.nx and grid.ny
  # Default: null (generate grid)
  # Requirements: 14.1, 14.2
  mesh_file: null

  # Grid configuration for generated Gaussian grid
  # Only used if mesh_file is null or absent
  # Requirements: 14.3, 14.4
  grid:
    # Grid points in X direction (must be positive)
    # Default: 4
    nx: 4

    # Grid points in Y direction (must be positive)
    # Default: 4
    ny: 4
```

## Configuration Parameters

### start_time

**Type**: String (ISO8601 format)

**Format**: `YYYY-MM-DDTHH:MM:SS`

**Default**: `2020-01-01T00:00:00`

**Description**: The simulation start time. Must be before `end_time`.

**Examples**:
- `2020-01-01T00:00:00` - January 1, 2020 at midnight UTC
- `2020-06-15T14:30:45` - June 15, 2020 at 2:30:45 PM UTC

**Requirements**: 1.1, 1.3, 1.4

### end_time

**Type**: String (ISO8601 format)

**Format**: `YYYY-MM-DDTHH:MM:SS`

**Default**: `2020-01-02T00:00:00`

**Description**: The simulation end time. Must be after `start_time`.

**Examples**:
- `2020-01-02T00:00:00` - January 2, 2020 at midnight UTC
- `2020-06-16T00:00:00` - June 16, 2020 at midnight UTC

**Requirements**: 2.1, 2.3, 2.4

### timestep_seconds

**Type**: Integer

**Default**: `3600`

**Description**: The duration of each timestep in seconds. Must be positive. The driver will execute `(end_time - start_time) / timestep_seconds` timesteps.

**Examples**:
- `3600` - 1 hour timesteps
- `1800` - 30 minute timesteps
- `86400` - 1 day timesteps

**Validation**:
- Must be > 0
- A warning is logged if `(end_time - start_time) % timestep_seconds != 0`

**Requirements**: 3.1, 3.2, 3.3

### mesh_file

**Type**: String (file path) or null

**Default**: `null`

**Description**: Path to an ESMF mesh file for spatial discretization. If specified, the driver reads the mesh from this file and skips grid generation. If null or absent, the driver generates a Gaussian grid based on `grid.nx` and `grid.ny`.

**Examples**:
- `"/path/to/mesh.nc"` - Use pre-existing mesh
- `null` - Generate Gaussian grid (default)

**Validation**:
- If specified, the file must exist and be a valid ESMF mesh
- If file doesn't exist, the driver logs an error and exits with status 1

**Requirements**: 14.1, 14.2, 14.6

### grid.nx

**Type**: Integer

**Default**: `4`

**Description**: Number of grid points in the X direction for generated Gaussian grids. Only used if `mesh_file` is null or absent. Must be positive.

**Examples**:
- `4` - 4x4 grid (16 points)
- `360` - 360x180 grid (64,800 points)

**Validation**: Must be > 0

**Requirements**: 14.3, 14.4, 14.7

### grid.ny

**Type**: Integer

**Default**: `4`

**Description**: Number of grid points in the Y direction for generated Gaussian grids. Only used if `mesh_file` is null or absent. Must be positive.

**Examples**:
- `4` - 4x4 grid (16 points)
- `180` - 360x180 grid (64,800 points)

**Validation**: Must be > 0

**Requirements**: 14.3, 14.4, 14.7

## Grid/Mesh Selection Logic

The driver uses the following logic to select spatial discretization:

1. **If `mesh_file` is specified and valid**:
   - Read ESMF mesh from file
   - Use mesh for spatial discretization
   - Skip grid generation
   - Log mesh file source and dimensions

2. **If `mesh_file` is null or absent**:
   - Generate Gaussian grid based on `grid.nx` and `grid.ny`
   - Create mesh from grid with proper node/element connectivity
   - Use generated mesh for spatial discretization
   - Log grid configuration

**Requirements**: 14.1, 14.3, 14.4, 15.1, 15.4

## Execution Modes

### Standalone Mode

When the driver is executed directly (not invoked by a NUOPC_Driver framework):

- Reads timing and grid/mesh parameters from CECE config file
- Creates ESMF clock with parsed parameters
- Creates grid/mesh based on configuration
- Executes CECE component through all NUOPC phases
- Manually manages clock advancement (if needed)

**Requirements**: 15.1, 15.3

### Coupled Mode

When the driver is invoked by a NUOPC_Driver framework:

- Clock is provided by framework (driver skips clock creation)
- Grid is provided by framework (driver skips grid creation)
- Driver configuration is ignored
- Driver operates normally without driver configuration section

**Requirements**: 1.5, 2.5, 3.5, 14.9, 15.2

## Default Values

When driver configuration is missing from the config file, the following defaults are used:

| Parameter | Default Value |
|-----------|---------------|
| `start_time` | `2020-01-01T00:00:00` |
| `end_time` | `2020-01-02T00:00:00` |
| `timestep_seconds` | `3600` |
| `mesh_file` | `null` (generate grid) |
| `grid.nx` | `4` |
| `grid.ny` | `4` |

**Requirements**: 1.2, 2.2, 3.2, 14.5, 15.3, 20

## Validation Rules

The driver validates configuration parameters and exits with status 1 if validation fails:

### ISO8601 Format Validation

- Format must be exactly `YYYY-MM-DDTHH:MM:SS`
- Year must be 4 digits
- Month must be 2 digits (01-12)
- Day must be 2 digits (01-31)
- Hour must be 2 digits (00-23)
- Minute must be 2 digits (00-59)
- Second must be 2 digits (00-59)

**Error message**: `ERROR: [Driver] Invalid ISO8601 format: {value}`

**Requirements**: 1.3, 2.3

### Time Ordering Validation

- `start_time` must be strictly before `end_time`

**Error message**: `ERROR: [Driver] Start time must be before end time`

**Requirements**: 1.4, 2.4

### Timestep Validation

- `timestep_seconds` must be > 0

**Error message**: `ERROR: [Driver] Timestep must be positive`

**Requirements**: 3.3

### Grid Dimension Validation

- `grid.nx` must be > 0
- `grid.ny` must be > 0

**Error message**: `ERROR: [Driver] Grid dimensions must be positive`

**Requirements**: 14.7

### Mesh File Validation

- If `mesh_file` is specified, the file must exist
- The file must be a valid ESMF mesh

**Error message**: `ERROR: [Driver] Mesh file not found: {path}`

**Requirements**: 14.6

## Logging and Diagnostics

The driver logs all configuration values during initialization:

```
INFO: [Driver] Config file:   cece_config.yaml
INFO: [Driver] Start time:    2020-01-01T00:00:00
INFO: [Driver] End time:      2020-01-02T00:00:00
INFO: [Driver] Time step (s): 3600
INFO: [Driver] Grid size:     4 x 4
INFO: [Driver] Clock: 2020-01-01 -> 2020-01-02 dt=3600s
```

For large grids (>50k points), the driver logs the synchronization level:

```
INFO: [Driver] Large grid (64800 points) - enhanced synchronization...
```

**Requirements**: 4.3, 14.8, 19.3

## Examples

### Example 1: Minimal Configuration (All Defaults)

```yaml
species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
```

**Result**:
- Simulation from 2020-01-01 to 2020-01-02 (24 hours)
- 3600-second (1-hour) timesteps = 24 timesteps
- 4x4 Gaussian grid (16 points)

### Example 2: Custom Timing and Grid

```yaml
driver:
  start_time: "2020-06-01T00:00:00"
  end_time: "2020-06-02T00:00:00"
  timestep_seconds: 1800
  grid:
    nx: 8
    ny: 8

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
```

**Result**:
- Simulation from 2020-06-01 to 2020-06-02 (24 hours)
- 1800-second (30-minute) timesteps = 48 timesteps
- 8x8 Gaussian grid (64 points)

### Example 3: Using Mesh File

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  mesh_file: "/data/mesh.nc"

species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0

physics_schemes:
  - name: NativeExample
    language: cpp
```

**Result**:
- Simulation from 2020-01-01 to 2020-01-02 (24 hours)
- 3600-second (1-hour) timesteps = 24 timesteps
- Mesh read from `/data/mesh.nc`

## Large Grid Synchronization

For grids larger than 50,000 points, the driver applies grid-size-dependent synchronization during finalization to prevent race conditions:

| Grid Size | Synchronization Strategy |
|-----------|--------------------------|
| ≤ 50,000 points | Single VM barrier |
| 50,001 - 100,000 points | Enhanced: 2 VM barriers |
| 100,001 - 500,000 points | Extended: 3 VM barriers |
| > 500,000 points | Maximum: 4 VM barriers |

**Requirements**: 12.1, 12.2, 12.3, 12.4, 12.5

## Error Handling

The driver implements comprehensive error handling:

### Fatal Errors (Exit with status 1)

- Invalid ISO8601 format
- Start time >= end time
- Non-positive timestep
- Non-positive grid dimensions
- Missing mesh file
- Invalid mesh file
- ESMF operation failures
- CECE component failures

### Non-Fatal Errors (Log warning, continue)

- VM barrier failures during cleanup
- Resource destruction failures

**Requirements**: 18.1, 18.2, 18.3, 18.4

## Requirements Mapping

This documentation covers the following requirements:

- **Requirement 1**: Configurable start time via CECE config file
- **Requirement 2**: Configurable end time via CECE config file
- **Requirement 3**: Configurable timestep via CECE config file
- **Requirement 4**: ESMF clock creation with proper configuration
- **Requirement 12**: Large grid synchronization (>50k points)
- **Requirement 14**: Grid and mesh configuration via CECE config file
- **Requirement 15**: Optional driver configuration in coupled mode
- **Requirement 18**: Error handling and logging
- **Requirement 19**: Driver configuration documentation
- **Requirement 20**: Idempotent clock advancement

## See Also

- [CECE Configuration Guide](configuration.md)
- [CECE Developer Guide](developer_guide.md)
- [NUOPC Reference Manual](https://earthsystemmodeling.org/docs/release/latest/NUOPC_refdoc)
