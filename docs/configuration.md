# Configuration Reference

ACES is configured using a YAML file, typically named `aces_config.yaml`. This file defines the species to be processed, the source of data, and the active physics schemes.

## Top-Level Structure

```yaml
meteorology:
  # ... meteorology name mappings ...

scale_factors:
  # ... scale factor name mappings ...

masks:
  # ... mask name mappings ...

temporal_profiles:
  # ... periodic scaling factors (diurnal, weekly, etc.) ...

species:
  # ... species definitions ...

physics_schemes:
  # ... list of schemes ...

diagnostics:
  # ... diagnostic settings ...

aces_data:
  # ... CDEPS settings ...
```

---

## `species`

The `species` block defines the emission targets and the layers that contribute to them.

### Layer Properties

| Key | Type | Description |
| --- | --- | --- |
| `field` | String | Name of the input field. ACES looks in CDEPS first, then ESMF ImportState. |
| `operation` | String | `add` (accumulate) or `replace` (overwrite existing value). |
| `scale` | Float | Base scaling factor (Default: `1.0`). |
| `category` | String | Grouping for layers. |
| `hierarchy` | Integer | Priority within the category. Higher values overwrite lower ones. |
| `mask` | String | (Optional) Name of a 2D/3D field to use as a geographical mask. |
| `scale_fields` | List | (Optional) List of field names to multiply with the base field. |

**Example:**
```yaml
meteorology:
  temperature: air_temperature

scale_factors:
  hourly_scalfact: HOURLY_SCALFACT

masks:
  land_mask: LAND_MASK

temporal_profiles:
  diurnal: [1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 3.0, 3.1, 3.2, 3.3]

species:
  nox:
    - field: "base_nox"
      operation: "add"
      scale: 1.0
      category: "anthropogenic"
      hierarchy: 1
      mask: "land_mask"
      scale_fields: ["temperature", "hourly_scalfact"]
      diurnal_cycle: "diurnal"
```

---

## `physics_schemes`

List of physics plugins to instantiate and execute during the `Run` phase.

| Key | Type | Description |
| --- | --- | --- |
| `name` | String | Registered name of the scheme (e.g., "native_example"). |
| `language` | String | `cpp` or `fortran`. |
| `options` | Map | Scheme-specific configuration passed to `Initialize`. |

**Example:**
```yaml
physics_schemes:
  - name: "sea_salt"
    language: "cpp"
    options:
      emission_mode: "monahan"
```

---

## `diagnostics`

Configures the output of intermediate variables.

| Key | Type | Description |
| --- | --- | --- |
| `output_interval` | Integer | Frequency of output in seconds. |
| `variables` | List | Names of variables to write. These must be registered by schemes. |
| `grid_type` | String | `native`, `gaussian`, or `mesh`. |
| `nx`, `ny` | Integer | Dimensions for `gaussian` or `native` grids. |
| `grid_file` | String | Path to ESMF mesh file (if `grid_type: mesh`). |

---

## `aces_data`

Configures the CDEPS-inline streams for reading data from disk.

| Key | Type | Description |
| --- | --- | --- |
| `streams` | List | List of stream definitions. |

### Stream Properties
- `name`: Field name as used in the `species` block.
- `file`: Path to the NetCDF file.
- `interpolation`: `linear` or `none`.

**Example:**
```yaml
aces_data:
  streams:
    - name: "base_nox"
      file: "data/inventories/nox_2024.nc"
      interpolation: "linear"
```
