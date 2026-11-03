# Test data provenance

Fixtures in this directory serve different purposes and must not be treated as
interchangeable scientific inputs. The locally bundled `data/speciation/MACCity_4x5.nc`
copy is an intentionally invalid geometry regression fixture: its latitude
coordinate includes 91 degrees and it advertises no cell bounds. It must fail
geometry validation and is not the authoritative MACCity dataset used by the
parity example.

The `hemco_megan/` fixtures are reference inputs supplied for MEGAN integration
tests; their provenance and intended use are described in that directory's
README. Small grids created by test helpers are synthetic and must declare
their coordinate ordering, boundaries, seam behavior, and area model in the
test that creates them. No synthetic or local negative fixture establishes
MACCity domain boundaries or conservative suitability.
