# CECE Scripts and Utilities

CECE provides several Python scripts to facilitate data management, configuration migration, and visualization of the emission stacking process.

## Data Management

### `download_hemco_data.py`
Downloads required emission inventories from the public GEOS-Chem S3 bucket.
```bash
python scripts/download_hemco_data.py --config cece_config.yaml --dest data/
```

### `verify_hemco_data.py`
Validates the integrity of downloaded NetCDF files and ensures all required variables are present.
```bash
python scripts/verify_hemco_data.py --config cece_config.yaml --data-dir data/
```

### `setup_hemco_examples.sh`
Automates the creation of example CECE configuration files and generates download scripts for the associated data.
```bash
./scripts/setup_hemco_examples.sh
```

### `stage_earthaccess_streams.py`
Stages NASA Earthdata-backed `source: earthaccess` streams into CECE's native per-step cache format. Run it on a login or data-transfer node with outbound HTTPS access, then point compute jobs at the staged directory with `CECE_EARTHACCESS_STAGE_DIR`.

The staging helper downloads protected granules once into a shared `granules/` cache under the staging directory before extracting per-timestep slices with `xarray+h5netcdf`. Each `step_N/` subdirectory contains only the lightweight `manifest.txt`, grid coordinates, and raw `*.f64` 2D field arrays for that model step. The `cloud` extra explicitly installs `h5py`, which provides h5netcdf's HDF5 backend. Streams should use NetCDF4/HDF5-readable collections; HDF-EOS `.hdf` granules are detected during preflight and should be pre-converted to NetCDF before compute-node runs.
```bash
python scripts/stage_earthaccess_streams.py \
	--config examples/cece_config_earthaccess_megan3.yaml \
	--stage-dir /scratch/$USER/cece_earthaccess_stage \
	--overwrite

python scripts/stage_earthaccess_streams.py \
	--config examples/cece_config_earthaccess_megan3.yaml \
	--stage-dir /scratch/$USER/cece_earthaccess_stage \
	--preflight-only \
	--check-download-access \
	--auth-strategy netrc

export CECE_EARTHACCESS_STAGE_DIR=/scratch/$USER/cece_earthaccess_stage
```

For `--auth-strategy netrc`, store the Earthdata username and account password in `~/.netrc` and run `unset EARTHDATA_TOKEN` before preflight. `EARTHDATA_TOKEN` is only for a current bearer token; an expired token causes CMR `401 Unauthorized: Token does not exist` responses.

With `--check-download-access`, preflight also opens each downloaded sample and verifies that its configured variable names exist. An `EulaNotAccepted` failure includes a protected data URL. Open that URL in a browser while signed in with the same account as `~/.netrc`, authorize the GES DISC application, accept any displayed terms, and verify the application under Authorized Apps at <https://urs.earthdata.nasa.gov/profile> before rerunning preflight.

### `ursa_earthaccess_staged_run.slurm`
Example Ursa workflow that prepares the EarthAccess Python environment and stages remote streams on a login node, then submits a Slurm job that consumes the staged cache without network access from compute nodes.
```bash
bash scripts/ursa_earthaccess_staged_run.slurm
```

### `ursa_earthaccess_live_run.slurm`
Example direct-streaming job for Ursa's externally connected `u1-service`
partition. It requests one task, 64 allocated CPUs, and 240 GB explicitly while
limiting CECE to eight application threads. Omitting `--mem` may leave the job
with too little memory for xarray's lazy remote NetCDF reads. Ursa limits each
user on this partition to 64 cores and/or 250 GB of memory, so this request uses
the user's full CPU allowance and only one copy can run at a time.

Prepare `.venv-ursa-earthaccess` and `build-ursa-earthaccess` first, then submit
from the repository root:

```bash
sbatch scripts/ursa_earthaccess_live_run.slurm
sacct -j <job-id> --format=JobID,State,Elapsed,ReqMem,MaxRSS,ExitCode
```

Ursa enforces an effective maximum memory per allocated CPU. A 240 GB request
with only eight requested CPUs was normalized to approximately 63 CPUs, leaving
`SLURM_CPUS_PER_TASK=63` in conflict with `SLURM_TRES_PER_TASK=cpu=8`. Requesting
64 CPUs explicitly keeps those Slurm values consistent. This is accounting and
scheduling capacity; `OMP_NUM_THREADS=8` still limits application concurrency.
CECE YAML does not control Slurm task allocation.

After a successful representative run, reduce `--mem` to roughly 20% above the
reported `MaxRSS` if that value is substantially below 240 GB.

---

## Configuration Migration

### `hemco_to_cece.py`
Converts legacy HEMCO `.rc` configuration files to the CECE YAML format. It handles:

- Recursive includes (`>>>include`)
- `$ROOT` token replacement
- Mapping scale factors and masks to CECE layers
- Parsing grid and diagnostic definitions from auxiliary files

```bash
python scripts/hemco_to_cece.py HEMCO_Config.rc -o cece_config.yaml
```

---

## Visualization

### `visualize_stack.py`
Generates a visual representation (graph) of the emission stacking hierarchy defined in an CECE configuration file. This is useful for verifying that layers, masks, and scale factors are correctly prioritized.
```bash
python scripts/visualize_stack.py --config cece_config.yaml --output stacking_plan.png
```

### `visualize_optimized_stack.py`
Similar to `visualize_stack.py`, but specifically visualizes the fused kernel plan used by the optimized CECE engine.
```bash
python scripts/visualize_optimized_stack.py --config cece_config.yaml --output optimized_plan.png
```
