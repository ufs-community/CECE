# TIDE (Temporal Interpolation & Data Extraction) Design

## YAML Configuration Schema
The TIDE library replaces XML/ESMF/CIME configurations with a clean YAML format.

```yaml
streams:
  - name: stream1
    mesh_file: "path/to/mesh.nc"
    lev_dimname: "lev"           # Optional, defaults to "null"
    tax_mode: "cycle"            # cycle, extend, limit
    time_interp: "linear"        # linear, lower, upper, nearest, coszen
    map_algo: "bilinear"         # bilinear, redist, nn, consf, consd, none
    read_mode: "single"          # single, full_file
    dt_limit: 1.5
    year_first: 1850
    year_last: 2000
    year_align: 1850
    offset: 0
    input_files:
      - "file1.nc"
      - "file2.nc"
    field_maps:
      - { file_var: "SO2", model_var: "so2_flux" }
      - { file_var: "NOx", model_var: "nox_flux" }
```

## Fortran API
The TIDE API is designed to be lean and easily integrated into NUOPC caps.

```fortran
use tide_mod, only: tide_type, tide_init, tide_advance, tide_get_ptr

type(tide_type) :: tide

! Initialize TIDE with a YAML config, model mesh, and clock
call tide_init(tide, config_yaml="tide_config.yaml", model_mesh=mesh, clock=clock, rc=rc)

! Advance TIDE to the current model time (handles interpolation)
call tide_advance(tide, clock, rc=rc)

! Retrieve a pointer to the interpolated data for a specific field
real(r8), pointer :: field_ptr(:,:)
call tide_get_ptr(tide, field_name="so2_flux", ptr=field_ptr, rc=rc)
```

## Implementation Strategy
1. **YAML Parsing**: Use `yaml-cpp` via a C interface to Fortran.
2. **Core Logic**: Refactor `shr_strdata` and `shr_stream` into `tide_mod`.
3. **Library Focus**: Remove all non-essential data components and XML dependencies.
