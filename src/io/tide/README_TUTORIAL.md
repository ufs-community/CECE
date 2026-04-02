# TIDE Integration Tutorial: Hooking into a NUOPC Cap

This tutorial demonstrates how to integrate the TIDE (Temporal Interpolation & Data Extraction) library into a NUOPC Gridded Component cap. TIDE simplifies reading and interpolating earth system datasets using a clean YAML configuration.

## Step 1: Initialize TIDE in the Cap

In your NUOPC component's `InitializeRealize` phase (or similar initialization routine), call `tide_init` to load your stream configuration and set up the spatial mapping to your model mesh.

```fortran
use tide_mod, only: tide_type, tide_init
...
type(tide_type) :: tide_handle
integer :: rc

! tide_handle should be part of your component's internal state
call tide_init(tide_handle, &
               config_yaml="emissions_config.yaml", &
               model_mesh=model_mesh, & ! Your ESMF_Mesh
               clock=clock, &           ! Your ESMF_Clock
               rc=rc)
if (rc /= ESMF_SUCCESS) call handle_error()
```

## Step 2: Advance TIDE in the Model Run

In your cap's `ModelAdvance` phase, call `tide_advance` to perform temporal interpolation of the stream data to the current model time.

```fortran
use tide_mod, only: tide_advance
...
call tide_advance(tide_handle, clock, rc=rc)
if (rc /= ESMF_SUCCESS) call handle_error()
```

## Step 3: Retrieve and Use Data

After advancing, retrieve pointers to the interpolated data fields using `tide_get_ptr`. This data can then be used to populate your component's export state or internal arrays.

```fortran
use tide_mod, only: tide_get_ptr
...
real(r8), pointer :: so2_ptr(:,:)

call tide_get_ptr(tide_handle, "SO2_flux", so2_ptr, rc)
if (rc == ESMF_SUCCESS) then
    ! Use so2_ptr to fill ESMF_Fields or internal model variables
    ! Note: so2_ptr is (n_levels, n_local_elements)
end if
```

## Example YAML Configuration (`emissions_config.yaml`)

TIDE uses a clean YAML schema to define data streams.

```yaml
streams:
  - name: anthropogenic_emissions
    mesh_file: "input/emission_mesh.nc"
    tax_mode: "cycle"            # Options: cycle, extend, limit
    time_interp: "linear"        # Options: linear, lower, upper, nearest
    map_algo: "bilinear"         # Options: bilinear, nn, redist, consf, consd
    year_first: 2000
    year_last: 2020
    year_align: 2024
    input_files:
      - "input/so2_emissions_2000-2020.nc"
    field_maps:
      - { file_var: "so2", model_var: "SO2_flux" }
      - { file_var: "nox", model_var: "NOx_flux" }
```

## Key Considerations
- **Memory Management**: TIDE manages internal ESMF objects (Mesh, Fields, RouteHandles) and PIO descriptors.
- **Spatial Mapping**: TIDE automatically handles spatial interpolation (regridding) from the source `mesh_file` to the provided `model_mesh`.
- **Temporal Interpolation**: TIDE handles time-bounds searching and interpolation across multiple files as defined in the configuration.
