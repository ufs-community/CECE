# Scheme interface audit

Baseline 25f7209. This table is a source-access inventory, not a claim that every scheme is column-ready. Computed field names and direct legacy accesses require separate audit. New adapters initially support native_example, MEGAN3 and BDSNP; other schemes must fail explicitly until their contracts are reviewed.

| Scheme | Implementation | Literal import/input accesses | Literal export accesses |
|---|---|---|---|
| bdsnp | `src/core/physics/cece_bdsnp.cpp` | deposited_nitrogen, leaf_area_index, soil_fertilizer, soil_moisture, soil_temperature, soilnox_arid_fraction, soilnox_canopy_nox, soilnox_land_fractions, soilnox_nonarid_fraction, soilnox_pulse_factor, solar_zenith_cosine, wind_speed_squared | soil_nox_emissions, soil_nox_fertilizer_emissions |
| bdsnp_fortran | `src/core/physics/cece_bdsnp_fortran_bridge.cpp` |  |  |
| dms | `src/core/physics/cece_dms.cpp` | seawater_conc, tskin, wind_speed | dms_emissions |
| dms_fortran | `src/core/physics/cece_dms_fortran_bridge.cpp` |  |  |
| dust | `src/core/physics/cece_dust.cpp` | erodibility, soil_moisture, wind_speed | dust_emissions |
| fengsha | `src/core/physics/cece_fengsha.cpp` | air_density, clay_fraction, drag_partition, erodibility, friction_velocity, lake_fraction, land_mask, sand_fraction, silt_fraction, snow_fraction, soil_moisture, threshold_velocity | fengsha_dust_emissions |
| fortran_bridge_example | `src/core/physics/cece_fortran_bridge.cpp` |  |  |
| ginoux | `src/core/physics/cece_ginoux.cpp` | dust_source, lake_fraction, land_mask, particle_radius, surface_soil_wetness, u10m, v10m | ginoux_dust_emissions |
| k14 | `src/core/physics/cece_k14.cpp` | air_density, clay_fraction, dust_source, friction_velocity, height, land_fraction, roughness_length, sand_fraction, silt_fraction, snow_fraction, soil_temperature, soil_texture, u_wind, v_wind, vegetation_fraction, vegetation_type, volumetric_soil_moisture | k14_dust_emissions |
| lightning | `src/core/physics/cece_lightning.cpp` | cloud_top_height, land_mask | lightning_nox_emissions |
| lightning_fortran | `src/core/physics/cece_lightning_fortran_bridge.cpp` |  |  |
| megan | `src/core/physics/cece_megan.cpp` | leaf_area_index, leaf_area_index_prev, par_diffuse, par_direct, soil_moisture_root, solar_cosine, temperature |  |
| megan3 | `src/core/physics/cece_megan3.cpp` | leaf_area_index, leaf_area_index_prev, par_diffuse, par_direct, soil_moisture_root, solar_cosine, temperature, wind_speed | soil_nox_emissions |
| native_example | `src/core/physics/cece_native_example.cpp` | base_anthropogenic_nox, multiplier_input | nox |
| sea_salt | `src/core/physics/cece_sea_salt.cpp` | tskin, wind_speed | coarse_input, secondary_input |
| sea_salt_fortran | `src/core/physics/cece_sea_salt_fortran_bridge.cpp` |  |  |
| soil_nox | `src/core/physics/cece_soil_nox.cpp` | soil_moisture, temperature | soil_nox_emissions |
| soil_nox_fortran | `src/core/physics/cece_soil_nox_fortran_bridge.cpp` |  |  |
| volcano | `src/core/physics/cece_volcano.cpp` | layer_thickness, surface_altitude | volcanic_so2 |
| volcano_fortran | `src/core/physics/cece_volcano_fortran_bridge.cpp` |  |  |

## Native example

Profile input base_anthropogenic_nox and optional multiplier_input; profile output nox. Multiplier defaults to 2.0 and is dimensionless. The example has no defined chemical mass unit: preserve caller units. Kernel adds to output; a new runtime step zeroes its per-step output first. Cells are independent.

## MEGAN3

Required surface inputs: temperature [K], leaf_area_index [1], par_direct/par_diffuse [W m-2], solar_cosine [1]. Optional leaf_area_index_prev defaults to current LAI; soil_moisture_root and wind_speed enable corresponding stress terms. Per-class AEF_<CLASS> fields use kmol class m-2 s-1 and fall back to configured constants. Soil NO is read from export soil_nox_emissions [kg NO m-2 s-1], converted internally to kmol NO. Existing default behavior uses zero if absent. New composition must explicitly choose an external field, a preceding scheme, or zero.

Outputs: MEGAN_-prefixed species from data/speciation/spc_cb6.yaml and map_cb6.yaml by default. Audit exact species units in SpeciationEngine; a CCPP species array must carry a versioned index manifest. The runtime kernel is cell-local and reads level zero.

Important correction to planning assumptions: MeganHistory is configured scalar effective history; its header explicitly says no history evolution is performed. Do not invent rolling-history science or claim hidden prognostic state. Preserve configured temperature/PAR history, day_of_year, days_between_lai and leaf-age policy in restart configuration. Actual history evolution is a separate scientific change.

## BDSNP

Two option-dependent modes: yl95 and bdsnp. The yl95 path requires soil_temperature [K] and soil_moisture; the bdsnp path additionally consumes soil land fractions/category fields, arid/nonarid fractions, canopy NOx, wind-speed squared, solar zenith cosine, fertilizer/deposition and pulse factors. Category extent must be audited from the selected mode and cannot be labeled atmospheric levels. Outputs include soil_nox_emissions and optional fertilizer component. See existing tests/test_bdsnp.cpp and tests/test_bdsnp_runtime.cpp for numerical reference fixtures. Freeze the full bdsnp contract before enabling that mode in new adapters.
