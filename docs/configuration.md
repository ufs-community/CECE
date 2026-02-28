# Configuration Reference

ACES is configured using a YAML file, typically named `aces_config.yaml`. This file defines the species to be processed, the source of data, and the active physics schemes.

## Top-Level Structure

```yaml
species:
  # ... species definitions ...

physics_schemes:
  # ... list of schemes ...

diagnostics:
  # ... diagnostic settings ...

cdeps_inline_config:
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
species:
  nox:
    - field: "base_nox"
      operation: "add"
      scale: 1.0
      category: "anthropogenic"
      hierarchy: 1
      scale_fields: ["temperature"]
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

## `cdeps_inline_config`

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
cdeps_inline_config:
  streams:
    - name: "base_nox"
      file: "data/inventories/nox_2024.nc"
      interpolation: "linear"
```
