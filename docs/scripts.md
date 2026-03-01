# ACES Scripts and Utilities

ACES provides several Python scripts to facilitate data management, configuration migration, and visualization of the emission stacking process.

## Data Management

### `download_hemco_data.py`
Downloads required emission inventories from the public GEOS-Chem S3 bucket.
```bash
python scripts/download_hemco_data.py --config aces_config.yaml --dest data/
```

### `verify_hemco_data.py`
Validates the integrity of downloaded NetCDF files and ensures all required variables are present.
```bash
python scripts/verify_hemco_data.py --config aces_config.yaml --data-dir data/
```

### `setup_hemco_examples.sh`
Automates the creation of example ACES configuration files and generates download scripts for the associated data.
```bash
./scripts/setup_hemco_examples.sh
```

---

## Configuration Migration

### `hemco_to_aces.py`
Converts legacy HEMCO `.rc` configuration files to the ACES YAML format. It handles:
- Recursive includes (`>>>include`)
- `$ROOT` token replacement
- Mapping scale factors and masks to ACES layers
- Parsing grid and diagnostic definitions from auxiliary files

```bash
python scripts/hemco_to_aces.py HEMCO_Config.rc -o aces_config.yaml
```

---

## Visualization

### `visualize_stack.py`
Generates a visual representation (graph) of the emission stacking hierarchy defined in an ACES configuration file. This is useful for verifying that layers, masks, and scale factors are correctly prioritized.
```bash
python scripts/visualize_stack.py --config aces_config.yaml --output stacking_plan.png
```

### `visualize_optimized_stack.py`
Similar to `visualize_stack.py`, but specifically visualizes the fused kernel plan used by the optimized ACES engine.
```bash
python scripts/visualize_optimized_stack.py --config aces_config.yaml --output optimized_plan.png
```
