# CCPP Host Model Metadata Reference for CECE

This document lists all variables that CECE CCPP driver schemes require from the host model (imports) and produce as outputs (exports). Host model developers should use this reference to ensure their host-side metadata provides the correct fields for CECE integration.

## Common Variables (All Schemes)

These variables are required by every CECE CCPP driver scheme during initialization and/or execution.

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `horizontal_loop_extent` | `horizontal_loop_extent` | count | () | integer | — | in |
| `vertical_layer_dimension` | `vertical_layer_dimension` | count | () | integer | — | in |
| `cece_configuration_file_path` | `cece_configuration_file_path` | none | () | character | len=512 | in |
| `errmsg` | `ccpp_error_message` | none | () | character | len=512 | out |
| `errflg` | `ccpp_error_code` | 1 | () | integer | — | out |

## Import Variables (Host → CECE)

These are the meteorological and environmental input fields that the host model must provide.

### Stacking Engine (`cece_ccpp_stacking`)

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `current_hour` | `current_hour_of_day` | 1 | () | integer | — | in |
| `current_day_of_week` | `current_day_of_week` | 1 | () | integer | — | in |

### Sea Salt (`cece_ccpp_sea_salt`)

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `air_temperature` | `air_temperature` | K | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `surface_wind_speed` | `wind_speed_at_10m` | m s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `sea_surface_temperature` | `sea_surface_skin_temperature` | K | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |

### MEGAN Biogenics (`cece_ccpp_megan`)

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `air_temperature` | `air_temperature` | K | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `surface_pressure` | `surface_pressure` | Pa | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `soil_temperature` | `soil_temperature` | K | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `leaf_area_index` | `leaf_area_index` | 1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `downward_shortwave_radiation` | `downward_shortwave_radiation` | W m-2 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |

### Dust (`cece_ccpp_dust`)

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `surface_wind_speed` | `wind_speed_at_10m` | m s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `soil_moisture` | `soil_moisture` | m3 m-3 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |

### DMS (`cece_ccpp_dms`)

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `surface_wind_speed` | `wind_speed_at_10m` | m s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `sea_surface_temperature` | `sea_surface_skin_temperature` | K | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |

### Lightning (`cece_ccpp_lightning`)

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `convective_cloud_top_pressure` | `convective_cloud_top_pressure` | Pa | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `convective_cloud_bottom_pressure` | `convective_cloud_bottom_pressure` | Pa | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |

### Soil NOx (`cece_ccpp_soil_nox`)

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `soil_temperature` | `soil_temperature` | K | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |
| `soil_moisture` | `soil_moisture` | m3 m-3 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |

### Volcano (`cece_ccpp_volcano`)

No scheme-specific import variables. The volcano scheme uses internally managed emission inventories.

### Example Emission (`cece_ccpp_example`)

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent |
|---|---|---|---|---|---|---|
| `air_temperature` | `air_temperature` | K | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | in |

## Export Variables (CECE → Host)

These are the emission fields produced by CECE and returned to the host model.

| Local Name | standard_name | Units | Dimensions | Type | Kind | Intent | Scheme |
|---|---|---|---|---|---|---|---|
| `sea_salt_emission_flux_fine` | `sea_salt_emission_flux_fine_mode` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | sea_salt |
| `sea_salt_emission_flux_coarse` | `sea_salt_emission_flux_coarse_mode` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | sea_salt |
| `biogenic_isoprene_emission_flux` | `biogenic_isoprene_emission_flux` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | megan |
| `dust_emission_flux` | `dust_emission_flux` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | dust |
| `dms_emission_flux` | `dms_emission_flux` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | dms |
| `lightning_nox_emission_flux` | `lightning_nox_emission_flux` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | lightning |
| `soil_nox_emission_flux` | `soil_nox_emission_flux` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | soil_nox |
| `volcanic_so2_emission_flux` | `volcanic_so2_emission_flux` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | volcano |
| `example_emission_flux` | `example_emission_flux` | kg m-2 s-1 | (horizontal_loop_extent, vertical_layer_dimension) | real | kind_phys | out | example |

## CCPP Standard Name to CECE Internal Name Mapping

The CCPP driver schemes translate between CCPP `standard_name` values used by the host model and CECE's internal field names used by the C++ physics engine. This mapping is driven by the `met_mapping` section in the CECE YAML configuration file.

### Meteorological Input Mapping

| CCPP standard_name | CECE Internal Name | Description |
|---|---|---|
| `air_temperature` | `temperature` | Air temperature field |
| `wind_speed_at_10m` | `wind_speed` | Surface wind speed at 10m |
| `sea_surface_skin_temperature` | `sst` | Sea surface temperature |
| `surface_pressure` | `ps` | Surface pressure |
| `downward_shortwave_radiation` | `solar_radiation` | Downward shortwave radiation flux |
| `soil_temperature` | `soil_temperature` | Soil temperature |
| `leaf_area_index` | `lai` | Leaf area index |
| `soil_moisture` | `soil_moisture` | Volumetric soil moisture |
| `convective_cloud_top_pressure` | `cloud_top_pressure` | Convective cloud top pressure |
| `convective_cloud_bottom_pressure` | `cloud_bottom_pressure` | Convective cloud bottom pressure |

### Export Field Mapping

| CCPP standard_name | CECE Internal Name | Description |
|---|---|---|
| `sea_salt_emission_flux_fine_mode` | `EmissSS_SALA` | Fine mode sea salt emissions |
| `sea_salt_emission_flux_coarse_mode` | `EmissSS_SALC` | Coarse mode sea salt emissions |
| `biogenic_isoprene_emission_flux` | `EmissMEGAN_ISOP` | Biogenic isoprene emissions |
| `dust_emission_flux` | `EmissDust` | Dust emissions |
| `dms_emission_flux` | `EmissDMS` | DMS emissions |
| `lightning_nox_emission_flux` | `EmissLNOx` | Lightning NOx emissions |
| `soil_nox_emission_flux` | `EmissSoilNOx` | Soil NOx emissions |
| `volcanic_so2_emission_flux` | `EmissVolcSO2` | Volcanic SO2 emissions |
| `example_emission_flux` | `EmissExample` | Example emission flux |

## Dimension Mapping

CCPP uses a 2D blocked layout `(horizontal_loop_extent, vertical_layer_dimension)` while CECE internally uses a 3D grid `(nx, ny, nz)`. The driver schemes handle this conversion:

| CCPP Dimension | CECE Dimension | Notes |
|---|---|---|
| `horizontal_loop_extent` | `nx` (with `ny=1`) | Host model provides blocked columns |
| `vertical_layer_dimension` | `nz` | Direct mapping |

For gridded host models, reshape `(nx, ny)` into `horizontal_loop_extent = nx * ny` before calling the CCPP scheme. The CCPP blocked layout element `(i, k)` maps to DualView3D element `(i, 1, k)` when `ny=1`.
