# Fire emissions: product support and limits

The prepared bulk-product path is wired for local RAVE v2r0 and GBBEPx v4r0
inputs: decoders validate source metadata, preparation writes normalized
interval files, and the prepared runtime verifies manifests, source hashes,
cutoffs, profile identities, and conservative spatial/temporal allocation.
Existing fire-free runtime behavior is unchanged. This is not a production
chemistry certification or an operational Ursa recipe.

The product decoder and preparation support are distinct from chemistry
approval. The CB6 factor rows are verified against the pinned upstream excerpt
and confirmed by the user, but the asset registry still marks CB6 production
use false: redistribution permission is unverified and independent target
budgets remain pending. Passing parser or sample-conservation tests does not
remove that gate. RAVE FRP/FRE diagnostics are retained as diagnostics; they do
not activate an FRP-to-emissions conversion.

## RAVE derivation references

The proposed derivation profile is `rave-atbd-2.1-unscaled`; it is not yet
implemented. The [RAVE ATBD v2.1](https://www.ospo.noaa.gov/resources/documents/pdf/RAVE_ATBD.pdf)
describes 10-minute FRP reconstruction and hourly integration, even though its
diurnal climatologies are built in five-minute bins. The earlier
[Li et al. (2022)](https://repository.library.noaa.gov/view/noaa/67902/noaa_67902_DS1.pdf)
study describes five-minute reconstruction, different long-gap burn durations
and sensor-pair timing, and eight species rather than eleven. They are not
interchangeable profiles. The ATBD's eleven-row factor table has an
eight-species caption, so deployed coefficients still require verification.

Both references describe five vegetation groups derived from 2019 MODIS
land cover and North American EPA Level I ecoregions. The user selected
VIIRS AST IGBP17 as a land-cover substitute, not an established equivalent:
its crosswalk, aggregation and compatibility with the reconstruction assets
must be validated. AST's surface-type climatology is not a fire-radiative-power
diurnal climatology or an ecoregion map; compatible FRP/burn-duration tables
and ecoregions are still needed. The
[RAVE manifest](../data/fire/rave_atbd_2_1_manifest.yaml) records document
hashes, differences and remaining gates. No raw-sensor RAVE or GBBEPx
derivation is enabled by these references.

The standalone VIIRS IGBP17-to-RAVE biome crosswalk maps classes 1-5 to
forest, 6-7 to shrubland, 8-9 to savanna, 10-11 to grassland and 12/14 to
cropland. It rejects fill, water and classes without a documented five-biome
mapping rather than treating them as zero fire. It is not connected to input
preparation or runtime, and its aggregation and compatibility with deployed
RAVE climatology remain unverified. In the inspected VIIRS file, 0 is fill
and 17 is water; a 20-class dataset must not be interpreted as IGBP17.

Four Phase 3 demonstration configurations are generated from the factor
manifest by [`scripts/generate_fire_cb6_examples.py`](../scripts/generate_fire_cb6_examples.py):

| Product | Native product species (direct, non-CB6-speciated) | CO/PM2.5 factor profile |
|---|---|---|
| Hourly RAVE | [`cece_config_fire_rave_cb6_native.yaml`](../examples/cece_config_fire_rave_cb6_native.yaml) | [`cece_config_fire_rave_cb6_speciation.yaml`](../examples/cece_config_fire_rave_cb6_speciation.yaml) |
| Daily GBBEPx | [`cece_config_fire_gbbepx_cb6_native.yaml`](../examples/cece_config_fire_gbbepx_cb6_native.yaml) | [`cece_config_fire_gbbepx_cb6_speciation.yaml`](../examples/cece_config_fire_gbbepx_cb6_speciation.yaml) |

The native profile passes selected product fields through unchanged and
excludes PM25, TPM, NOX and VOC. The factor profile requests only CO and PM25,
maps them with the 43 manifest coefficients, and excludes all other product
species. Choose one profile/configuration per run; do not combine their outputs.
The native outputs retain product-species names and are not CB6 mechanism-ready.
The factor outputs are exact mass-basis factor targets; they are not a complete
or production-certified CB6 mechanism. Redistribution rights, independent
budgets and deployed-mechanism compatibility remain unresolved.

All four configs use a 72x46 CONUS grid, `nz: 1`, surface placement and
explicit `kg m-2 s-1` exports. The physical longitude extent is -125 to -66
degrees, represented as 235 to 294 degrees to match the products' 0-to-360
longitude coordinates. They select source-resolved allocation, fixed UTC
cutoffs and unscaled inputs. Runtime target-cell areas use AXIS's great-circle
edge convention; RAVE curvilinear source areas use the same polygon method,
while GBBEPx source areas retain the regular 0.1-degree product bounds. Run
preflight from the repository root:

```bash
python scripts/prepare_fire_inputs.py \
  --config examples/cece_config_fire_rave_cb6_speciation.yaml \
  --output-dir examples/fire_inputs/rave --preflight-only
```

Equivalent config/source-manifest pairs are provided for RAVE native, GBBEPx
native and GBBEPx factor mode. Preflight checks their actual sample hashes and
metadata declarations; it does not normalize or run CECE. From the repository
root, build the normalizer and driver, normalize once per product, then run
each native or factor configuration separately:

```bash
cmake --build build --target cece_fire_product_normalize cece_standalone_driver
export CECE_FIRE_PRODUCT_NORMALIZER=build/cece_fire_product_normalize
python scripts/prepare_fire_inputs.py \
  --config examples/cece_config_fire_rave_cb6_speciation.yaml \
  --output-dir examples/fire_inputs/rave
python scripts/prepare_fire_inputs.py \
  --config examples/cece_config_fire_gbbepx_cb6_speciation.yaml \
  --output-dir examples/fire_inputs/gbbepx
./build/cece_standalone_driver examples/cece_config_fire_rave_cb6_native.yaml
./build/cece_standalone_driver examples/cece_config_fire_rave_cb6_speciation.yaml
./build/cece_standalone_driver examples/cece_config_fire_gbbepx_cb6_native.yaml
./build/cece_standalone_driver examples/cece_config_fire_gbbepx_cb6_speciation.yaml
```

CECE driver clock values use `YYYY-MM-DDTHH:MM:SS` without a timezone suffix;
the preparation helper interprets these driver values as UTC. Product and
source-manifest timestamps remain explicitly UTC with a trailing `Z`.

Both chemistry choices for a product reuse its normalized input manifest; they
are alternatives and must not be added together.

## Proposed GOCART-2G Stage 1 product examples

The nonproduction [Stage 1 profile](../data/fire/ufschem_gocart2g_stage1_profile.yaml)
uses the proposed BC 80/20 and OC 50/50 splits and passes SO2 and NH3 through.
It excludes PM2.5 and TPM to avoid adding overlapping particulate pools. The
OC-to-BRC mass basis, factors, tracer definitions, rights and independent
budgets have not been verified against a deployed UFS-Chem mechanism. These
examples are not operational or production-certified.

The following configurations use existing gridded product emissions, not
FRP-derived emissions. They require the full local files and hashes identified
in the [product sample manifest](../data/fire/product_samples_manifest.yaml);
the GBBEPx sample is approximately 526 MB. Configurations:
[RAVE](../examples/cece_config_fire_rave_gocart2g_stage1.yaml) and
[GBBEPx](../examples/cece_config_fire_gbbepx_gocart2g_stage1.yaml).
From the repository root:

```bash
cmake --build build --target cece_fire_product_normalize cece_standalone_driver
export CECE_FIRE_PRODUCT_NORMALIZER=build/cece_fire_product_normalize
python scripts/prepare_fire_inputs.py \
  --config examples/cece_config_fire_rave_gocart2g_stage1.yaml \
  --output-dir examples/fire_inputs/rave_gocart2g_stage1
python scripts/prepare_fire_inputs.py \
  --config examples/cece_config_fire_gbbepx_gocart2g_stage1.yaml \
  --output-dir examples/fire_inputs/gbbepx_gocart2g_stage1
./build/cece_standalone_driver examples/cece_config_fire_rave_gocart2g_stage1.yaml
./build/cece_standalone_driver examples/cece_config_fire_gbbepx_gocart2g_stage1.yaml
```

Each outputs six surface fluxes in `kg m-2 s-1` on the same demonstration
grid as the CB6 examples. The prepared manifests have target identities and
must be regenerated for these configurations, rather than reusing the CB6
manifests. These are standalone demonstrations, not FV3 tile or CATChem
two-stage output.

AM4.1 Stage 2 is not runnable: preparation does not yet attach verified
source-grid biome identities or a verified NOx reference basis, and the bulk
inventory currently loses the product identity needed to enforce
product-specific coefficients. The mapper can combine direct CO with
composition-specific outputs only under independent-coefficient semantics,
with distinct targets and matching component identities; this has synthetic
tests but does not supply the missing product metadata. Do not substitute one
product's coefficients for the other or invent missing GBBEPx CH4/VOC. See
[tasks](../specs/001-fire-emissions/tasks.md) for the remaining implementation
and certification gates.

| Product | Pinned revision and sample | Interval and units | Species support |
|---|---|---|---|
| RAVE | v2r0; 2026-07-01 actual sample and 24 hourly crops in [`tests/data/fire/product_samples/`](../tests/data/fire/product_samples/) | Hourly; normalized to kg/cell/interval | All 11 canonical species; unscaled and embedded scaled variants |
| GBBEPx | v4r0; 2023-02-24 actual sample and crops in [`tests/data/fire/product_samples/`](../tests/data/fire/product_samples/) | Daily; source flux kg m-2 s-1 integrated over cell area and 86400 seconds | Eight species: CO2, CO, BC, OC, PM2.5, SO2, NH3, NOx; CH4, TPM and VOC unavailable |

Pinned source hashes and independent crop/reference totals are recorded in
[`data/fire/product_samples_manifest.yaml`](../data/fire/product_samples_manifest.yaml)
and [`tests/data/fire/product_samples/manifest.yaml`](../tests/data/fire/product_samples/manifest.yaml).
Requesting a missing GBBEPx species is an error, not an implicit zero.

[`data/fire/cb6_fire_factors_manifest.yaml`](../data/fire/cb6_fire_factors_manifest.yaml)
records 17 PM2.5-referenced and 26 CO-referenced CB6r5-AE7-AQ rows and their
source identity. PM2.5 coefficients sum to 1.0; CO coefficients sum to 1.1247.
Those are observed sums, not normalization instructions. The source excerpt is
[`data/fire/cb6_fire_config.txt`](../data/fire/cb6_fire_config.txt); its asset
hash and unresolved redistribution rights are recorded in
[`data/fire/asset_registry.yaml`](../data/fire/asset_registry.yaml). These
factors are not a complete production-ready CB6 fire profile: rights,
independent gas/aerosol budgets and compatibility with the production target
mechanism must be resolved first.

Exports are extensive target-species mass divided by target cell area and the
exact model interval, in `kg m-2 s-1`. Declare every `FIRE_` export in
`output.fields` and/or `nuopc.export_fields` with those units. Fire exports are
surface fields; existing species vertical-distribution configuration (`vdist`)
remains responsible for placement in model levels. Select `unscaled` or RAVE's
embedded `scaled` variant at decode time. CECE rejects repeated scaling and
does not apply extra fire scaling during preparation. FRP, FRE and dry-matter
diagnostics do not replace product species mass.

## Implemented contracts

[The shared input API](../include/cece/cece_fire_input.hpp) currently provides:

- Validated UTC timestamps, positive half-open intervals, independent
  source/product/algorithm/delivery identities and explicit availability cutoffs.
  Timestamps use `YYYY-MM-DDTHH:MM:SSZ` and Gregorian years 0001-9999;
  leap seconds, invalid dates and implicit/local time zones are rejected.
- Immutable shared grid definitions with explicit degree coordinates, quadrilateral
  corner bounds, positive supplied areas in m2 and a longitude convention.
  The initial supported CRSs are `EPSG:4326` and `OGC:CRS84`; projected coordinates
  require a future validated adapter. Date-line crossing and polar bounds are
  retained, not used to invent replacement cell areas.
- Component-resolved, selected-species inventory masses in kg, source/profile
  identities when supplied, coverage and quality. Product inventories may mark
  components unprofiled for invariant mappings; composition-specific mappings
  require matching identities, and FRP-derived inputs require biome/composition
  metadata. Species totals are calculated from those components.
- Product intervals with explicit availability for all eleven canonical species,
  source-grid cell areas, quality/coverage, and normalized kg/cell/interval
  masses. Supported source units are `kg/cell/interval`, `kg s-1`, and
  `kg m-2 s-1`; rates and fluxes are integrated over the exact UTC interval
  and source-cell areas. Overlapping intervals for the same product are rejected.
- Target-specific products retain their target id/version and kg fields and
  bypass bulk mapping only on an exact target match. Optional FRP, FRE, and dry
  matter diagnostics do not replace product mass or trigger emissions derivation;
  scaling identities are retained and a second scaling application is rejected.
- Explicit NOx reference mass and VOC profiles when those species are selected.
  A particulate-overlap profile is required when selected PM25/TPM total pools
  coexist with each other or with OC/BC component pools; no combined total of
  overlapping pools is exposed. Optional FRE/dry-matter diagnostics remain absent
  unless supplied.

These in-memory contracts are used by the decoders, normalized writer and
prepared-product runtime. Scientific-profile verification and production
eligibility remain separate approval gates. Unsupported metadata, missing
composition for a composition-specific mapping, nonfinite/negative mass and
unavailable input cannot silently become valid zero-emission inventories.
Partial positive coverage is retained explicitly and not renormalized into
emissions.

## Fire option validation

The native parser exposes typed `PhysicsSchemeConfig.fire` options when the
scheme name is `fire`. Python uses the existing
`PhysicsSchemeConfig(name="fire", options={...})` representation and validates
the same option combinations. No defaults are inserted into the Python options
dictionary, preserving serialization.

Both reject unknown keys and require an explicit mode, provider, target revision
and cutoff policy. `prepared` requires `input_manifest`; `coupled` prohibits
that prepared-file reference. Optional `source_manifest` is exclusively a
preparation input, must differ from the prepared output (including symlink
aliases), and is not required merely to parse a prepared runtime configuration.
Paths retain the existing working-directory-relative convention.

- Product mode requires `product_adapter` and `product_time_allocation`
  (`uniform`, `source_resolved` or `profile`). Only `profile` accepts and requires
  `product_time_profile`. Scientific derivation profiles are prohibited.
- Derivation requires `scientific_profile`, positive
  `max_observation_age_seconds`, and bulk chemistry; product options are prohibited.
- `chemistry_state` defaults to `bulk`, requiring `fire_mapping_file`.
  `target_specific` is product-only and prohibits a second mapping file.
  `mechanism_file`, `target_id` and `target_version` are always required.
- `requested_species` defaults to all eleven canonical bulk identifiers;
  explicit lists must be nonempty, unique and canonical.
- `cutoff_mode: fixed` requires `availability_cutoff` in strict UTC and prohibits
  `cutoff_schedule`. `host_supplied` prohibits a fixed timestamp and requires a
  schedule path for prepared inputs; a coupled host may optionally provide a
  matching schedule.
- `missing_policy: reconstruct` requires a named `reconstruction_policy`,
  positive `max_gap_seconds` and positive `max_observation_age_seconds`.
  The default `error` policy prohibits reconstruction options.
  Integer bounds are 1-2147483647 seconds.
- `scaling` defaults to `none`. Mapping dictionaries require nonempty strings;
  explicit output aliases must be unique.

Only one fire contribution is accepted per configuration: the current contract
does not identify independent spatial supports sufficiently to prove multiple
contributions nonoverlapping. This prevents accidental duplicate fire mass.

The option parser checks declarations only; the prepared runtime separately
verifies normalized interval content, manifest/file identity, temporal-profile
identity and cutoff consistency. Successful parsing does not certify a
scientific profile.
The [configuration validation CLI](../scripts/validate_cece_config.py) reuses the
Python checks and recognizes the registered `fire` scheme. The prepared adapter
supports bulk products only; coupled ingestion and target-specific product
loading are not implemented.

## Source-manifest preflight

[`prepare_fire_inputs.py`](../scripts/prepare_fire_inputs.py) can validate a
product-mode source manifest before any normalized output directory exists.
It reuses the Python CECE configuration validator, verifies local file SHA256
values, checks declared acquisition/availability cutoffs and source priorities,
and validates configured cutoff schedules and temporal-profile compatibility.
Preflight does not contact remote URIs or download sample granules.

Run it with `--preflight-only` to check metadata without creating output. The
non-preflight path requires the built normalizer executable. It supports local
bulk RAVE v2r0 and GBBEPx v4r0 inputs, validates source-manifest declarations
against product metadata, preserves source intervals, and writes one normalized
NetCDF file per source interval. Files are staged and checksummed before the
manifest is atomically committed; failures remove newly committed intervals and
preserve any previous manifest and files. Each interval file keeps the decoded
source identity and delivery ID separate from its source-manifest ID. Remote
acquisition and target-specific products are not supported. The prepared
runtime consumes the normalized manifest, but still requires a compatible
mapping profile and a supported rectilinear CECE target grid. Component
biome/composition identities are required only by composition-specific
mappings; the Phase 3 invariant product examples do not require them. FRP
derivation still requires its applicable biome/composition identities. This
does not imply chemistry certification.

Build the writer and run preparation with:

```bash
cmake --build build-fire --target cece_fire_product_normalize
CECE_FIRE_PRODUCT_NORMALIZER=build-fire/cece_fire_product_normalize \
  python scripts/prepare_fire_inputs.py \
    --config path/to/product-config.yaml \
    --output-dir fire_inputs/product
```

The configured source manifest must use adapter IDs `rave-v2r0` or
`gbbepx-v4r0`, explicit local file hashes, product/algorithm versions, source
variant, source interval, acquisition and availability timestamps. GBBEPx v4r0
advertises only its eight available species; the normalized manifest records
unavailable species explicitly.

## Synthetic mapping scheme

`FireScheme` accepts a validated bulk inventory through its staging API, applies
the hash-pinned target profile and required-species list, sums mapped component
masses, and exports each configured `FIRE_` field as kg m-2 s-1 over the
inventory's exact interval and cell areas. Exact-target inputs can bypass bulk
mapping only when their target id and version exactly match the configured
target. The scheme rejects missing staged intervals, missing/wrong-sized output
fields, non-mass output units and invalid mass/area values rather than creating
zero-filled outputs.

The scheme boundary and runtime lifecycle are covered with explicitly
synthetic, non-production chemistry assets. The prepared adapter loads
normalized product intervals, allocates them to the model interval/grid, and
hands the resulting component-resolved inventory to `FireScheme::Run`. Coupled
provider loading and host-field bindings remain unsupported.

Typed target profiles default to `mapping_factor_semantics: partition`, which
requires explicit unit-sum factors. The nonproduction
`independent_coefficients` mode preserves non-normalized coefficients such as
the CB6 CO rows; it does not certify target budgets or permit production
activation. It permits invariant and composition-specific mappings for the
same source only when their target names are distinct. Required composition
identities must still match for every component.

## Bounded provider foundation

[FireIntervalProvider](../include/cece/cece_fire_input.hpp) accepts already decoded
bulk inventories through prepared-file and coupled-byte staging methods. Both
verify `identity.content_sha256` against the supplied normalized payload using
OpenSSL SHA256. Files are read in 64 KiB chunks and checked on every staging
attempt, including reuse of an unchanged filename. Prepared staging does not
reopen raw sources. The RAVE/GBBEPx decoders and source-preparation path verify
raw local assets at their acquisition boundary.

The window has explicit positive limits for slots, cells, components and encoded
metadata per interval. They bound retained logical data independently of run
length, not process RSS or allocations made by a caller before staging. Each
slot holds at most one inventory. A failed stage throws, records a bounded
rejection reason and leaves no ready or zero-filled payload. A ready interval
may be revised before consumption, but cannot be silently evicted or replaced
with different time support. Overlapping ready intervals are rejected.

Cache identities include run/revision, mode, interval/grid/content and accepted
source hashes, acquisition/availability/cutoff, quality/science/scaling/target
profile identities, raw selection/schedule hashes and product temporal policy
and profile. Accepted-source ordering is canonicalized. Missing or incompatible
profile/cutoff declarations fail; profile hashes are identity references, not
proof of scientific certification.

Callers borrow a ready interval, successfully commit its output, then consume
the expected cache identity and evict/reuse its slot. Consumption releases the
inventory and advances constant-size committed-end/cutoff watermarks. Replayed
or overlapping committed support, stale cache identities, out-of-order commits
and backward cutoffs fail. Persist the provider checkpoint with output and
restore the same run/revision on restart; historical reprocessing requires an
explicit new run/revision. Durable output/checkpoint atomicity and measured
high-resolution memory behavior are not established. This provider foundation
is distinct from prepared-product runtime loading.

## Conservative allocation foundation

The shared allocation helper works on one explicit source interval and a supplied
first-order AXIS operator. It intersects positive half-open UTC intervals,
allocates mass uniformly within that source interval, converts each source
component/species from extensive kg to density using source area, applies AXIS,
then recovers extensive target kg. `DstArea` and `FracArea` normalization are
handled separately; fraction-normalized densities are multiplied by target
coverage when recovering mass. Source/target physical areas must match the
operator's area metric up to one common scale, such as spherical area to m2.

The helper validates nonnegative weights, indices, areas, source support and
destination normalization before use. It retains each source component's
composition identity through allocation and returns per-component/per-species
and species-total kg budgets. Supported source and allocated target budgets must
agree within 1e-6 relative plus 1e-12 absolute tolerance. There is no sum across
overlapping particulate species. Accepted source hashes and NOx/VOC/particulate
conventions are retained. Optional FRE and dry matter are allocated by the same
extensive-quantity operator with separate MJ and kg budgets; absent diagnostics
remain absent rather than being inferred from product mass.

Space/time coverage is reported without renormalizing missing support. Inventory
mass already represents supported mass, so coverage is not multiplied into mass
again. Unmapped or temporally disjoint cells remain unavailable, not valid-zero
fire. Observed/reconstructed/valid-zero quality remains explicit. This primitive
is applied independently to retained source intervals by the prepared adapter,
which then combines component-preserving results over the model interval and
regenerates conservative weights for each source/target geometry. This does not
certify target chemistry or replace a validated source composition.

## Fixtures and scientific gates

[The asset registry](../data/fire/asset_registry.yaml) distinguishes pinned
product references, verified-but-not-production CB6 factors, synthetic test
data and blocked scientific prerequisites. ABI/VIIRS reference scenes,
compatible RAVE derivation/ancillary assets and production-ready fire profiles
for supported chemistry targets remain unavailable. No MEGAN factor is used
as a fire factor.

[The RAVE ATBD manifest](../data/fire/rave_atbd_2_1_manifest.yaml) pins the
inspected NOAA document and records unresolved cloud-threshold and table-caption
inconsistencies. [The FRP reference manifest](../tests/data/fire/frp_reference_manifest.yaml)
records the missing independent sensor scenes and preapproved tolerances.
Neither manifest installs or certifies a derivation profile.

The candidate NOAA vegetation file's bounded header identifies a 2020 VIIRS
17-class IGBP product, with class 17 water; it does not establish compatibility
with RAVE's described 2019 MODIS ancillary, verified grid alignment, or
redistribution rights. The synthetic splitter assigns class 17 to its
grassland component as a test contract, not an approved water-fire policy.
Missing or invalid classes are rejected even for zero-fire cells. No ancillary
loader or production biome mapping is implied by these synthetic tests.
The RAVE reference manifest also records a candidate CC-BY-4.0 reprocessed
output archive; paired sensor inputs and approved tolerances remain missing.

The [GBBEPx V5 evidence manifest](../data/fire/gbbepx_v5_atbd_manifest.yaml)
records a distinct algorithm: available ATBD excerpts describe direct VIIRS
FRP-to-emissions estimates calibrated to QFED, not the RAVE FRE-to-dry-matter
chain. The complete V5 ATBD and coefficients have not been verified, and the
existing V4r0 decoder does not establish V5 support. Supplied product
emissions continue to bypass all FRP-to-emissions conversion.
NOAA's linked VIIRS I-band active-fire ATBD documents an upstream sensor
product; it is not the V5 emissions ATBD.

An experimental swath-area helper estimates spherical pixel areas from local
geolocation-center differences and excludes explicitly marked deleted pixels.
This is not a certified VIIRS footprint or conservative cell-overlap method,
and is not connected to a raw-input adapter or a runtime derivation profile.
Cloud-covered land still requires a separately validated land/water mask.

[The fixture schema](../tests/data/fire/fixture_schema.yaml) requires provenance,
redistribution information and independent expected values/tolerances.
[The constant-power fixture](../tests/data/fire/synthetic_interval.yaml) contains
deliberately artificial coefficients and hand-derived expectations. Its content
digest is recorded in the registry. It must not be used in production.

The [ATBD forest illustration](../tests/data/fire/rave_atbd_forest_reference.yaml)
uses hypothetical already-corrected sensor values and transcribed ATBD forest
means to check the stated precedence and dimensional arithmetic. It is not a
test of CECE sensor fusion or derivation, a deployed-table verification, or a
production profile. Document and derived-table redistribution rights remain
unverified; the fixture records the conservative no-permission status.

The native fire selectors and Python configuration tests exercise fixture
consistency, option/input validation, typed profile loading/mapping,
registered-scheme mass-flux export, exact-target bypass and test-helper device
round trips. Product tests decode pinned RAVE/GBBEPx crops and all 24 RAVE
hours. Conservation tests include actual RAVE and GBBEPx sample mass allocated
over multiple model substeps and changing target geometry, alongside interval
subdivision, extensive/density equivalence, partial support, mixed components
and generated AXIS weights across global longitude conventions and polar rows.
The [shared option cases](../tests/data/fire/config_cases.yaml) exercise native
and Python acceptance; neither suite loads production scientific assets.
Passing these checks does
not certify NOAA science, chemical mapping, coupled parity, offline operation
or high-resolution memory behavior.
