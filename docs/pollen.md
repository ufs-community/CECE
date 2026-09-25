# Pollen Emissions

## Overview

The `pollen` scheme implements equations 1-9 from Li et al. (2025) as a native Kokkos surface-emission parameterization. It converts taxon-specific vegetation, RF annual production, phenology, and hourly meteorology into inert number and mass fluxes suitable for coarse-particle CTM tracers.

Reference: Li, J. et al. (2025), *Construction and application of a pollen emissions model based on phenology and random forests*, Atmos. Chem. Phys., 25, 3583-3602, https://doi.org/10.5194/acp-25-3583-2025.

Taxon aliases allow CECE to schedule several independently mapped pollen classes. Every alias uses the same configurable physics; it does not imply that a global RF climatology or validated particle properties are bundled for that taxon.

## Available Pollen Taxa

CECE provides these registration names:

| CECE scheme | Taxon or role | Broad provider category | Notes |
| --- | --- | --- | --- |
| `pollen` | Custom taxon | Any | Generic registration for one user-defined taxon |
| `pollen_mugwort` | Mugwort (*Artemisia*) | Weed | Preferred explicit name |
| `pollen_artemisia` | Mugwort (*Artemisia*) | Weed | Synonym alias; do not run with `pollen_mugwort` for the same pool |
| `pollen_chenopod` | Chenopod | Weed | Region-dependent provider coverage |
| `pollen_ragweed` | Ragweed | Weed | Common in Europe and North America |
| `pollen_nettle` | Nettle | Weed | Primarily European provider coverage |
| `pollen_grass` | Grasses | Grass | Widest global provider coverage |
| `pollen_alder` | Alder | Tree | Region-dependent |
| `pollen_ash` | Ash | Tree | Region-dependent |
| `pollen_birch` | Birch | Tree | Region-dependent |
| `pollen_cottonwood` | Cottonwood / poplar | Tree | Provider names may differ |
| `pollen_cypress` | Cypress / yew / cedar group | Tree | Provider grouping varies geographically |
| `pollen_elm` | Elm | Tree | Region-dependent |
| `pollen_hazel` | Hazel | Tree | Primarily European provider coverage |
| `pollen_juniper` | Juniper / cedar group | Tree | Common North American grouping |
| `pollen_maple` | Maple | Tree | Primarily North American provider coverage |
| `pollen_oak` | Oak | Tree | Region-dependent |
| `pollen_olive` | Olive | Tree | Mediterranean and selected regions |
| `pollen_pine` | Pine | Tree | Region-dependent |
| `pollen_plane` | Plane | Tree | Primarily European/Mediterranean coverage |
| `pollen_total` | Aggregate total pollen | Aggregate | Diagnostic/aggregate pool; do not sum with its component taxa |

The generic `pollen` scheme can also model provider-observed taxa without built-in aliases, including acacia, daisy, dock, Japanese cedar, Japanese cypress, mulberry, myrtle, plantain, sedges, she-oak, and willow. Give each concurrently scheduled custom taxon a unique registered alias in `cece_pollen.cpp`; duplicate uses of the same registration name are not independently scheduled by CECE's current clock.

Taxon availability is not globally uniform. Ambee documents regional species groups and broad type-level coverage elsewhere; Google documents country-specific species coverage but exposes only a short forecast, not a historical 2025 training archive. Always record provider, region, taxon naming, units, and observation period in RF provenance. Never treat an unavailable species as zero pollen.

## Modeling Workflow

1. Train the offline RF against station/year pollen production and meteorological predictors. `tools/train_pollen_rf.py` writes the gridded `annual_pollen_production` field used by CECE.
2. Supply gridded vegetation fraction, RF production, and start/end DOY. Start/end dates may be generated with the paper's `Rs1`, `Rs2`, or `Rssig` autumn forcing and cumulative threshold.
3. CECE computes the Gaussian daily pollen pool and applies hourly wind, precipitation, RH, and temperature release controls.
4. Map each number/mass pair to inert, non-reactive CTM tracers. Diameter and density outputs preserve settling and coarse-mode metadata needed by CMAQ-like consumers.

## Equations

For taxon $i$, the daily potential is

$$E_i(t)=f_i P_{annual,i}\exp\left[-\frac{(t-\mu)^2}{2\delta^2}\right],\quad
\mu=\frac{sDOY+eDOY}{2},\quad \delta=\frac{eDOY-sDOY}{a}.$$

The default is $a=4$. The three autumn forcing options are

$$R_{s1}=(T_{base}-T_i)^x\left(\frac{L_i}{L_{base}}\right)^y,$$

$$R_{s2}=(T_{base}-T_i)^x\left(1-\frac{L_i}{L_{base}}\right)^y,$$

when $T_i<T_{base}$ and $L_i<L_{base}$, and zero otherwise, and

$$R_{ssig}=\frac{1}{1+\exp(aT_iL_i-b)}.$$

The autumn trigger occurs on the first $t_n$ satisfying $\sum_{t=t_0}^{t_n}R_s(t)\ge Y$. CECE can enforce this with `use_autumn_trigger` and the `autumn_accumulated_forcing` input.

Hourly mobilization follows

$$E_{pollen,i}=E_i f_w f_r f_h,$$

where $f_w=1.5-\exp[-(u_{10}+u_{conv})/5]$. Rain and RH factors are one below their low threshold, zero above their high threshold, and decrease linearly between them. Precipitation is the accumulation in millimeters over one physics execution interval; configure `precipitation_low_mm_interval` and `precipitation_high_mm_interval` when that interval differs from the paper's hourly application. CECE additionally applies a configurable logistic temperature dehiscence factor; set `temperature_slope: 0` for a constant factor or use a small threshold to effectively disable temperature gating.

The daily number potential is divided by 86400 to produce `grains m-2 s-1`. Mass flux assumes spherical grains:

$$m_{grain}=\frac{\pi}{6}d^3\rho.$$

## Fields

Required inputs are `day_of_year`, `annual_pollen_production`, `wind_speed`, `convective_velocity`, `precipitation`, `relative_humidity`, `temperature`, and `sunshine_hours`. Supply either a spatial `vegetation_fraction` input or `vegetation_fraction_default`; the configured default is used only when the field is absent. Optional spatial inputs `season_start_doy` and `season_end_doy` override configured constants. `autumn_accumulated_forcing` is required only when the autumn trigger is enabled.

Required outputs are `pollen_number_emissions` and `pollen_mass_emissions`. Optional outputs are `pollen_diameter`, `pollen_density`, and `pollen_phenology_forcing`. All emissions are written only to the surface layer.

Use `input_mapping` and `output_mapping` to bind taxon-specific names. See `examples/cece_config_pollen.yaml` for Artemisia, chenopod, and total-pollen mappings and CTM metadata.

### Vegetation Fraction Semantics

The pollen-pool equation multiplies annual production by the taxon's vegetation fraction:

$$E_i(t)=f_i P_{annual,i}\exp\left[-\frac{(t-\mu)^2}{2\delta^2}\right].$$

`vegetation_fraction_default: 1.0` sets $f_i=1$ only when no `vegetation_fraction` input field is mapped or available. It means "do not apply an additional vegetation scaling," not "the entire grid cell is covered by this plant."

Use this setting when `annual_pollen_production` already represents production per total grid-cell area and has already incorporated the taxon's land-cover or plant-functional-type fraction:

```yaml
options:
  vegetation_fraction_default: 1.0
  input_mapping:
    annual_pollen_production: RF_MUGWORT_PANNUAL
```

If `annual_pollen_production` instead represents production per unit vegetated area, map a dimensionless grid-cell vegetation fraction in `[0, 1]`:

```yaml
options:
  input_mapping:
    annual_pollen_production: MUGWORT_PRODUCTION_PER_VEGETATED_AREA
    vegetation_fraction: MUGWORT_GRIDCELL_FRACTION
```

For example, a value of `0.25` applies pollen production to 25% of the grid cell. A mapped field takes precedence over `vegetation_fraction_default`.

Do not apply vegetation fraction twice. If the RF target or predictor preprocessing already multiplied production by land-cover fraction, also mapping that fraction in CECE would underestimate the pool by another factor of $f_i$. Conversely, using `vegetation_fraction_default: 1.0` with production defined per vegetated area treats the production as grid-cell-wide and overestimates emissions. Record whether each RF product is `per_grid_cell_area` or `per_vegetated_area` in its NetCDF metadata and training provenance.

## RF Mapping Tool

Install the optional dependencies and generate a grid:

```bash
python -m pip install -e '.[pollen]'
python tools/train_pollen_rf.py observations.csv meteorology_grid.nc artemisia_rf.nc \
  --taxon artemisia --model-output artemisia_rf.joblib
```

The training table and predictor NetCDF must contain the selected feature names. Defaults include temperature, wind, precipitation, RH, sunshine, altitude, and pressure. The tool uses a 4:1 train/test split and cross-validated RF hyperparameter search, matching the paper's workflow.

### Global 2025 Climatology

`tools/prepare_global_pollen_rf.py` prepares a global annual predictor grid from MERRA-2 and joins pollen observations to that grid. NASA Earthdata authentication is required. Configure Earthdata credentials outside CECE using Earthaccess (`~/.netrc`, environment-based login, or its interactive login); do not put credentials in YAML or command history.

```bash
python -m pip install -e '.[pollen]'

python tools/prepare_global_pollen_rf.py download-merra2 \
  --year 2025 \
  --output-dir data/pollen/merra2/2025

python tools/prepare_global_pollen_rf.py aggregate-merra2 \
  --year 2025 \
  --surface-glob 'data/pollen/merra2/2025/MERRA2_*tavg1_2d_slv_Nx*.nc4' \
  --flux-glob 'data/pollen/merra2/2025/MERRA2_*tavg1_2d_flx_Nx*.nc4' \
  --radiation-glob 'data/pollen/merra2/2025/MERRA2_*tavg1_2d_rad_Nx*.nc4' \
  --constant-glob 'data/pollen/merra2/2025/MERRA2_*const_2d_asm_Nx*.nc4' \
  --output data/pollen/merra2_annual_predictors_2025.nc
```

Obtain an authorized 2025 historical pollen-count export from a provider such as Ambee. Normalize its columns to `site_id`, `latitude`, `longitude`, `timestamp`, `taxon`, and `pollen_count`, or pass the corresponding `--*-column` options. Ambee API keys must remain in the provider client or environment and must never be committed.

For station-based API retrieval, prepare a sites CSV with `site_id`, `latitude`, and `longitude` columns (`data/pollen/pollen_sites_global.csv` ships a reference set of major cities across every continent for global training coverage), and set `AMBEE_API_KEY` directly in the shell:

```bash
export AMBEE_API_KEY="your-key-here"
```

The public endpoint documentation does not guarantee one universal species-count JSON layout, so `--records-path`, `--timestamp-path`, and `--count-path` are deliberately explicit. Discover the real layout for your account and site before running a full year, since Ambee's free trial only returns the past 2 days through `v3/pollen/history` (a longer range returns `HTTP 400 "Only past 2 days data available!"`; email `contactus@getambee.com` for full-year/bulk access):

```bash
curl -sS -G "https://api.ambeedata.com/v3/pollen/history" \
  -H "x-api-key: $AMBEE_API_KEY" \
  --data-urlencode "lat=39.7392" \
  --data-urlencode "lng=-104.9903" \
  --data-urlencode "from=$(date -u -d '2 days ago' '+%Y-%m-%d %H:%M:%S')" \
  --data-urlencode "to=$(date -u '+%Y-%m-%d %H:%M:%S')" \
  --data-urlencode "speciesRisk=true" | python -m json.tool
```

Note `-G` (not `-X GET`): `--data-urlencode` normally builds a POST body, and `-G` moves those values into the query string so the request is a genuine GET.

A typical response nests per-record group counts under `Count` and per-species counts under `Species`, with the record timestamp in `timestamp` (not the tool's default `updatedAt` — override it):

```json
{
  "data": [
    {
      "Count": {"grass_pollen": 0, "tree_pollen": 35, "weed_pollen": 35},
      "Species": {
        "Grass": {"Grass": 0},
        "Tree": {"Ash": 0, "Birch": 0, "Elm": 35, "Oak": 0, "Pine": 0},
        "Weed": {"Ragweed": 35}
      },
      "timestamp": "2026-09-23T20:00:00.000Z"
    }
  ]
}
```

Only request taxa your own site's response actually contains; species coverage is region-dependent and an absent field must never be treated as zero. Typical mappings from this layout to CECE taxon aliases:

| CECE taxon | `--taxon` | `--count-path` |
|---|---|---|
| Grass | `grass` | `Species.Grass.Grass` |
| Ash | `ash` | `Species.Tree.Ash` |
| Birch | `birch` | `Species.Tree.Birch` |
| Cypress/juniper/cedar group | `cypress` | `Species.Tree.Cypress/Juniper/Cedar` |
| Elm | `elm` | `Species.Tree.Elm` |
| Maple | `maple` | `Species.Tree.Maple` |
| Oak | `oak` | `Species.Tree.Oak` |
| Pine | `pine` | `Species.Tree.Pine` |
| Cottonwood/poplar | `cottonwood` | `Species.Tree.Poplar/Cottonwood` |
| Ragweed | `ragweed` | `Species.Weed.Ragweed` |
| Total (aggregate) | `total` | `Count.grass_pollen+Count.tree_pollen+Count.weed_pollen` |

Ambee groups juniper and cedar into `Cypress/Juniper/Cedar`; don't also run a separate `pollen_juniper` extraction from that same combined value, or the two taxa would double-count the same pollen. There is no single `pollen_total` field in Ambee's response, so `--count-path` accepts `+`-joined dot paths and sums them; use this only for genuine aggregates such as the total across grass, tree, and weed groups.

```bash
python tools/prepare_global_pollen_rf.py download-ambee \
  data/pollen/pollen_sites_global.csv \
  data/pollen/ambee_pollen_history_2025_elm.csv \
  --year 2025 \
  --taxon elm \
  --records-path data \
  --timestamp-path timestamp \
  --count-path Species.Tree.Elm

python tools/prepare_global_pollen_rf.py download-ambee \
  data/pollen/pollen_sites_global.csv \
  data/pollen/ambee_pollen_history_2025_total.csv \
  --year 2025 \
  --taxon total \
  --records-path data \
  --timestamp-path timestamp \
  --count-path "Count.grass_pollen+Count.tree_pollen+Count.weed_pollen"
```

Mugwort/Artemisia, chenopod, nettle, alder, hazel, olive, and plane did not appear at all in the sample response above; confirm each of those fields actually exists in your own site's payload (run the discovery `curl` per site/region) before assuming Ambee reports them there.

Airborne pollen concentration is affected by transport and removal and is not identical to source production. Therefore, preparation requires a positive `--concentration-to-production` factor calibrated against source measurements or an inverse transport model. This prevents concentration or pollen-index values from being silently labeled as `grains m-2 yr-1`.

The preparer infers each site's reporting cadence, integrates concentration in concentration-days, and defaults to retaining only sites with at least 75% annual temporal coverage. Adjust `--minimum-coverage-fraction` only when the observation product has a documented seasonal sampling design.

```bash
python tools/prepare_global_pollen_rf.py prepare-training \
  data/pollen/ambee_pollen_history_2025_mugwort.csv \
  data/pollen/merra2_annual_predictors_2025.nc \
  data/pollen/mugwort_training_2025.csv \
  --year 2025 \
  --taxon mugwort \
  --concentration-to-production CALIBRATED_FACTOR

python tools/train_pollen_rf.py \
  data/pollen/mugwort_training_2025.csv \
  data/pollen/merra2_annual_predictors_2025.nc \
  data/pollen/annual_pollen_production_mugwort_2025.nc \
  --taxon mugwort \
  --year 2025 \
  --training-source 'Ambee historical pollen export, 2025' \
  --meteorology-source 'NASA MERRA-2 M2T1NXSLV, M2T1NXFLX, M2T1NXRAD, and M2C0NXASM' \
  --model-output data/pollen/mugwort_rf_2025.joblib
```

This assumes your Ambee response actually contains a mugwort/Artemisia field (verified with the discovery `curl` above); the sample payload shown earlier only had elm, ragweed, and grass, so substitute whichever taxa your own account and sites return.

Repeat the preparation and training stages for each supported taxon. The Google Maps Pollen API is not suitable for reconstructing calendar year 2025: it provides a rolling forecast of up to five days and a Universal Pollen Index rather than a historical concentration archive. Its values must not be used as annual production observations.

Map each generated climatology directly into the corresponding CECE pollen scheme. For example:

```yaml
cece_data:
  streams:
    - name: MUGWORT_RF_CLIMATOLOGY_2025
      file: "data/pollen/annual_pollen_production_mugwort_2025.nc"
      yearFirst: 2025
      yearLast: 2025
      yearAlign: 2025
      taxmode: extend
      tintalgo: nearest
      mapalgo: bilinear
      variables:
        - file: annual_pollen_production
          model: RF_MUGWORT_PANNUAL

physics_schemes:
  - name: pollen_artemisia
    options:
      input_mapping:
        annual_pollen_production: RF_MUGWORT_PANNUAL
```

The climatology provides the annual pool only. Reanalysis, analysis, or forecast meteorology must separately populate the online fields mapped to `temperature`, `wind_speed`, `convective_velocity`, `precipitation`, and `relative_humidity` at each CECE physics interval.
The RF writer stores that pool on a singleton January 1 time coordinate; use `taxmode: extend` and `tintalgo: nearest` so CECE holds it constant throughout the target year.

### Earthaccess-Driven CECE Example

`examples/cece_config_earthaccess_pollen.yaml` combines the generated 2025 mugwort RF climatology with MERRA-2 meteorology streamed by the native standalone driver's Earthaccess helper. `M2T1NXSLV` supplies temperature, pressure, and specific humidity; CECE derives RH, fractional DOY, and astronomical daylight duration. `M2T1NXFLX` supplies wind, friction velocity, and corrected precipitation, with precipitation converted from `mm s-1` to accumulation over the one-hour physics interval.

The example maps MERRA-2 `USTAR` to `convective_velocity` as an explicitly documented turbulence proxy. For production coupling, map a diagnosed convective velocity scale from the driving forecast model when available.

```bash
python -m pip install -e '.[cloud,pollen]'
export EARTHDATA_TOKEN=<NASA-Earthdata-token>
./build/cece_standalone_driver examples/cece_config_earthaccess_pollen.yaml
```

The example expects `data/pollen/annual_pollen_production_mugwort_2025.nc`, generated by the global 2025 workflow above. Earthdata credentials can alternatively be supplied through `~/.netrc`; see `docs/examples.md` for live and staged Earthaccess operation.

`examples/cece_config_earthaccess_pollen_total.yaml` runs only the aggregate `pollen_total` scheme, matching the summed `--count-path "Count.grass_pollen+Count.tree_pollen+Count.weed_pollen"` Ambee export shown above. It expects `data/pollen/annual_pollen_production_total_2025.nc`. Don't run this alongside per-species schemes (`pollen_grass`, `pollen_ragweed`, `pollen_birch`, etc.) trained from the same Ambee export in the same simulation, since their emissions are already folded into the total and would otherwise be double counted.

`examples/cece_config_earthaccess_pollen_all_taxa.yaml` demonstrates every distinct built-in taxon alias plus `pollen_total`. It expects `data/pollen/annual_pollen_production_all_taxa_2025.nc` containing `{taxon}_pannual`, `{taxon}_sdoy`, and `{taxon}_edoy` variables. `pollen_artemisia` is omitted because it is synonymous with the included `pollen_mugwort` pool. Remove any taxon not actually available from your observation provider at your sites — for example, Ambee reports juniper and cedar merged into `pollen_cypress`'s `Species.Tree.Cypress/Juniper/Cedar` field with no separate juniper count, so don't train and instantiate both `pollen_cypress` and `pollen_juniper` from that same Ambee export.

After training each taxon separately, consolidate the products without changing their coordinates:

```python
import xarray as xr

taxa = [
  "mugwort", "chenopod", "ragweed", "grass", "alder", "ash", "birch",
  "cottonwood", "cypress", "elm", "hazel", "juniper", "maple", "oak",
  "olive", "pine", "plane", "nettle", "total",
]
datasets = []
for taxon in taxa:
  production = xr.open_dataset(
    f"data/pollen/annual_pollen_production_{taxon}_2025.nc"
  ).rename({"annual_pollen_production": f"{taxon}_pannual"})
  phenology = xr.open_dataset(f"data/pollen/phenology_{taxon}_2025.nc").rename(
    {"season_start_doy": f"{taxon}_sdoy", "season_end_doy": f"{taxon}_edoy"}
  )
  datasets.extend([production, phenology])

xr.merge(datasets, compat="no_conflicts").to_netcdf(
  "data/pollen/annual_pollen_production_all_taxa_2025.nc"
)
```

Train and include only taxa supported by the observation product in each region. Remove unsupported schemes and fields from the all-taxa example rather than filling missing climatologies with zero.

To run the bundled configuration without observational training data, generate its explicitly synthetic demonstration stream:

```bash
python tools/generate_pollen_example_stream.py
```

This writes `data/pollen/pollen_rf_phenology_2020.nc`, the path used by `examples/cece_config_pollen.yaml`. Replace this demonstration file with RF, phenology, land-cover, and meteorological products for scientific simulations.
