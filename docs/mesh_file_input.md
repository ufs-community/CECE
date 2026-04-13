# Mesh File Input Support

## Overview

The CECE driver supports reading pre-existing ESMF mesh files for spatial discretization. This feature allows users to use complex geometries or pre-computed meshes instead of generating simple structured grids.

## Configuration

Mesh file input is configured in the CECE configuration file under the `driver` section:

```yaml
driver:
  # Simulation timing
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600

  # Use pre-existing ESMF mesh file
  mesh_file: "/path/to/mesh.nc"

  # Grid dimensions (ignored when mesh_file is specified)
  grid:
    nx: 360
    ny: 180
```

## Grid/Mesh Selection Logic

The driver uses the following logic to determine whether to use a mesh file or generate a grid:

1. **If `mesh_file` is specified and non-empty**: Read the mesh from the file
2. **If `mesh_file` is empty or absent**: Generate a Gaussian grid based on `grid.nx` and `grid.ny`

## Mesh File Requirements

ESMF mesh files must:

- Be in ESMF mesh format (NetCDF-based)
- Contain proper node/element connectivity
- Have at least one node and one element
- Be readable by `ESMF_MeshCreate(filename=...)`

## Validation

When a mesh file is specified, the driver performs the following validation:

1. **File existence check**: Verifies the file exists at the specified path
2. **Format validation**: Attempts to read the mesh using ESMF_MeshCreate
3. **Connectivity validation**: Checks that the mesh has nodes and elements
4. **Dimension logging**: Logs the mesh dimensions (nodes, elements, spatial dimension)

If any validation step fails, the driver logs an error and exits with a non-zero status.

## Error Handling

### File Not Found

```
ERROR: [simple_driver] Failed to read mesh from file: /path/to/mesh.nc (rc=67)
```

**Solution**: Verify the mesh file path is correct and the file exists.

### Invalid Mesh Format

```
ERROR: [simple_driver] Failed to read mesh from file: /path/to/mesh.nc (rc=...)
```

**Solution**: Verify the file is in ESMF mesh format. Use ESMF utilities to validate the mesh file.

### Invalid Connectivity

```
ERROR: [validate_mesh] Mesh has no nodes (nodeCount=0)
ERROR: [validate_mesh] Mesh has no elements (elementCount=0)
```

**Solution**: Verify the mesh file contains valid node and element data.

## Logging

When using a mesh file, the driver logs:

```
INFO: [simple_driver] Mesh file: /path/to/mesh.nc
INFO: [simple_driver] Reading mesh from file: /path/to/mesh.nc
INFO: [validate_mesh] Mesh validation passed
INFO: [log_mesh_info] Mesh nodes: 12345
INFO: [log_mesh_info] Mesh elements: 10000
INFO: [log_mesh_info] Mesh spatial dimension: 2
```

## Example Configurations

### Using a Mesh File

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  mesh_file: "/data/meshes/global_0.25deg.nc"
  grid:
    nx: 1440  # Ignored when mesh_file is specified
    ny: 720   # Ignored when mesh_file is specified
```

### Generating a Grid (Default)

```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-02T00:00:00"
  timestep_seconds: 3600
  mesh_file: ""  # Empty or omit to generate grid
  grid:
    nx: 360
    ny: 180
```

## Implementation Details

### Fortran Driver (standalone_nuopc/simple_driver.F90)

The driver implements mesh file input in the following steps:

1. **Read configuration**: Parse `mesh_file` from the CECE config file
2. **Determine mode**: Check if `mesh_file` is non-empty
3. **Read mesh**: If mesh file specified, call `ESMF_MeshCreate(filename=...)`
4. **Validate mesh**: Check node/element counts using `ESMF_MeshGet`
5. **Log dimensions**: Log mesh information for diagnostics
6. **Create dummy grid**: Create a minimal 2x2 grid for compatibility

### C++ Configuration (src/cece_core_driver_config.cpp)

The C++ configuration reader extracts the `mesh_file` parameter:

```cpp
std::string mesh_str = driver_cfg.mesh_file;
if (mesh_str.length() >= static_cast<size_t>(mesh_file_len)) {
    std::cerr << "WARNING: mesh_file buffer too small, truncating\n";
    std::strncpy(mesh_file, mesh_str.c_str(), mesh_file_len - 1);
    mesh_file[mesh_file_len - 1] = '\0';
} else {
    std::strcpy(mesh_file, mesh_str.c_str());
}
```

## Requirements Satisfied

This implementation satisfies the following requirements:

- **Requirement 14.1**: Read ESMF mesh from file using `ESMF_MeshCreate(filename=...)`
- **Requirement 14.2**: Validate mesh has proper node/element connectivity
- **Requirement 14.3**: Generate Gaussian grid when mesh file is not specified
- **Requirement 14.4**: Use grid dimensions from config when generating grid
- **Requirement 14.6**: Log error and exit with non-zero status for invalid mesh files

## Testing

Unit tests are provided in `tests/test_mesh_file_input.cpp`:

- Configuration reading with mesh file
- Configuration reading without mesh file
- Grid/mesh selection logic
- Mesh file existence validation
- Mesh connectivity validation
- Logging and diagnostics

Run tests with:

```bash
./setup.sh -c "cd build && ./test_mesh_file_input"
```

## Future Enhancements

Potential future enhancements include:

1. Support for multiple mesh file formats (SCRIP, UGRID, etc.)
2. Automatic mesh generation from grid specifications
3. Mesh refinement and coarsening
4. Mesh partitioning for MPI decomposition
5. Mesh quality metrics and diagnostics
