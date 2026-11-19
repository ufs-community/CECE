# Fire emissions: implementation status

The fire feature is **not operational yet**. No `fire` physics scheme, NOAA
decoder, preparation command, production chemistry profile or Ursa fire-run
script is currently available. Existing fire-free configurations are unchanged.

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

These are validated in-memory contracts, not file readers or science algorithms.
Hash fields are checked for syntax; actual file hashing/profile-content
verification belongs to the provider, which is not yet implemented. Unsupported
metadata, missing composition, nonfinite/negative mass and unavailable input
cannot silently become valid zero-emission inventories. Partial positive
coverage is retained explicitly; runtime missing-policy enforcement is pending.

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

The four `test_fire_*` selectors currently exercise fixture consistency, input
validation and test-helper device round trips only. Passing these checks does
not certify NOAA science, chemical mapping, coupled parity, offline operation
or high-resolution memory behavior.
