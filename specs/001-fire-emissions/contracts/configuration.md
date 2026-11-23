# Proposed Fire Configuration and Preparation Contract

**Status**: Design only. Commands/options/profiles become available during implementation.

## Scheme Options

Add `fire` under existing `physics_schemes`. Fire-free configs are unchanged. Paths follow existing CECE path-resolution conventions, documented consistently in final examples.

| Option | Contract |
|--------|----------|
| `mode` | Required `product` or `derive_frp`, exclusive per fire contribution |
| `input_provider` | `prepared` or `coupled`, same normalized interval contract |
| `source_manifest` | Required by preparation/acquisition only; raw-source selection defined below, distinct from prepared outputs |
| `input_manifest` | Required for prepared runtime: normalized schema, source/metadata identities, interval files; never interpreted as raw-source selection |
| `product_adapter` | Product-only, verified GBBEPx/RAVE revision |
| `product_time_allocation` | Required in product mode: `uniform`, `source_resolved`, or `profile`; semantics below |
| `product_time_profile` | Required only for `profile`; versioned normalized temporal weights, not FRP conversion factors |
| `scientific_profile` | Required for derivation, prohibited in product mode |
| `mechanism_file` | Exact target species/version definition |
| `fire_mapping_file` | Required for bulk inputs, prohibited for already target-specific inputs |
| `chemistry_state` | `bulk` default or product-only `target_specific`; explicit declaration must match input metadata, never infers speciation from filenames |
| `target_id`, `target_version` | Verified revision of one of five requested target families |
| `requested_species` | Explicit bulk set, default eleven; missing requested species fails |
| `missing_policy` | `error` default or `reconstruct`; latter requires supported policy and age/gap settings |
| `reconstruction_policy` | Required only for `missing_policy: reconstruct`; verified retrospective or causal profile ID; otherwise absent |
| `max_gap_seconds` | Required positive bound for reconstruction, within the selected profile's verified limit |
| `cutoff_mode` | Required: `fixed` or `host_supplied`; never infer a cutoff from wall-clock time |
| `availability_cutoff` | Required only for `fixed`; UTC timestamp |
| `cutoff_schedule` | Required for preparation and prepared runtime with `host_supplied`; interval-to-cutoff file defined below; prohibited in fixed mode |
| `max_observation_age_seconds` | Required positive bound in derivation or any observation/persistence policy; age is cutoff minus acquisition time |
| `scaling` | `none` default or verified profile; reject already-applied scaling |
| `input_mapping`, `output_mapping` | Existing base field-name mapping, no semantic conversion |

Illustrative product options, not a complete model configuration:

```yaml
physics_schemes:
  - name: fire
    options:
      mode: product
      input_provider: prepared
      source_manifest: fire_sources/rave_sources.yaml
      input_manifest: fire_inputs/manifest.yaml
      product_adapter: rave-verified-file-revision
      product_time_allocation: source_resolved
      requested_species: [CO2, CO, CH4, PM25, TPM, SO2, OC, BC, NOX, NH3, VOC]
      target_id: CB6
      target_version: verified-target-revision
      mechanism_file: fire_profiles/cb6_species.yaml
      fire_mapping_file: fire_profiles/cb6_fire_mapping.yaml
      missing_policy: error
      cutoff_mode: fixed
      availability_cutoff: "2022-07-02T00:00:00Z"
      scaling: none
```

Derivation substitutes `derive_frp`, removes all product-specific options, and supplies the scientific profile and observation-age bound. Reconstruction additionally requires its verified policy and gap bound. Reject unknown/inconsistent options.

Implementation detail: `chemistry_state` makes the existing bulk versus
target-specific validation rule decidable before data loading. The initial
configuration accepts one fire contribution because no independent contribution
support/domain identifier is defined. Paths follow existing working-directory
resolution, and prepared runtime does not require the raw source manifest.
Structural native/Python option validation and local product normalization are
implemented. Normalization verifies product file metadata against source-manifest
identities and records source/output hashes and cutoff policy. Runtime manifest
loading, remote acquisition, profile certification and product allocation remain
the responsibility of pending provider/profile stages.

## Raw Source Manifest

`source_manifest` is a versioned YAML file with `schema_version: 1` and a nonempty `sources` list. Each entry contains a unique `source_id`, `adapter_id`, product/algorithm versions, acquisition and availability timestamps, and exactly one of `local_path` or `uri`. Local entries require `content_sha256`; remote entries require an expected digest or a provider checksum/version verified and recorded before normalization. URLs must not embed credentials. Paths resolve relative to the source manifest.

Sensor entries additionally identify platform, instrument and granule identity; product entries identify variant and bulk versus target-specific chemistry. File metadata remains authoritative, and incompatible manifest declarations fail. Explicit per-source `priority` integers (higher wins) resolve overlapping revisions only within compatible product/variant identities; conflicting content with equal priority fails. Priority never authorizes summing duplicate observations or silently mixing scaled/unscaled variants. Sensor quality/overlap selection remains the scientific profile's responsibility.

The CLI reads raw inputs from `source_manifest` and writes the normalized `manifest.yaml` below `--output-dir`. Preflight does not require that output manifest to exist. Prepared runtime reads `input_manifest`, verifies it against the configured identities/policies, and does not open raw sources. Preparation rejects a source manifest that resolves to its own normalized output.

## Product Temporal Allocation

- `uniform`: explicitly assume constant rate within each supplied interval, allocating its mass by overlap duration. Record this assumption; do not merge or flatten finer source intervals.
- `source_resolved`: preserve all supplied subintervals and their totals. Within the smallest supplied interval, allocate by overlap duration; do not infer finer variability or bridge missing intervals.
- `profile`: use versioned nonnegative weights whose integral over each source interval is one. The profile declares UTC or local-solar-time basis and supported source cadence. Reject it if the product is already temporally disaggregated at that cadence or finer, or metadata shows the same weighting has already been applied.

All modes preserve covered interval mass and report partial support without renormalizing missing coverage. `product_time_profile` is prohibited for the other two modes. Product-time weighting does not activate satellite fusion or FRP-to-emission algorithms.

## Availability Cutoff and Reconstruction

- Each consumed interval has one explicit UTC cutoff shared by all ranks and both providers. `fixed` uses the configured timestamp for every interval.
- In `host_supplied`, a coupled host supplies the interval start/end and cutoff before ingestion. Prepared execution and preparation read `cutoff_schedule`, a versioned file with one unique `(interval_start, interval_end, availability_cutoff)` entry per requested model interval. Missing, mismatched or duplicate entries fail; when a coupled run also has a preparation schedule, host values must match it.
- Cutoffs must be nondecreasing across forward-processed intervals. Historical reprocessing uses a new explicit run/revision identity, not a backward cutoff within the same run. No fallback to the current date or previous interval's cutoff is permitted.
- Both acquisition and availability times must be at or before cutoff. Observation age is `cutoff - acquired_at` in seconds, nonnegative and no larger than `max_observation_age_seconds`. Unknown required timestamps fail rather than assume availability.
- Retrospective bracketing may use later-acquired observations only when available by cutoff and within profile age/gap limits. Causal policies additionally prohibit observations acquired after the modeled interval end, even if a later cutoff would make them available.
- The initial RAVE retrospective profile does not imply a verified causal profile exists. A causal reconstruction request must name an installed, independently verified profile defining anchor selection, time integration, normalization and age/gap applicability. Otherwise initialization fails. Do not invent a one-sided formula.
- Default `missing_policy: error` requires complete valid support. `reconstruct` requires the policy and gap bound above; unsupported coverage, stale anchors or missing context fail explicitly. A connected preparer may wait/retry within its declared bound, but cannot commit an incomplete interval as ready or silently advance model time.
- Prepared manifests record the accepted observations, interval cutoffs, policies and schedule hash where applicable. A changed schedule/cutoff invalidates incompatible prepared/cache identities. Cutoff checks repeat at runtime.

## Preparation CLI

```text
python scripts/prepare_fire_inputs.py
  --config CONFIG
  --output-dir DIRECTORY
  [--preflight-only]
  [--overwrite]
```

- Complete CECE config selects input mode, `source_manifest` and profiles; `input_manifest` is a separate runtime output reference. Record resolved source/output identities and temporal/cutoff policies in the normalized manifest.
- Preflight validates metadata, units, bounds, assets and profile compatibility without interval outputs.
- Inputs default to explicit local granules/manifests. Optional remote acquisition needs verified provider endpoints; NASA Earthaccess is not assumed to host NOAA fire products.
- Preparation decodes/normalizes. Nonlinear correction, cross-sensor fusion, diurnal reconstruction and emission generation remain in CECE.
- Remote acquisition uses bounded retries and explicit network/authentication errors; staged compute consumes local files only.
- Output commits atomically per interval; incompatible existing manifests fail unless explicitly overwritten.
- Exit 0: complete requested operation; exit 2: invalid user configuration/unsupported metadata or profile; exit 1: acquisition/decoding/output failure. Nonzero exits include actionable diagnostics.

## Coupled Interface

- Advertise required imports and `FIRE_<target_species>` exports before initialization through existing NUOPC field dictionary/config/state binding.
- Coupled imports provide equivalent sensor time/power/QA/geometry or product mass fields and metadata.
- Static identities validate at initialization; dynamic time/cutoff/coverage validates before each interval.
- Host-supplied cutoff metadata follows the availability contract above; missing host values are errors, never implicit wall-clock defaults.
- Host-supplied pre-fused observations or emissions cannot masquerade as CECE-derived sensor fusion.
- No new public C ABI by default. If essential metadata cannot use existing mechanisms, document/test the smallest compatible extension before acceptance.

## Errors and Diagnostics

- Initialization errors identify option/source/profile and expected versus actual units/version.
- Runtime errors identify interval, missing context, age/coverage and rank as applicable.
- Missing required fields use existing required-field/error propagation, not silent returns.
- Product-mode FRP conversion is not applicable; absent optional diagnostics remain explicitly absent.
- Output units and target identity appear in field metadata and provenance.
