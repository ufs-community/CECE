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
  # ... TIDE data stream settings ...

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

The `grid` section defines the computational domain and resolution.

| Key | Type | Description |
| --- | --- | --- |
| `nx` | Integer | Number of grid points in longitude (x-direction) |
| `ny` | Integer | Number of grid points in latitude (y-direction) |
| `lon_min` | Float | Western boundary longitude [degrees] |
| `lon_max` | Float | Eastern boundary longitude [degrees] |
| `lat_min` | Float | Southern boundary latitude [degrees] |
| `lat_max` | Float | Northern boundary latitude [degrees] |

**Example:**
```yaml
grid:
  nx: 144           # 2.5° longitude resolution
  ny: 91            # 2° latitude resolution
  lon_min: -180.0
  lon_max: 177.5
  lat_min: -90.0
  lat_max: 90.0
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
- **Weekly**: 7 values (daily scale factors for Sunday-Saturday)
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
| `field` | String | Name of the input field. CECE looks in TIDE first, then ESMF ImportState. |
| `operation` | String | How to combine with existing data: `add`, `multiply`, `replace`, or `set` |
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
| `bdsnp` | Soil NO emissions (BDSNP/YL95) | `soil_no_method`, `fert_emission_factor` |
| `dust` | Mineral dust emissions | `particle_density`, `tuning_factor` |
| `lightning` | Lightning NOx production | `yield_land`, `yield_ocean` |
| `volcano` | Volcanic SO₂ emissions | `target_location`, `emission_rate` |
| `dms` | Ocean DMS emissions | `schmidt_coeffs`, `transfer_velocity` |

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
      soil_no_method: bdsnp

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

Configuration for TIDE (Temporal Interpolation & Data Extraction) data streams for reading external emission inventories and auxiliary fields.

### Stream Properties

| Key | Type | Description |
| --- | --- | --- |
| `name` | String | Unique identifier for the data stream |
| `file` | String | Path to NetCDF data file(s) |
| `refresh_interval_seconds` | Integer | (Optional) Data ingestion interval in seconds. Must be a positive multiple of `timestep_seconds`. Default: `0` (use `timestep_seconds`, i.e., ingest every step). |
| `yearFirst` | Integer | First year of data coverage |
| `yearLast` | Integer | Last year of data coverage |
| `yearAlign` | Integer | Simulation year to align with data |
| `taxmode` | String | Time axis mode: `cycle`, `extend`, or `limit` |
| `tintalgo` | String | Temporal interpolation: `linear`, `nearest`, or `bpch` |
| `mapalgo` | String | Spatial regridding: `consd`, `bilinear`, or `patch` |
| `variables` | List | Variable mappings between file and model |

### Variable Mapping

| Key | Type | Description |
| --- | --- | --- |
| `file` | String | Variable name in NetCDF file |
| `model` | String | Internal field name in CECE |

**Example:**
```yaml
cece_data:
  streams:
    - name: "MACCITY_CO"
      file: "/data/inventories/MACCity_CO_2010.nc"
      yearFirst: 2000
      yearLast: 2010
      yearAlign: 2020           # Use 2010 data for year 2020
      taxmode: "cycle"          # Repeat yearly cycle
      tintalgo: "linear"        # Linear time interpolation
      mapalgo: "consd"          # Conservative regridding
      variables:
        - file: "MACCity_CO"    # Variable name in file
          model: "global_co_inventory"  # Internal CECE field name

    - name: "HTAP_NOX"
      file: "/data/inventories/HTAPv3_NOx_*.nc"  # Wildcard for multiple files
      yearFirst: 2018
      yearLast: 2018
      yearAlign: 2020
      taxmode: "extend"         # Extend last value beyond data range
      tintalgo: "linear"
      mapalgo: "consd"
      variables:
        - file: "NOx_TOTAL"
          model: "regional_nox_override"
```

---

## `output`

Configuration for NetCDF output file generation with emission fields and diagnostics.

| Key | Type | Description |
| --- | --- | --- |
| `enabled` | Boolean | Enable NetCDF output (default: false) |
| `directory` | String | Output directory path |
| `filename_pattern` | String | Filename template with time substitution |
| `frequency_steps` | Integer | Output frequency in timesteps |
| `fields` | List | List of field names to write to output |

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
    - "co"
    - "nox"
    - "isoprene"
    - "sea_salt_total"
```
