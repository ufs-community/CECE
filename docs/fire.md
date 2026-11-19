# Fire emissions: implementation status

The fire feature is **not operational end to end yet**. The `fire` physics
scheme is registered and can map an explicitly staged synthetic/test inventory
to isolated `FIRE_` mass-flux fields, including exact-target bypass. The CECE
driver does not yet stage prepared/coupled inventories or advertise/populate
those export fields; no NOAA decoder, preparation command, production chemistry
profile or Ursa fire-run script is available. Existing fire-free runtime
behavior is unchanged.
Building the core now requires system OpenSSL Crypto for input verification
(see the [developer guide](developer_guide.md#development-environment)).

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
  identities, coverage and quality. Components retain their own biome/composition
  references. Species totals are calculated from those components.
- Explicit NOx reference mass, VOC profile and particulate-pool definitions when
  those species are selected. No combined total of overlapping particulate pools
  is exposed. Optional FRE/dry-matter diagnostics remain absent unless supplied.

These are validated in-memory contracts, not NOAA decoders or science algorithms.
Hash fields are checked for syntax; the provider below additionally verifies
normalized payload bytes. Scientific-profile content verification remains
pending. Unsupported
metadata, missing composition, nonfinite/negative mass and unavailable input
cannot silently become valid zero-emission inventories. Partial positive
coverage is retained explicitly; runtime missing-policy enforcement is pending.

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

This validation checks declarations, not asset existence, schedule contents,
profile certification, metadata agreement or duplicate scaling within actual
inputs. Those checks require pending manifest/schedule readers and
profile/runtime integration.
The [configuration validation CLI](../scripts/validate_cece_config.py) reuses the
Python checks and recognizes the registered `fire` scheme. Do not treat
successful option parsing as an executable ingestion example: driver-side
provider staging and output-field registration are still pending.

## Synthetic mapping scheme

`FireScheme` accepts a validated bulk inventory through its staging API, applies
the hash-pinned target profile and required-species list, sums mapped component
masses, and exports each configured `FIRE_` field as kg m-2 s-1 over the
inventory's exact interval and cell areas. Exact-target inputs can bypass bulk
mapping only when their target id and version exactly match the configured
target. The scheme rejects missing staged intervals, missing/wrong-sized output
fields, non-mass output units and invalid mass/area values rather than creating
zero-filled outputs.

These are in-memory scheme boundaries exercised with explicitly synthetic,
non-production assets. They do not imply that manifests are decoded, temporal
or spatial allocation is invoked by the scheme, nor that the CECE driver is
currently staging inventory data. Core provider lifecycle and export-field
integration remain pending.

## Bounded provider foundation

[FireIntervalProvider](../include/cece/cece_fire_input.hpp) accepts already decoded
bulk inventories through prepared-file and coupled-byte staging methods. Both
verify `identity.content_sha256` against the supplied normalized payload using
OpenSSL SHA256. Files are read in 64 KiB chunks and checked on every staging
attempt, including reuse of an unchanged filename. Prepared staging does not
reopen raw sources. The decoder/host must bind the inventory values and metadata
to those same bytes; actual decoders and host bindings are not yet implemented.
The standalone content/file digest helpers can also verify raw assets at their
future acquisition boundary.

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
explicit new run/revision. Durable output/checkpoint atomicity, runtime wiring,
schedule-content/host agreement, observation-age and causal reconstruction
enforcement remain later tasks. This in-memory foundation is not a runnable
fire ingestion workflow or a measured high-resolution memory result.

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
does not merge finer source intervals, choose a temporal profile, perform
chemistry, generate geometry weights or wire runtime ingestion; those remain
the responsibility of the subsequent product/profile/lifecycle stages.

## Fixtures and scientific gates

[The asset registry](../data/fire/asset_registry.yaml) distinguishes synthetic
test data from missing scientific prerequisites. All production assets remain
blocked: representative GBBEPx/RAVE granules, ABI/VIIRS reference scenes, compatible
RAVE scientific/ancillary assets and fire-specific AM4, CB6, CRACMM2, RACM2 and
GOCART2G profiles. No MEGAN factor is used as a fire factor.

[The fixture schema](../tests/data/fire/fixture_schema.yaml) requires provenance,
redistribution information and independent expected values/tolerances.
[The constant-power fixture](../tests/data/fire/synthetic_interval.yaml) contains
deliberately artificial coefficients and hand-derived expectations. Its content
digest is recorded in the registry. It must not be used in production.

The native fire selectors and Python configuration tests exercise fixture
consistency, option/input validation, synthetic typed profile loading/mapping,
registered-scheme mass-flux export, exact-target bypass and test-helper device
round trips.
The conservation selector also checks interval subdivision, extensive/density
equivalence, partial support, mixed components and generated AXIS weights across
global longitude conventions and polar rows.
The [shared option cases](../tests/data/fire/config_cases.yaml) exercise native
and Python acceptance; neither suite loads production scientific assets.
Passing these checks does
not certify NOAA science, chemical mapping, coupled parity, offline operation
or high-resolution memory behavior.
