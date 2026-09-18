# Using the Provided Examples

CECE includes several example configurations that demonstrate common emission stacking scenarios, modeled after examples in the HEMCO guide and showcasing advanced CECE features.

## Example Scenarios

The `examples/` directory contains several YAML configuration files:

-   `cece_config_ex1.yaml`: Basic single CO species with data stream ingestion
-   `cece_config_ex2.yaml`: Overlaying a regional European CO inventory on a global background
-   `cece_config_ex3.yaml`: Simple testing configuration with minimal grid
-   `cece_config_ex4.yaml`: Using the GFED4 extension for biomass burning
-   `cece_config_ex5.yaml`: Multi-species (CO and NO) emissions with multi-timestep execution
-   `cece_config_ex6.yaml`: Handling non-separated inventories
-   `cece_config_advanced.yaml`: **NEW** - Comprehensive example demonstrating advanced Stacking Engine features
-   `cece_config_earthaccess.yaml`: Cloud-native NASA Earthdata streaming with `earthaccess`

### Advanced Example Highlights

The `cece_config_advanced.yaml` example showcases sophisticated emission processing capabilities:

- **Hierarchical Layer Processing**: Multiple priority levels within categories
- **Temporal Scaling**: Diurnal, weekly, and seasonal emission cycles
- **Vertical Distribution**: Multiple algorithms (PBL, HEIGHT, PRESSURE) for different source types
- **Environmental Dependencies**: Temperature, PAR, and LAI-dependent scaling
- **Geographical Masking**: Land/ocean/vegetation/regional masks
- **Physics Scheme Integration**: Active MEGAN, sea salt, and dust schemes
- **Multi-Source Integration**: Data streams from multiple emission inventories

For complete technical details about how these features work, see the [Stacking Engine Documentation](stacking_engine.md).

---

## Configuration Features by Example

| Example | Grid Size | Species | Key Features |
|---------|-----------|---------|---------------|
| ex1 | 4×4 | CO | Basic data stream integration, simple stacking |
| ex2 | (varies) | CO | Regional override with hierarchy |
| ex3 | 2×2 | CO | Minimal test configuration |
| ex4 | (varies) | Multiple | Biomass burning with GFED4 |
| ex5 | 4×4 | CO, NO | Multi-species, multi-timestep execution |
| ex6 | (varies) | Multiple | Non-separated inventory handling |
| **advanced** | **144×91** | **CO, NOx, Isoprene** | **All advanced features demonstrated** |
| earthaccess | HEMCO 4×5 | Isoprene, soil NO, dust | NASA Earthdata cloud streams via `earthaccess` |

---

## Installing Cloud Extras for Earthdata Tests

The Earthaccess example reads NASA Earthdata cloud-hosted granules directly instead of staging local NetCDF files. Install CECE's optional `cloud` dependencies before running that workflow.

For tests against this source checkout or development branch, install CECE in editable mode from the repository root:

```bash
cd /path/to/CECE
python -m pip install --upgrade pip setuptools wheel
python -m pip install -e '.[cloud,test]'
```

The quoted `'.[cloud,test]'` argument installs the local package plus optional dependency groups declared in `pyproject.toml`:

-   `cloud`: `earthaccess`, `xarray`, `h5netcdf`, `h5py`, `fsspec`, `s3fs`, and `dask`
-   `test`: `pytest`

The quotes are important because many shells treat square brackets as glob characters. Quoting ensures `pip` receives the extras expression unchanged.

If you are installing a published CECE package instead of the local checkout, use:

```bash
python -m pip install 'cece-tools[cloud]'
```

For an isolated local test environment, create and activate a virtual environment first:

```bash
cd /path/to/CECE
python -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip setuptools wheel
python -m pip install -e '.[cloud,test]'
```

Verify that the cloud stack imports in the same Python environment that will run the tests:

```bash
python - <<'PY'
import earthaccess
import xarray
import h5netcdf
import h5py
import fsspec
import s3fs
import dask

print("earthaccess:", getattr(earthaccess, "__version__", "installed"))
print("xarray:", xarray.__version__)
print("h5netcdf:", h5netcdf.__version__)
print("h5py:", h5py.__version__)
print("fsspec:", fsspec.__version__)
print("s3fs:", s3fs.__version__)
print("dask:", dask.__version__)
PY
```

### Earthdata Credentials

Live Earthdata tests require NASA Earthdata Login credentials. Create an Earthdata account at <https://urs.earthdata.nasa.gov/> if needed, then make the credentials available to `earthaccess`.

The recommended approach for local testing is `~/.netrc`:

```bash
cat > ~/.netrc <<'EOF'
machine urs.earthdata.nasa.gov
	login YOUR_EARTHDATA_USERNAME
	password YOUR_EARTHDATA_PASSWORD
EOF

chmod 600 ~/.netrc
```

Alternatively, export a username/password pair in the current shell:

```bash
export EARTHDATA_USERNAME='your-username'
export EARTHDATA_PASSWORD='your-password'
```

Or export a current Earthdata bearer token by itself:

```bash
export EARTHDATA_TOKEN='your-current-earthdata-token'
```

Do not put an Earthdata token in the `.netrc` password field. When using `--auth-strategy netrc`, clear any stale token first with `unset EARTHDATA_TOKEN`. A CMR response containing `401 Unauthorized` and `Token does not exist` means the exported bearer token is invalid or expired; generate a new token or use the `.netrc` username/password strategy.

Do not commit credentials, tokens, `.netrc` files, or shell history snippets containing secrets to the repository.

Smoke-test authentication and a small CMR search before running the full workflow:

```bash
python - <<'PY'
import earthaccess

auth = earthaccess.login(strategy="all")
print("Authenticated:", auth.authenticated)

granules = earthaccess.search_data(
		short_name="SPL4SMGP",
		temporal=("2022-07-01", "2022-07-02"),
		count=1,
		cloud_hosted=True,
)
print("Granules found:", len(granules))
PY
```

Run the fast mocked and fixture-backed tests with:

```bash
pytest tests/test_earthaccess_stream_bdsnp_megan3.py -v
```

Run the live Earthdata tests with:

```bash
pytest tests/test_earthaccess_stream_bdsnp_megan3.py -v -m live_earthdata
```

The native standalone driver also uses the cloud extras when a config contains `source: earthaccess` streams. It invokes the helper at each timestep, then injects the returned arrays into the C++ import state before physics runs. You can define the helper path directly in YAML:

```yaml
driver:
	earthaccess_helper: "../scripts/cece_earthaccess_standalone_ingest.py"
```

Relative helper paths are resolved from the YAML configuration file's directory. An absolute path is also accepted. `CECE_EARTHACCESS_HELPER` takes precedence over the YAML setting, which is useful for deployment overrides. If neither is set, the driver searches the current directory and parent directories for `scripts/cece_earthaccess_standalone_ingest.py`. Set `CECE_PYTHON` if the `earthaccess` environment is not the default `python3`:

```bash
export CECE_PYTHON=/path/to/venv/bin/python
export CECE_EARTHACCESS_HELPER=/path/to/CECE/scripts/cece_earthaccess_standalone_ingest.py
./build/bin/cece_nuopc_driver examples/cece_config_earthaccess_megan3.yaml
```

Earthaccess variable mappings can also apply simple transforms before fields are injected into CECE. For example, MEGAN3 expects `solar_cosine` in the range `[0, 1]`, while some NASA products expose solar zenith angle in degrees. Use `transform: cos_degrees` to convert degrees to daylight cosine during injection:

```yaml
variables:
  solar_zenith_angle:
    model: solar_cosine
    transform: cos_degrees
```

Use `transform: cos_radians` for radian inputs. If `solar_cosine` is mapped without a transform, the bridge validates that the incoming values are already in `[0, 1]`.

The staged MEGAN3 example obtains direct and diffuse PAR from the MERRA-2 land-surface-forcing collection `M2T1NXLFO`, using its `PARDR` and `PARDF` fields. Because that collection does not include solar zenith angle, the helper derives daylight-clipped `solar_cosine` from the UTC model time and target-grid coordinates:

```yaml
variables:
	PARDR: par_direct
	PARDF: par_diffuse
derived_variables:
	solar_cosine: solar_cosine
```

The older `M2T1NXRAD` collection contains broadband fields such as `SWGDN`, but it does not contain `PARDR`, `PARDF`, or `SZA` in the downloaded Version 5.12.4 files.

Land-only products can contain NaN/fill cells over oceans or outside the modeled surface type. Configure replacements explicitly per variable instead of globally weakening validation:

```yaml
variables:
	LAI:
		model: leaf_area_index
		fill_value: 0.0
```

The helper replaces only non-finite values for mappings that define `fill_value`; all other fields still fail validation when they contain NaN or infinity. The examples use zero for missing LAI and soil moisture, representing no vegetation or available soil water, and `273.15 K` for missing layer-1 soil temperature where zero-moisture/zero-LAI fields suppress land emissions.

If the live test fails to authenticate, confirm that `~/.netrc` is mode `600` and that `python -c 'import earthaccess; print(earthaccess.login(strategy="all").authenticated)'` returns `True` in the active environment.

### Staging EarthAccess Streams for Compute Nodes Without Network Access

Some HPC systems, including current NOAA RDHPC Ursa compute nodes, allow Python package installation and Earthdata queries from login or data-transfer nodes but block outbound HTTPS from compute nodes. In that environment, install the cloud extras and fetch EarthAccess streams before the Slurm compute job starts, then run CECE against the staged cache.

Stage the EarthAccess streams on a login node:

```bash
cd /path/to/CECE
module use ./modulefiles
module load cece_ursa.intel

python -m venv .venv-ursa-earthaccess
source .venv-ursa-earthaccess/bin/activate
python -m pip install --upgrade pip setuptools wheel
python -m pip install -e '.[cloud,test]'

python scripts/stage_earthaccess_streams.py \
	--config examples/cece_config_earthaccess_megan3.yaml \
	--stage-dir /scratch/$USER/cece_earthaccess_stage \
	--overwrite
```

Check the EarthAccess search terms without staging files by adding `--preflight-only`. For non-interactive shells, set the authentication strategy explicitly so missing credentials fail instead of prompting:

```bash
python scripts/stage_earthaccess_streams.py \
	--config examples/cece_config_earthaccess_megan3.yaml \
	--stage-dir /scratch/$USER/cece_earthaccess_stage \
	--preflight-only \
	--check-download-access \
	--auth-strategy netrc
```

Before using the `netrc` strategy, run `unset EARTHDATA_TOKEN` so an expired bearer token cannot be reused by EarthAccess or inherited helper processes. The staging script also removes this variable automatically in `netrc` mode.

`--check-download-access` downloads one sample granule per stream into a temporary directory. This verifies protected-file authorization and checks that every configured variable exists in the downloaded NetCDF, in addition to validating CMR search results. If EarthAccess raises `EulaNotAccepted`, the error includes the exact protected data URL. Open that URL in a browser, sign in with the same Earthdata account stored in Ursa's `~/.netrc`, follow the redirect to authorize the GES DISC application, and accept any displayed terms. Confirm the application appears under Authorized Apps at <https://urs.earthdata.nasa.gov/profile>, then rerun preflight with `--auth-strategy netrc`. CECE cannot accept legal terms on behalf of an Earthdata account.

If authorization was completed with a different browser account, sign out of Earthdata Login and repeat the protected-URL flow with the username shown in `~/.netrc`. If the account password changed, update `~/.netrc`; if an old bearer token is exported, run `unset EARTHDATA_TOKEN` before retrying.

Use Earthdata Cloud provider IDs in `source: earthaccess` streams. For example, LP DAAC cloud-hosted MODIS collections should use `daac: LPCLOUD`, not the legacy archive provider `LPDAAC_ECS`.

For direct live streaming on a data-transfer node, `cloud_hosted: true` filters
the CMR search but does not force `earthaccess.open()` to use an `s3://` URL.
Outside the collection's AWS region, EarthAccess may open the advertised GES
DISC HTTPS URL instead. CECE retries transient HTTP 429/500/502/503/504 and
connection/timeout failures by reopening the complete lazy xarray dataset. The
defaults are four attempts with exponential delays of 2, 4, and 8 seconds. They
can be adjusted for an unreliable gateway without enabling local staging:

```bash
export CECE_EARTHACCESS_STREAM_ATTEMPTS=6
export CECE_EARTHACCESS_RETRY_DELAY_SECONDS=5
./build-ursa-earthaccess/cece_standalone_driver \
	examples/cece_config_earthaccess_megan3.yaml
```

Persistent 502 responses from `data.gesdisc.earthdata.nasa.gov` originate at
the NASA HTTPS gateway rather than Ursa's scratch filesystem. Verify the URL
with `curl -I` from the same data-transfer node; use the staged workflow below
if the gateway remains unavailable after bounded retries.

The current CECE EarthAccess helper opens granules through `xarray+h5netcdf`, so staged streams must resolve to NetCDF4/HDF5-readable data such as MERRA-2 `.nc4` granules. LP DAAC MODIS products such as `MCD15A2H` and `MCD12Q1` are commonly delivered as HDF-EOS `.hdf` granules; those require a separate HDF-EOS conversion path before CECE can consume them as local NetCDF/AMIO streams.

Run CECE on compute nodes with remote fetching disabled and the staged cache enabled:

```bash
export CECE_EARTHACCESS_STAGE_DIR=/scratch/$USER/cece_earthaccess_stage
unset EARTHDATA_USERNAME
unset EARTHDATA_TOKEN
srun -n 1 ./build-ursa-earthaccess/cece_standalone_driver examples/cece_config_earthaccess_megan3.yaml
```

For a global 0.1-degree output grid (`3600x1800`), one surface `float64` field is approximately 49.4 MiB. Configure output AMIO independently from input AMIO:

```yaml
output:
	amio_worker_threads: 2
	amio_staging_buffer_count: 2
	amio_staging_buffer_capacity_bytes: 67108864 # 64 MiB
	amio_staging_timeout_ms: 60000
```

The writer automatically raises buffer capacity when a field is larger than the configured minimum, up to AMIO's 1 GiB per-buffer limit. It waits for every asynchronous coordinate and field write before reusing staging capacity. Increasing `driver.amio_staging_buffer_count` affects input streams and does not tune output buffers.

The staged directory contains a shared `granules/` download cache used during staging, plus one subdirectory per model timestep named `step_0`, `step_1`, and so on. Each `step_N` directory contains only the timestep-specific `manifest.txt`, coordinate references (`target_lons.txt`, `target_lats.txt`), and raw `*.f64` 2D field arrays. The staging helper downloads protected Earthdata granules once into the shared `granules/` cache before opening them with xarray, which avoids duplicate downloads and remote fsspec streaming failures. When `CECE_EARTHACCESS_STAGE_DIR` is set, the native driver reads the staged `manifest.txt` and `*.f64` files directly from `step_N` and does not invoke the EarthAccess helper or open network connections.

For a complete Ursa example that combines login-node staging with a compute-node Slurm run, use:

```bash
bash scripts/ursa_earthaccess_staged_run.slurm
```

## Setting Up Examples

To run these examples, you need the associated NetCDF data files. CECE provides a script to automate the setup process.

### 1. Run the Setup Script
```bash
./scripts/setup_hemco_examples.sh
```
This script will:

-   Create the `examples/` directory if it doesn't exist.
-   Copy or generate the example configuration files.
-   Create a `scripts/data_download/` directory with shell scripts to download the required data from S3.

### 2. Download Data
Choose an example to run and execute its data download script:
```bash
# Example: Download data for Example 1
./scripts/data_download/download_ex1.sh
```
This will download the necessary NetCDF files into the `data/` directory.

### 3. Run the Example
You can use the standalone NUOPC driver to run any of the example configurations:
```bash
# Example: Run Example 1
./build/bin/cece_nuopc_driver --config examples/cece_config_ex1.yaml
```
The driver will perform the simulation steps and produce diagnostic output as configured in the YAML file.

---

## Visualizing Example Plans

To better understand the stacking hierarchy of an example, use the visualization utility:
```bash
python scripts/visualize_stack.py --config examples/cece_config_ex2.yaml --output ex2_stack.png
```
This will generate a graph showing how the different layers (global background and regional override) are prioritized and combined.
