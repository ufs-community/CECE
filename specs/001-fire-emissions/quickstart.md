# Fire Feature Validation Guide

**Status**: Prepared bulk-product runtime loading is wired, but production
acceptance remains pending. The built-in prepared adapter consumes normalized
RAVE v2r0/GBBEPx v4r0 intervals. Invariant Phase 3 product mappings do not
require biome/composition profiles; composition-specific mappings and future
FRP-derived emissions do. Runtime target grids currently require uniformly spaced rectilinear
longitude/latitude centers. Coupled-provider, target-specific product, remote
acquisition and production-science workflows remain unsupported.

The current native fire selectors and Python configuration tests exercise
synthetic fixture consistency, option/identity/grid/inventory validation, bounded
prepared/coupled provider contracts, content hashing, conservative allocation and
test-helper device round trips. Product tests cover the pinned NOAA sample decoders,
normalized NetCDF writing and a synthetic prepared-runtime integration, including
profile allocation and component identity. No test exercises host-field bindings
or production chemistry; these results are not scientific acceptance.

## Prerequisites

1. Build with supported CECE dependencies and existing tests.
2. Use an isolated Python environment for optional preparation; do not modify Spack NumPy.
3. Obtain permitted, versioned NOAA samples and matching scientific ancillaries.
4. Install independently verified profiles; full acceptance needs AM4, CB6, CRACMM2, RACM2 and GOCART2G.
5. Use the preflighted Phase 3 product examples; FRP examples remain pending.

See [configuration](./contracts/configuration.md) and [input contracts](./contracts/fire-inputs.md).

## 1. Targeted Synthetic Tests

From the repository root, after implementation:

```bash
cmake -S . -B build-fire \
  -DCMAKE_BUILD_TYPE=Release \
  -DCECE_BUILD_TESTING=ON \
  -DKokkos_ENABLE_SERIAL=ON
cmake --build build-fire --target cece_standalone_driver cece_fire_product_normalize \
  test_fire_config test_fire_science test_fire_speciation \
  test_fire_products test_fire_runtime test_fire_conservation -j 8
ctest --test-dir build-fire -N -R 'test_fire_'
ctest --test-dir build-fire --output-on-failure -R 'test_fire_'
python -m pytest -q tests/test_prepare_fire_inputs.py
```

Confirm discovery lists tests; an empty selection is not success. Expect mode/schema validation, dimensional calculations, required-field errors and conservative budgets to pass offline.

## 2. Product Mode

Four product configurations are generated from the pinned samples and CB6
factor manifest. Choose either the native-product fields or CO/PM25 factor
profile; they are alternatives, not additive. The factor profile applies
independent coefficients without normalization. Both modes exclude overlapping
source species explicitly. The native fields are not CB6-speciated; factor
outputs remain nonproduction pending rights, independent budgets and mechanism
compatibility. The examples use one surface level and declare `kg m-2 s-1`.

Preflight the hourly RAVE factor example from the repository root:

```bash
python scripts/prepare_fire_inputs.py \
  --config examples/cece_config_fire_rave_cb6_speciation.yaml \
  --output-dir examples/fire_inputs/rave \
  --preflight-only
```

The other choices are `cece_config_fire_rave_cb6_native.yaml`,
`cece_config_fire_gbbepx_cb6_native.yaml` and
`cece_config_fire_gbbepx_cb6_speciation.yaml`. Each points at its matching raw
source manifest and generated normalized `input_manifest`. Preflight validates
the raw manifest and pinned sample hash without requiring normalized outputs.
Build and select the normalizer before preparing a run:

```bash
cmake --build build-fire --target cece_fire_product_normalize cece_standalone_driver
CECE_FIRE_PRODUCT_NORMALIZER=build-fire/cece_fire_product_normalize \
  python scripts/prepare_fire_inputs.py \
    --config examples/cece_config_fire_rave_cb6_speciation.yaml \
    --output-dir examples/fire_inputs/rave
./build-fire/cece_standalone_driver examples/cece_config_fire_rave_cb6_speciation.yaml
```

The current writer supports local bulk RAVE v2r0 and GBBEPx v4r0 files, produces
one checksummed CF-style NetCDF file per source interval, and commits the
normalized manifest only after every interval succeeds. Prepared runtime reads
that manifest and conservatively remaps intervals without inferring
composition. Composition-specific mappings still fail when identities are
missing. Remote acquisition, target-specific inputs, coupled loading, and
curvilinear or unstructured target grids are not implemented.

Exercise `uniform` and `source_resolved` against supplied finer intervals: each must preserve those interval totals. Exercise normalized `profile` weighting on a compatible coarse product and reject repeated/incompatible weighting. Mixed-biome chemistry cases must preserve component masses/profile identities and agree between the two component-preserving allocation orders, not silently use target-cell dominant-biome factors.

## 3. CECE-Derived Mode

Configure raw sensor samples through `source_manifest`, runtime `input_manifest` at `fire_inputs/frp/manifest.yaml`, verified science assets and explicit cutoff/age policy:

```bash
python scripts/prepare_fire_inputs.py \
  --config examples/cece_config_fire_frp.yaml \
  --output-dir fire_inputs/frp
./build-fire/cece_standalone_driver examples/cece_config_fire_frp.yaml
```

Preparation must not calculate emissions. Expect CECE sensor reconciliation, coverage/reconstruction flags, FRE, dry matter and eleven pre-mapping species budgets. Compare synthetic values first, then NOAA fixtures with predeclared per-variable tolerances.

Repeat with new-after-cutoff observations, missing ecoregions, cloudy/duplicate/outside-footprint cells: explicit rejection or selected flagged reconstruction is required.

Repeat with `cutoff_mode: host_supplied`: preparation/prepared runtime must use the same interval cutoff schedule, and coupled host metadata must match it for parity. Missing/duplicate entries, backward forward-run cutoffs, host/schedule disagreements and excessive cutoff-minus-acquisition ages fail. Causal policies reject post-interval acquisitions even when available by cutoff; an unverified causal policy fails initialization rather than selecting a default formula.

## 4. Five-Target and Coupled Acceptance

- Run identical bulk inputs for all five targets, checking species/units, NOx/VOC basis, declared budgets/exclusions and particulate overlap.
- Feed equivalent intervals through standalone and existing NUOPC tests; compare budgets/exports at 1e-6 relative tolerance for nonzero values.
- Repeat one/multiple MPI ranks and check conservative global sums and local-band memory.
- Existing non-fire tests retain established tolerances.

## 5. Offline and High-Resolution Ursa Acceptance

Prepare on an approved connected host. The proposed staged script loads supported Intel LLVM modules and reports actual allocation before running.

After implementation:

```bash
sbatch --ntasks=1 --cpus-per-task=8 --mem=32G \
  scripts/ursa_fire_staged_run.slurm
```

32G is a starting measurement allocation, not a requirement guarantee. Log job ID, host, Slurm CPU/memory variables and `scontrol show job "$SLURM_JOB_ID"` inside the job. Running with Bash does not honor resource directives.

Replace the example numeric ID with the actual submission ID:

```bash
scontrol show job 12345678
sacct -j 12345678 --units=G \
  --format=JobID,State,ExitCode,AllocCPUS,ReqMem,MaxRSS,NodeList
```

Run complete staged inputs with networking disabled: no remote calls, and results equivalent to identical accepted inputs in connected execution.

For memory, use the same 3600x1800 surface grid, profiles, ranks, diagnostics, temporal-window capacity and warm-up. Measure separate 12/24-interval runs; require peak ratio <=1.2, reporting absolute memory and elapsed time. Increase explicit memory based on actual allocation/use; adding MPI ranks is not an assumed fix.

## Acceptance Evidence

Record source/profile/reference hashes, cutoff, product versions, test discovery/results, energy/species budgets, coupled/standalone comparisons, offline evidence and effective Slurm allocation/peak memory. Scientific approval requires all spec success criteria, not just compilation or absence of Python errors.
