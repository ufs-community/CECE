# Configuration Reference

CECE is configured using a YAML file, typically named `cece_config.yaml`. This file defines the species to be processed, the source of data, and the active physics schemes.

## Top-Level Structure

```yaml
driver:
  # ... driver timing and execution configuration ...
  grid:
    # ... computational grid specification ...

meteorology:
  # ... meteorological field name mappings ...

scale_factors:
  # ... scale factor name mappings ...

masks:
  # ... mask name mappings ...

temporal_profiles:
  # ... periodic scaling factors (diurnal, weekly, etc.) ...

species:
  # ... species definitions ...

physics_schemes:
  # ... list of active physics schemes ...

diagnostics:
  # ... diagnostic settings and output configuration ...

cece_data:
  # ... data stream settings ...

output:
  # ... NetCDF output configuration ...
```

---

## `driver`

The `driver` section configures the execution timing and control parameters for CECE simulations.

| Key | Type | Description |
| --- | --- | --- |
| `start_time` | String | Simulation start time in ISO 8601 format (e.g., "2020-01-01T00:00:00") |
| `end_time` | String | Simulation end time in ISO 8601 format |
| `timestep_seconds` | Integer | Base time step duration in seconds. All component refresh intervals must be integer multiples of this value. |
| `stacking_refresh_interval_seconds` | Integer | (Optional) Stacking engine execution interval in seconds. Must be a positive multiple of `timestep_seconds`. Default: `0` (use `timestep_seconds`). |
| `amio_worker_threads` | Integer | (Optional) Number of AMIO background I/O worker threads. Must be ≥ 1 when explicitly set; nonpositive values are rejected. Default: `1` when omitted. Used as the fallback when `output.amio_worker_threads` is omitted. |
| `amio_staging_buffer_count` | Integer | (Optional) Number of AMIO staging buffers for offline input reads. Must be ≥ 1 when explicitly set; nonpositive values are rejected. Default: `8` when omitted. Increase to 16 for large global files if AMIO reports staging backpressure. |

**Example:**
```yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-01T06:00:00"
  timestep_seconds: 300                      # 5-minute base timestep
  stacking_refresh_interval_seconds: 3600    # Stacking runs hourly
```

---

## Clock Refresh Intervals

CECE supports independent refresh intervals for each component (physics schemes, data streams, and the stacking engine). This allows fast-responding schemes like biogenics to run every few minutes while slower-changing data streams ingest hourly, reducing unnecessary computation.

### How It Works

- The `timestep_seconds` in the `driver` section defines the base clock cadence.
- Each component can declare a `refresh_interval_seconds` that must be a positive integer multiple of `timestep_seconds`.
- Components without a `refresh_interval_seconds` (or with value `0`) default to the base timestep — they run every step, preserving backward compatibility.
- On the first timestep, all components execute regardless of their interval (first-step guarantee).
- The stacking engine always executes after all other due components in a given step.

### Configuration

Add `refresh_interval_seconds` to individual physics schemes or data streams, and `stacking_refresh_interval_seconds` to the driver section:

```yaml
driver:
  timestep_seconds: 300                      # 5-minute base
  stacking_refresh_interval_seconds: 3600    # Stacking runs hourly

physics_schemes:
  - name: "megan"
    language: "cpp"
    refresh_interval_seconds: 300            # Every base step (5 min)
    options: { ... }

  - name: "sea_salt"
    language: "cpp"
    refresh_interval_seconds: 1800           # Every 30 min
    options: { ... }

cece_data:
  streams:
    - name: "ANTHROPOGENIC"
      file: "/data/CEDS_2020.nc"
      refresh_interval_seconds: 3600         # Ingest hourly
```

### Validation

At startup, the clock validates all intervals. Errors are raised if:

- An interval is not a positive integer
- An interval is not an integer multiple of `timestep_seconds`

Error messages name the offending component for easy debugging.

### Backward Compatibility

Existing configurations without any `refresh_interval_seconds` fields continue to work unchanged — all components run every timestep, identical to previous behavior.

---

## `grid`

The `grid` section defines the computational domain and resolution. It must be nested under `driver:`.

| Key | Type | Description |
| --- | --- | --- |
| `grid_name` | String | (Optional) Named grid identifier (e.g., `"F360"`, `"R180"`). When set, `nx` and `ny` are auto-computed from the AXIS NamedGridRegistry. Supported families: `F` (regular Gaussian) and `R` (regular lat-lon). The number indicates resolution (e.g., `F360` → 1440×720). |
| `nx` | Integer | Number of grid points in longitude (x-direction). Auto-computed if `grid_name` is set. |
| `ny` | Integer | Number of grid points in latitude (y-direction). Auto-computed if `grid_name` is set. |
| `nz` | Integer | (Optional) Number of vertical levels. Default: `1`. |
| `lon_min` | Float | (Optional) Western boundary longitude [degrees]. Default: `-180.0`. |
| `lon_max` | Float | (Optional) Eastern boundary longitude [degrees]. Default: `180.0`. |
| `lat_min` | Float | (Optional) Southern boundary latitude [degrees]. Default: `-90.0`. |
| `lat_max` | Float | (Optional) Northern boundary latitude [degrees]. Default: `90.0`. |

**Example (explicit dimensions):**
```yaml
driver:
  grid:
    nx: 144           # 2.5° longitude resolution
    ny: 91            # 2° latitude resolution
    nz: 72            # 72 vertical levels
    lon_min: -180.0
    lon_max: 177.5
    lat_min: -90.0
    lat_max: 90.0
```

**Example (named grid):**
```yaml
driver:
  grid:
    grid_name: "F360"   # Regular Gaussian 1440×720
    nz: 72
```

---

## `meteorology`

Maps CECE internal field names to external data source field names for meteorological inputs.

**Example:**
```yaml
meteorology:
  temperature: air_temperature      # Map internal "temperature" to ESMF field "air_temperature"
  wind_speed: surface_wind_speed
  pressure: surface_pressure
  humidity: specific_humidity
```

---

## `scale_factors`

Maps CECE internal scale factor names to external field names for dynamic scaling.

**Example:**
```yaml
scale_factors:
  hourly_factor: HOURLY_SCALFACT
  monthly_factor: MONTHLY_SCALFACT
  emission_factor: EMISSION_SCALE
```

---

## `masks`

Maps CECE internal mask names to external field names for geographical masking.

**Example:**
```yaml
masks:
  land_mask: LAND_FRACTION
  urban_mask: URBAN_FRACTION
  water_mask: WATER_FRACTION
  vegetation_mask: VEGETATION_FRACTION
```

---

## `temporal_profiles`

Defines time-varying scale factors for diurnal, weekly, and seasonal cycles.

### Profile Types

- **Diurnal**: 24 values (hourly scale factors for 0-23 hours)
- **Weekly**: 7 values (daily scale factors for Sunday-Saturday, index 0=Sunday)
- **Seasonal**: 12 values (monthly scale factors for January-December)

**Example:**
```yaml
temporal_profiles:
  traffic_diurnal: [0.5, 0.3, 0.2, 0.3, 0.6, 1.2, 1.8, 1.5, 1.2, 1.0, 1.1, 1.2,
                    1.3, 1.2, 1.3, 1.5, 1.8, 2.0, 1.8, 1.5, 1.2, 1.0, 0.8, 0.6]
  weekday_pattern: [0.8, 1.2, 1.2, 1.2, 1.2, 1.0, 0.7]  # Sun-Sat
  seasonal_biomass: [0.5, 0.3, 0.8, 1.5, 2.0, 1.8, 1.2, 1.5, 2.2, 1.8, 0.8, 0.6]
```

---

## `species`

The `species` block defines the emission targets and the layers that contribute to them. This is the core configuration section that determines how different emission sources are combined.

### Layer Properties

| Key | Type | Description |
| --- | --- | --- |
| `field` | String | Name of the input field from a configured data stream or the Import State. |
| `operation` | String | How to combine with existing data: `add` or `replace` |
| `scale` | Float | Base scaling factor (Default: `1.0`) |
| `category` | String | Logical grouping for layers (e.g., "anthropogenic", "biogenic") |
| `hierarchy` | Integer | Priority within category. Higher values take precedence |
| `mask` | String | (Optional) Name of geographical mask field |
| `scale_fields` | List | (Optional) List of dynamic scaling field names |
| `diurnal_cycle` | String | (Optional) Reference to temporal profile for diurnal scaling |
| `weekly_cycle` | String | (Optional) Reference to temporal profile for weekly scaling |
| `seasonal_cycle` | String | (Optional) Reference to temporal profile for seasonal scaling |

### Vertical Distribution Properties

| Key | Type | Description |
| --- | --- | --- |
| `vdist_method` | String | Vertical distribution algorithm: `SINGLE`, `RANGE`, `PRESSURE`, `HEIGHT`, `PBL` |
| `vdist_layer_start` | Integer | Starting layer index for `SINGLE`/`RANGE` methods |
| `vdist_layer_end` | Integer | Ending layer index for `RANGE` method |
| `vdist_p_start` | Float | Starting pressure [Pa] for `PRESSURE` method |
| `vdist_p_end` | Float | Ending pressure [Pa] for `PRESSURE` method |
| `vdist_h_start` | Float | Starting height [m] for `HEIGHT` method |
| `vdist_h_end` | Float | Ending height [m] for `HEIGHT` method |

### Complete Example

```yaml
species:
  # Multi-layer CO with hierarchy and scaling
  co:
    - field: "global_co_inventory"
      operation: "add"
      scale: 1.0
      category: "anthropogenic"
      hierarchy: 1
      mask: "land_mask"
      scale_fields: ["temperature"]
      diurnal_cycle: "traffic_diurnal"

    - field: "regional_co_override"
      operation: "replace"           # Overrides global inventory
      scale: 1.2
      category: "anthropogenic"
      hierarchy: 10                  # Higher priority
      mask: "regional_mask"

  # Aircraft NOx with vertical distribution
  nox:
    - field: "surface_nox"
      operation: "add"
      category: "anthropogenic"
      hierarchy: 1
      vdist_method: "PBL"           # Distribute in boundary layer

    - field: "aircraft_nox"
      operation: "add"
      category: "transportation"
      hierarchy: 1
      vdist_method: "HEIGHT"        # Distribute by altitude
      vdist_h_start: 8000.0         # 8 km
      vdist_h_end: 12000.0          # 12 km

  # Biogenic emissions with environmental scaling
  isoprene:
    - field: "base_isoprene"
      operation: "add"
      category: "biogenic"
      hierarchy: 1
      scale_fields: ["temperature", "par", "lai"]
      mask: "vegetation_mask"
      diurnal_cycle: "biogenic_diurnal"
      seasonal_cycle: "growing_season"
```

---

## `physics_schemes`

List of physics schemes to instantiate and execute during the Run phase. Physics schemes can generate new emissions or modify existing ones based on environmental conditions.

| Key | Type | Description |
| --- | --- | --- |
| `name` | String | Registered scheme name (e.g., "sea_salt", "megan", "dust") |
| `language` | String | Implementation language: `cpp` or `fortran` |
| `refresh_interval_seconds` | Integer | (Optional) Execution interval in seconds. Must be a positive multiple of `timestep_seconds`. Default: `0` (use `timestep_seconds`, i.e., run every step). |
| `options` | Map | Scheme-specific configuration parameters |

### Available Physics Schemes

| Scheme Name | Description | Key Parameters |
| ----------- | ----------- | -------------- |
| `sea_salt` | Marine aerosol emissions | `r_sala_min`, `r_salc_max`, `sea_salt_density` |
| `megan` | Biogenic isoprene emissions (single-species) | `beta`, `ldf`, `aef`, `co2_concentration` |
| `megan3` | Full MEGAN3 multi-species biogenic emissions | `mechanism_file`, `speciation_file`, `emission_classes` |
| `bdsnp` | [Berkeley-Dalhousie Soil NOx Parameterization (BDSNP) or YL95 soil NO emissions](soil_nox.md) | `soil_no_method`, `use_soil_temperature` |
| `dust` | Mineral dust emissions | `particle_density`, `tuning_factor` |
| `lightning` | Lightning NOx production | `yield_land`, `yield_ocean` |
| `volcano` | Volcanic SO₂ emissions | `target_location`, `emission_rate` |
| `dms` | Ocean DMS emissions | `schmidt_coeffs`, `transfer_velocity` |

The C++ `bdsnp` scheme accepts two `soil_no_method` values:

- `bdsnp` (default): the validated effective-input, stateless BDSNP
  calculation; and
- `yl95`: the Yienger and Levy (1995) calculation.

Unknown method names are rejected.

Canonical `bdsnp` requires scalar temperature, soil moisture, arid and
non-arid fractions, LAI, wind-speed squared, solar-zenith cosine, fertilizer,
deposited-N, and pulse-factor fields. It also requires exactly 24 layers for
`soilnox_land_fractions` and `soilnox_canopy_nox`, and writes both
`soil_nox_emissions` and `soil_nox_fertilizer_emissions`. All scalar fields and
both outputs must have exactly one level and the same horizontal dimensions.

Pulse, canopy, fertilizer, and deposited-N are supplied effective inputs. The
legacy `bdsnp_fortran` scheme does not implement this contract and is not
equivalent to canonical C++ `bdsnp`. See
[BDSNP Soil NO Emissions](soil_nox.md) for the complete field contract.

**Example:**
```yaml
physics_schemes:
  - name: "sea_salt"
    language: "cpp"
    options:
      r_sala_min: 0.01          # Minimum accumulation mode radius [μm]
      r_sala_max: 0.5           # Maximum accumulation mode radius [μm]
      r_salc_min: 0.5           # Minimum coarse mode radius [μm]
      r_salc_max: 8.0           # Maximum coarse mode radius [μm]
      sea_salt_density: 2200.0  # Particle density [kg/m³]

  - name: "megan"
    language: "cpp"
    options:
      temperature_standard: 303.15    # Standard temperature [K]
      par_conversion: 4.6             # W/m² to μmol/m²/s conversion

  - name: "dust"
    language: "cpp"
    options:
      g_constant: 980.665             # Gravitational acceleration [cm/s²]
      air_density: 1.25e-3            # Air density [g/cm³]
      particle_density: 2.5           # Dust density [g/cm³]
      tuning_factor: 9.375e-10        # Emission tuning coefficient
```

---

## Speciation Files (SPC and MAP)

The MEGAN3 scheme uses two external YAML files to define chemical mechanism speciation. This allows switching between mechanisms (CB6, RACM2, SAPRC07, CRACMM2) at runtime without recompilation.

### SPC File — Mechanism Species Definition

Defines the target mechanism species and their molecular weights. Uses the MICM/OpenAtmos format.

**Format:**

```yaml
name: <mechanism_name>
species:
  - name: <species_name>
    molecular weight [kg mol-1]: <value>
  - name: <species_name>
    molecular weight [kg mol-1]: <value>
```

**Example** (`data/speciation/spc_cb6.yaml`):

```yaml
name: CB6_AE7
species:
  - name: ISOP
    molecular weight [kg mol-1]: 0.06812
  - name: TERP
    molecular weight [kg mol-1]: 0.13623
  - name: PAR
    molecular weight [kg mol-1]: 0.01443
  - name: MEOH
    molecular weight [kg mol-1]: 0.03204
  - name: "NO"
    molecular weight [kg mol-1]: 0.03001
  - name: CO
    molecular weight [kg mol-1]: 0.02801
  # ... up to 36 species for CB6
```

**Rules:**
- `name` key is required (mechanism identifier)
- Each species must have `name` (string) and `molecular weight [kg mol-1]` (positive number)
- Quote `"NO"` to prevent YAML 1.1 boolean interpretation

### MAP File — Speciation Mappings

Defines how emission classes map to mechanism species with per-class scale factors. Uses a dataset-oriented format supporting multiple emission sources.

**Format:**

```yaml
mechanism: <mechanism_name>
datasets:
  <dataset_name>:
    <mechanism_species>:
      <emission_class>: <scale_factor>
      <emission_class>: <scale_factor>
    <mechanism_species>:
      <emission_class>: <scale_factor>
```

**Example** (`data/speciation/map_cb6.yaml`):

```yaml
mechanism: CB6_AE7
datasets:
  MEGAN:
    ISOP:
      ISOP: 1.0
    TERP:
      MT_PINE: 0.5
      MT_ACYC: 0.3
      MT_CAMP: 0.1
      MT_SABI: 0.05
      MT_AROM: 0.05
    MEOH:
      MEOH: 1.0
    SESQ:
      SQT_HR: 0.7
      SQT_LR: 0.3
    "NO":
      "NO": 1.0
    CO:
      CO: 1.0
```

**Rules:**
- `mechanism` key is required and must match the SPC file's `name`
- `datasets` section is required; each key is a dataset name (e.g., `MEGAN`)
- Each mechanism species entry maps emission class names to positive scale factors
- Valid emission classes: `ISOP`, `MBO`, `MT_PINE`, `MT_ACYC`, `MT_CAMP`, `MT_SABI`, `MT_AROM`, `NO`, `SQT_HR`, `SQT_LR`, `MEOH`, `ACTO`, `ETOH`, `ACID`, `LVOC`, `OXPROD`, `STRESS`, `OTHER`, `CO`
- All mechanism species referenced in the MAP must exist in the SPC file

### Using SPC/MAP with MEGAN3

Reference the speciation files in the MEGAN3 scheme configuration:

```yaml
physics_schemes:
  - name: bdsnp
    options:
      # Use the compact YL95 coupling here. Canonical BDSNP requires the full
      # effective-input contract documented in docs/soil_nox.md.
      soil_no_method: yl95

  - name: megan3
    options:
      mechanism_file: data/speciation/spc_cb6.yaml
      speciation_file: data/speciation/map_cb6.yaml
      speciation_dataset: MEGAN
      co2_concentration: 415.0
      emission_classes:
        ISOP:
          ldf: 0.9996
          ct1: 95.0
          cleo: 2.0
          beta: 0.13
          default_aef: 1.0e-9
        MT_PINE:
          ldf: 0.10
          ct1: 80.0
          cleo: 1.83
          beta: 0.10
          default_aef: 3.0e-10
        # ... remaining 17 classes
    output_mapping:
      MEGAN_ISOP: ISOP_BIOG
      MEGAN_TERP: TERP_BIOG
```

The speciation engine computes each output species as:

```
output[TERP] = (class_total[MT_PINE] × 0.5 + class_total[MT_ACYC] × 0.3 + ...) × MW[TERP]
```

### Shipped Mechanism Files

| Mechanism | SPC File | MAP File | Species Count |
| --- | --- | --- | --- |
| CB6_AE7 | `data/speciation/spc_cb6.yaml` | `data/speciation/map_cb6.yaml` | 36 |
| RACM2 | `data/speciation/spc_racm2.yaml` | `data/speciation/map_racm2.yaml` | 43 |
| SAPRC07 | `data/speciation/spc_saprc07.yaml` | `data/speciation/map_saprc07.yaml` | 38 |
| CRACMM2 | `data/speciation/spc_cracmm.yaml` | `data/speciation/map_cracmm.yaml` | 56 |

### Adding a Custom Mechanism

1. Create an SPC file with your mechanism species and molecular weights (kg/mol)
2. Create a MAP file with a `MEGAN` dataset mapping the 19 emission classes to your species
3. Set `mechanism_file` and `speciation_file` in the MEGAN3 config to your file paths

---

## `diagnostics`

Controls diagnostic output and intermediate variable capture for analysis and validation.

| Key | Type | Description |
| --- | --- | --- |
| `output_interval_seconds` | Integer | Frequency of diagnostic output in seconds |
| `variables` | List | List of field names to include in diagnostic output |
| `enabled` | Boolean | Enable/disable diagnostic output (default: true) |

**Example:**
```yaml
diagnostics:
  output_interval_seconds: 3600     # Hourly output
  enabled: true
  variables:
    - "co"
    - "nox"
    - "isoprene"
    - "sea_salt_accumulation"
    - "sea_salt_coarse"
```

---

## `cece_data`

Configuration for data streams that read external emission inventories and auxiliary fields via AMIO (Asynchronous Multidimensional I/O) with AXIS regridding.

### Stream Properties

| Key | Type | Description |
| --- | --- | --- |
| `name` | String | Unique identifier for the data stream |
| `file` | String | Path to NetCDF data file(s) |
| `refresh_interval_seconds` | Integer | (Optional) Data ingestion interval in seconds. Must be a positive multiple of `timestep_seconds`. Default: `0` (use `timestep_seconds`, i.e., ingest every step). |
| `cadence` | String | (Optional) How file records are addressed: `series` (default), `daily`, `monthly`, `hourly`, `weekly`, or `stepwise`. See [Record Selection](#record-selection-cadence). |
| `time_var` | String | (Optional) Name of the time coordinate variable. Default: `time` (falls back to `Time`, `t`, `valid_time`). |
| `time_units` | String | (Optional) Override for the time variable's CF `units` attribute (e.g. `"hours since 2020-01-01 00:00:00"`). Use when the file's attribute is missing or non-standard. |
| `calendar` | String | (Optional) Override for the time variable's CF `calendar` attribute: `gregorian`/`standard`/`proleptic_gregorian`, `noleap`/`365_day`, or `360_day`. |
| `yearFirst` | Integer | First calendar year of data coverage in the file. Only consulted on the arithmetic fallback path (`daily`/`monthly` cadence with an undecodable time axis). |
| `yearLast` | Integer | Last calendar year of data coverage in the file. Same applicability as `yearFirst`. |
| `yearAlign` | Integer | Simulation year corresponding to the first file year. Default: `0` (no shift, simulation years map 1-to-1 onto file years). |
| `taxmode` | String | Behavior when the simulation time falls outside the file's coverage: `cycle` (default, wrap), `extend` (clamp to the nearest end), or `limit` (fail). |
| `tintalgo` | String | Temporal interpolation: `linear` or `nearest`. Default: `nearest`. Ignored by `hourly`/`weekly`/`stepwise` cadences. |
| `mapalgo` | String | Spatial regridding: `consd`, `bilinear`, `consf`, `nn`, `redist`, or `passthrough`. `passthrough` requires identical dimensions and ordered source/target coordinates, then copies without AXIS regridding. |
| `data_model` | String | (Optional) AMIO NetCDF data model for reads: `enhanced`, `classic`, or `auto`. Default behavior is auto (`enhanced` first, then `classic` fallback on backend open failure). |
| `variables` | List | Variable mappings between file and model |

### Variable Mapping

| Key | Type | Description |
| --- | --- | --- |
| `file` | String | Variable name in NetCDF file |
| `model` | String | Internal field name in CECE |
| `levels` | Integer | (Optional) Number of nonspatial layers for this variable. Defaults to the global model `nz`; values must be positive. |

For canonical BDSNP, the two biome-dependent fields use 24 layers while
scalar fields omit `levels`:

```yaml
variables:
  - file: SOILNOX_LAND_FRACTIONS
    model: soilnox_land_fractions
    levels: 24
  - file: SOILNOX_CANOPY_NOX
    model: soilnox_canopy_nox
    levels: 24
```

**Example:**
```yaml
cece_data:
  streams:
    - name: "MACCITY_CO"
      file: "/data/inventories/MACCity_CO_2000-2010.nc"
      # cadence omitted -> "series": the file's own time axis is decoded and the
      # simulation time is bracketed against the actual record times.
      taxmode: "cycle"          # Repeat the file's coverage outside 2000-2010
      tintalgo: "linear"        # Linear interpolation between bracketing records
      mapalgo: "consd"          # Conservative regridding
      variables:
        - file: "MACCity_CO"    # Variable name in file
          model: "global_co_inventory"  # Internal CECE field name

    - name: "HTAP_NOX"
      file: "/data/inventories/HTAPv3_NOx_*.nc"  # Wildcard for multiple files
      yearAlign: 2020           # Simulation year 2020 aligns to the first file year
      taxmode: "extend"         # Clamp to the nearest end outside the covered range
      tintalgo: "linear"
      mapalgo: "consd"
      data_model: "classic"     # Force classic model for legacy files
      variables:
        - file: "NOx_TOTAL"
          model: "regional_nox_override"

    - name: "DIURNAL_PROFILE"
      file: "/data/profiles/diurnal_nox.nc"
      cadence: "hourly"         # Profile: select record by hour-of-day (0-23)
      mapalgo: "consd"
      variables:
        - file: "NOx_HOURLY"
          model: "nox_diurnal_scale"

    - name: "MONTHLY_CLIM"
      file: "/data/climatology/monthly_co.nc"
      cadence: "monthly"        # Series with a monthly arithmetic fallback
      tintalgo: "linear"        # Mid-month linear interpolation
      mapalgo: "consd"
      variables:
        - file: "CO_MONTHLY"
          model: "co_monthly_clim"
```

### Record Selection (`cadence`)

`cadence` selects *how* a stream's file records are addressed for a given simulation
date-time. There are three kinds:

| `cadence` | Kind | Behavior |
| --- | --- | --- |
| *(omitted)*, `series` | Series | Decode the file's CF time axis and bracket the simulation time against the actual record times. Honors `tintalgo`, `taxmode`, and `yearAlign`. |
| `daily`, `monthly` | Series | Same as `series`, but if the time axis cannot be decoded, degrade to calendar arithmetic at the stated granularity (day-of-year / month-of-year) using `yearFirst`, `yearLast`, `yearAlign`, and `taxmode`. |
| `hourly`, `weekly` | Profile | Climatological profile indexed directly by a calendar field: hour-of-day (records 0–23) or day-of-week (records 0–6, 0 = Monday). The time axis is not read. `tintalgo`, `taxmode`, `yearAlign`, `yearFirst`, and `yearLast` are ignored (a warning is logged if set). |
| `stepwise` (alias `step`) | Stepwise | Opt-in step-index cycling: record `= step_index % n_records`. Time is ignored entirely. `tintalgo`, `taxmode`, and `yearAlign` are ignored (a warning is logged if set). |

Any other value is a configuration error.

> **Changed behavior:** omitting `cadence` used to mean step-index cycling. It now
> means `series` (time-aware record selection), which is almost always what is
> wanted. Set `cadence: stepwise` to opt back in to the old index-walking behavior.

#### Decoding the time axis

For `series` (and the `daily`/`monthly` variants) the driver reads the stream's time
coordinate variable — `time_var`, defaulting to `time` with fallbacks to `Time`, `t`,
and `valid_time` — together with its CF `units` (`"<unit> since <reference>"`) and
`calendar` attributes. Only fixed-length units are decodable: seconds, minutes, hours,
and days. Months and years are calendar-ambiguous and are treated as undecodable.

Use `time_units` and `calendar` to supply these values for files whose attributes are
missing or non-standard; the config values take precedence over the file's attributes.

If the axis cannot be decoded and the cadence carries no granularity to fall back on
(that is, plain `series` on a file with more than one record), the run fails with a
message pointing at the three ways out: set `time_units`, use `cadence: daily`/`monthly`,
or use `cadence: stepwise` to ignore time altogether. Files with a single record always
resolve to record 0.

#### Numeric types and packed data

Variables and coordinates are read using the element type the file declares, so
integer-valued time axes, coordinates, and data (all common in CF files) decode
correctly. CF packing attributes are applied on read wherever they are present:

$$\text{value} = \text{stored} \times \text{scale\_factor} + \text{add\_offset}$$

This needs no configuration — `scale_factor` and `add_offset` are picked up from the
file, and a variable without them is read unchanged.

#### Interpolation

`tintalgo: nearest` (the default) reads the single closest record. `tintalgo: linear`
reads the two bracketing records and blends them on the source grid before regridding.
On the arithmetic fallback path, `monthly` uses the mid-month convention and `daily`
uses the mid-day convention, so e.g. January 1 blends the December and January records.

### Temporal Alignment Semantics (`yearAlign`, `taxmode`)

`yearAlign` shifts the simulation time onto the file's time axis: it names the
simulation year that corresponds to the file's **first** year. `yearAlign: 0` (the
default) means no shift — simulation years map 1-to-1 onto file years. On the
arithmetic fallback path the dataset year is computed as:

$$\text{effective\_year} = \text{yearFirst} + (\text{sim\_year} - \text{yearAlign})$$

`taxmode` decides what happens when the simulation time falls outside the file's
coverage:

* **`cycle`** (default): wrap the simulation time back into the file's range, repeating
  the file's coverage indefinitely. The repeat period runs one record interval past the
  last record, so a 48-record hourly file repeats every 48 hours and simulation hour 53
  reads record 5.
* **`extend`**: clamp to the nearest end of the file's coverage. Useful for running past
  the last year of an inventory.
* **`limit`**: no record is resolved and the run fails.

`yearFirst` and `yearLast` describe the file's year coverage. They are only consulted on
the arithmetic fallback path; when the time axis is decodable the record times carry that
information themselves, so setting them on a `series` stream logs a warning.

### Practical Applications & Common Use Cases

#### 1. Future Projections / Present-Day Runs with Historical Inventories
**Scenario:** You are running a simulation for year 2026, but your emissions inventory (e.g., CEDS or HTAP) only extends through 2023. You want all simulation years $\ge 2023$ to use the 2023 emission rates.
```yaml
cece_data:
  streams:
    - name: "CEDS_ANTHRO_NOX"
      file: "/data/emissions/CEDS_NOx_2000-2023.nc"
      cadence: "monthly"
      taxmode: "extend"         # Sim year 2026 clamps to the last file record
      tintalgo: "linear"
      # yearFirst/yearLast only matter if the time axis cannot be decoded:
      yearFirst: 2000
      yearLast: 2023
```

#### 2. Historical Reanalysis / Hindcast Runs
**Scenario:** You are running a historical simulation (e.g., 2010–2015) using a multi-year dataset that covers 2000–2020. Each simulation year should strictly use the corresponding file year.
```yaml
cece_data:
  streams:
    - name: "HISTORICAL_EMISSIONS"
      file: "/data/emissions/inventory_2000-2020.nc"
      cadence: "monthly"
      yearAlign: 0              # 1-to-1 mapping (sim year 2015 -> file year 2015)
      taxmode: "limit"          # Fail if the simulation leaves 2000-2020
      tintalgo: "linear"
```

#### 3. Repetitive Multi-Year Cycles
**Scenario:** You have a 10-year dataset (2000–2010) and want to repeat this 10-year cycle continuously for long-term climate or air quality simulations (e.g., 2011–2030).
```yaml
cece_data:
  streams:
    - name: "CYCLING_INVENTORY"
      file: "/data/emissions/inventory_2000-2010.nc"
      cadence: "monthly"
      taxmode: "cycle"          # Sim year 2011 wraps back to the start of the file
      tintalgo: "linear"
```

#### 4. Applying a Single-Year Inventory to a Different Simulation Year
**Scenario:** You have a single-year inventory for year 2010, but you are running a simulation for year 2020.
```yaml
cece_data:
  streams:
    - name: "SINGLE_YEAR_INVENTORY"
      file: "/data/emissions/inventory_2010.nc"
      cadence: "monthly"
      yearAlign: 2020           # Sim year 2020 aligns to the first file year (2010)
      taxmode: "extend"
      tintalgo: "linear"
```

#### 5. Monthly Climatologies (12-Record Files)
**Scenario:** You have a 12-month climatology file (Jan–Dec) that applies identically to every simulation year.
```yaml
cece_data:
  streams:
    - name: "MONTHLY_CLIMATOLOGY"
      file: "/data/climatology/monthly_isoprene.nc"
      cadence: "monthly"        # Falls back to month-of-year indexing if the
                                # 12-record axis has no decodable units
      tintalgo: "linear"        # Smooth mid-month interpolation
      taxmode: "cycle"          # Reuse the same 12 records every year
```

#### 6. Daily Climatologies and Daily Emissions Files
**Scenario:** Reading daily data files (e.g., 365/366-day daily climatology or multi-year daily emissions).
```yaml
cece_data:
  streams:
    - name: "DAILY_CLIMATOLOGY"
      file: "/data/climatology/daily_emissions.nc"
      cadence: "daily"          # Falls back to day-of-year indexing (records 0-364/365)
      tintalgo: "linear"        # Smooth intra-day linear interpolation
      taxmode: "cycle"

    - name: "MULTIYEAR_DAILY_EMISSIONS"
      file: "/data/emissions/daily_inventory_2000-2023.nc"
      cadence: "daily"
      taxmode: "extend"
      tintalgo: "linear"
      yearFirst: 2000           # Fallback-path coverage hints
      yearLast: 2023
```

#### 7. Diurnal (Hourly) and Weekly Variation Profiles
**Scenario:** Applying 24-hour diurnal scale factors or 7-day weekly scale factors. These
are climatological profiles, not time series, so the file's time axis is not consulted and
no other temporal keys apply.
```yaml
cece_data:
  streams:
    - name: "DIURNAL_SCALE"
      file: "/data/profiles/diurnal_factors.nc"
      cadence: "hourly"         # Selects record 0-23 by hour of day

    - name: "WEEKLY_SCALE"
      file: "/data/profiles/weekly_factors.nc"
      cadence: "weekly"         # Selects record 0-6 by day of week (0=Mon ... 6=Sun)
```

#### 8. Sub-Daily Time Series (e.g. Hourly Emissions over a Date Range)
**Scenario:** An hourly emissions file spanning several days. This is a *series*, not an
hour-of-day profile — each record has its own date, so `cadence: hourly` would be wrong.
```yaml
cece_data:
  streams:
    - name: "HOURLY_FIRE_EMISSIONS"
      file: "/data/emissions/fire_hourly_20240701-20240707.nc"
      # cadence omitted -> series: bracket the simulation time on the decoded axis
      tintalgo: "linear"
      taxmode: "extend"         # Hold the last record once the run outlasts the file
```

#### 9. Ignoring Time Entirely (Legacy Step Cycling)
**Scenario:** Reproducing the pre-`series` default, where record `n` is read on step `n`
regardless of the simulation date.
```yaml
cece_data:
  streams:
    - name: "STEP_CYCLED_INPUT"
      file: "/data/inventories/records.nc"
      cadence: "stepwise"       # record = step_index % n_records; time is ignored
```

#### 10. Files with Missing or Non-Standard Time Metadata
**Scenario:** A file whose time variable is named `valid_time` and carries no `units`
attribute, so the axis cannot be decoded from the file alone.
```yaml
cece_data:
  streams:
    - name: "LEGACY_INVENTORY"
      file: "/data/inventories/legacy_no_units.nc"
      time_var: "valid_time"
      time_units: "hours since 2020-01-01 00:00:00"
      calendar: "noleap"
      tintalgo: "linear"
```

---

## `output`

Configuration for NetCDF output file generation with emission fields and diagnostics.

| Key | Type | Description |
| --- | --- | --- |
| `enabled` | Boolean | Enable NetCDF output. Presence of the `output:` block enables output unless `enabled: false`; with no block, output is disabled. `enabled: false` keeps the rest of the block as dormant configuration. |
| `directory` | String | Output directory path |
| `filename_pattern` | String | Filename template with time substitution |
| `frequency_steps` | Integer | Output frequency in timesteps |
| `fields` | List | Fields to write; each entry is either a field name string or a map with `name` and optional `attributes` |
| `diagnostics` | Boolean | Also write diagnostic fields (default: false) |
| `amio_worker_threads` | Integer | (Optional) Number of AMIO background I/O worker threads for output. Must be ≥ 1 when explicitly set; nonpositive values are rejected. When omitted, falls back to `driver.amio_worker_threads`. |
| `global_attributes` | Map | (Optional) Map of custom NetCDF global attributes to write verbatim on the output file, overriding any defaults. |

### Fields and Attributes

Each `fields` entry selects a field to write and optionally carries the
NetCDF attributes written on that variable. A plain string is shorthand for
a field with no configured attributes; the map form pairs the required
`name` with an optional flat `attributes` map (attribute name → value).

```yaml
output:
  fields:
    - name: co
      attributes:
        units: "kg m-2 s-1"
        long_name: "carbon_monoxide_emission_flux"
    - nox                        # shorthand: written with no configured attributes
    - name: isoprene
      attributes:
        units: "kg m-2 s-1"
        long_name: "isoprene_emission_flux"
        coordinates: "lat lon"
```

Semantics:

- Only configured attributes are emitted. A field without an `attributes`
  map gets no attributes — the writer never fabricates `units` or
  `long_name`.
- The `coordinates` attribute is the one exception: it defaults to
  the written field shape (`"time lev lat lon"`) for every field, and can be
  overridden per field via `attributes`.
- The coordinate variables `lon`, `lat`, `lev`, and `time` carry fixed
  built-in attributes and are not affected by `attributes`.
- `units`, `short_name`, and `long_name` are currently optional; they will
  eventually be required on every output field, sourced from a
  field-metadata dictionary with the inline `attributes` map acting as the
  per-config override layer.

### Custom Global Attributes

The optional `global_attributes` map allows you to specify custom global NetCDF attributes to write verbatim on the output files, overriding any default attributes:

```yaml
output:
  global_attributes:
    title: "My Custom MPAS Emission Simulation Run"
    institution: "National Center for Atmospheric Research (NCAR)"
    references: "Custom project publication URL (2026)"
```

The standalone writer automatically populates a standard set of geoscientific default attributes (`title`, `Conventions`, `institution`, `source`, `history`, `references`, `comment`, and `gridspec_file`). Any key-value pair specified under `global_attributes` overrides these defaults, while other omitted keys retain their professional defaults.

### Filename Pattern Substitutions

| Token | Description |
| ----- | ----------- |
| `{YYYY}` | 4-digit year |
| `{MM}` | 2-digit month |
| `{DD}` | 2-digit day |
| `{HH}` | 2-digit hour |
| `{mm}` | 2-digit minute |
| `{ss}` | 2-digit second |

**Example:**
```yaml
output:
  enabled: true
  directory: "./output"
  filename_pattern: "cece_emissions_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1            # Output every timestep
  fields:
    - name: "co"
      attributes:
        units: "kg m-2 s-1"
        long_name: "carbon_monoxide_emission_flux"
    - name: "nox"
      attributes:
        units: "kg m-2 s-1"
        long_name: "nitrogen_oxides_emission_flux"
    - "isoprene"
    - "sea_salt_total"
```
