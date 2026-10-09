# NUOPC Cap Parity

CECE ships two ways to run a simulation:

- **`cece_standalone_driver`** — the C++ driver (`src/main.cpp`), used for
  standalone emission runs and benchmarks.
- **`cece_nuopc_app`** — the Fortran NUOPC cap (`src/driver/nuopc/`), which
  embeds CECE as an ESMF GridComponent for coupled Earth-system models and also
  runs standalone when launched directly.

Both entry points call the **same** C++ orchestration core (`CeceSimulation`,
exposed through the `cece_sim_*` C ABI in `include/cece/cece_sim_c_abi.h`).
This page documents what parity means for the cap, which grid topologies it
supports, and how to run the parity checks.

## What parity guarantees

For the same YAML configuration and input data:

1. **Bit-for-bit output.** The cap's NetCDF output is byte-identical to the C++
   driver's at every configured output step — same field values, same time
   stamps, same coordinate variables. Comparison uses variable-level
   comparison (`tests/nccmp_var.c`, tolerance 0) because the HDF5 creation
   timestamp inside each file differs run-to-run even for the same driver.
2. **Feature completeness.** Every feature of the C++ driver — species, data
   streams, stacking, physics schemes, diagnostics, and every configured
   `output.fields` entry — is available through the cap, because the cap does
   not reimplement any of it.
3. **Rank-count invariance.** The cap's output at `-np 1`, `-np 2`, and `-np 3`
   is bit-for-bit identical, and matches the C++ driver at `-np 1`.
4. **One time convention.** Both drivers ingest the data stream at the
   step-start instant and stamp the output at the step-end instant (elapsed
   seconds measured from `driver.start_time`).

## Supported grid topologies through the cap

The cap can realize its component on any of the three grid shapes the writer
supports. The grid comes from one of two sources
(resolution order documented in the
[driver configuration guide](driver_configuration.md)):

| Topology | Config-built (YAML) | Parent-provided (coupled) |
|----------|---------------------|---------------------------|
| Rectilinear | uniform `nx × ny` extents, named grid (`F360`/`R360`), or 1-D GRIDSPEC coords | parent `ESMF_Grid` with 1-D `lon[nx]`/`lat[ny]` center coords |
| Curvilinear | 2-D GRIDSPEC file (`gridspec_file:` with `nx × ny` 2-D coords) | parent `ESMF_Grid` with 2-D coords (flattened across PETs) |
| Unstructured | `ny: 1` node row, or a UGRID/mesh gridspec file | parent `ESMF_Mesh` node coordinates |

Notes:

- Radian coordinate systems are converted to degrees and longitudes wrapped to
  `[-180, 180)` inside the shared C++ code — the cap passes the raw extracted
  arrays plus an `is_radian` flag and never post-processes coordinates itself.
- The vertical layer count `nz` always comes from `driver.grid.nz` in the
  config; a flat 2-D parent grid carries no vertical dimension.
- When both a parent grid and a YAML grid are present, the parent wins and a
  single warning names the ignored YAML grid. The two are never merged.
- An unsupported parent topology (e.g. a coordinate shape that matches neither
  1-D, nor flattened 2-D, nor node-list conventions) fails loudly at
  `InitializeRealize` with a named diagnostic. There is no fallback to a
  uniform grid.

## Field coupling: how CECE exchanges data with a host

When the YAML carries a `nuopc:` section (see the
[Configuration Reference](configuration.md#nuopc)), the cap exchanges fields
with the coupling framework through the standard NUOPC phases:

1. **Advertise.** The cap declares one field per configured entry: exports
   offer CECE's own geometry ("will provide") and imports accept the peer's
   ("cannot provide"). Both sides of every pair request reference sharing.
   Each `standard_name` must exist in the active NUOPC Field Dictionary —
   which the **host application** installs before components initialize
   (ESMF's preloaded dictionary covers only common ocean/land-surface names).
   A name missing from the dictionary fails the run at advertise time, and
   the error names it.
2. **Realize.** Connected exports stay on CECE's component grid; connected
   imports are realized on the grid transferred from the providing peer, so
   the two sides share an identical data distribution. Configured-but-
   unconnected fields are removed at realization: they allocate nothing and
   produce no error, which is what makes a `nuopc:`-bearing config behave
   exactly like the same config without peers.
3. **Run.** Exports need no per-step data movement — a host that shares
   CECE's decomposition reads CECE's computed storage by reference every
   step. For imports, the cap copies the delivered values from the coupling
   field into the simulation's input storage before each step, resolving the
   configured input name through the meteorology/scale-factor/mask mappings.
4. **Timestamps.** The cap stamps its exports at the step-**start** instant
   rather than the framework's step-end default. A consumer's default import
   check compares field timestamps against its own clock current time, which
   during a driver sweep equals the step start; with shared fields the
   consumer sees the provider's stamp directly, so step-end would always be
   one step ahead and fail. Symmetrically, a provider whose connector runs
   before it advances (e.g. a met source feeding CECE) needs no user-side
   stamping at all: the framework's step-end default, delivered one sweep
   late through the connector, equals the consumer's current step start.

The standalone NUOPC app can emulate a dictionary-providing host for testing
via `--field-dictionary <path.yaml>`; without the flag the preloaded
dictionary applies and advertising specialized names fails as it would in an
under-provisioned host.

## Running the parity checks

All commands below run inside the ESMF-enabled dev container
(`cece/cece-dev:esmf`, built with `BUILD_ESMF=ON`). The NUOPC tests are
registered only when ESMF and Fortran are found; ESMF-less builds skip them.

### 1. Driver parity at `-np 1` (uniform grid)

```bash
docker run --rm -v "$PWD:/work" -w /work/build \
  -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
  cece/cece-dev:esmf bash -c "ctest -R '^test_driver_nuopc_parity$' --output-on-failure"
```

Runs both drivers on `examples/cece_config_ceds_oc_small.yaml` and asserts
bit-for-bit equality of every configured field, the `time` variable, and the
full output variable set.

### 2. Curvilinear (GRIDSPEC) parity

```bash
docker run --rm -v "$PWD:/work" -w /work/build \
  -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
  cece/cece-dev:esmf bash -c "ctest -R '^test_driver_nuopc_gridspec$' --output-on-failure"
```

Same comparison on `examples/cece_config_ceds_oc_gridspec.yaml` (2-D
coordinates from `data/C96_grid_spec.tile1.nc`), plus an assertion that the
cap's output carries rank-2 coordinate variables.

### 3. Unstructured target

```bash
docker run --rm -v "$PWD:/work" -w /work/build \
  -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
  cece/cece-dev:esmf bash -c "
    ./cece_standalone_driver /work/examples/cece_config_ceds_oc_unstructured.yaml
    ./bin/cece_nuopc_app /work/examples/cece_config_ceds_oc_unstructured.yaml"
```

`examples/cece_config_ceds_oc_unstructured.yaml` targets a flattened `ny = 1`
node row (72 nodes). Both drivers write the same `oc rank=3 dims=lev:1
mesh_dim0:1 lon:72` geometry with identical values. (The cap parity script
performs the bit-for-bit comparison when invoked with that config.)

### 4. Distributed output equivalence (`-np 1/2/3`)

```bash
docker run --rm -v "$PWD:/work" -w /work/build \
  -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
  cece/cece-dev:esmf bash -c "ctest -R '^test_nuopc_distributed_output_equivalence$' --output-on-failure"
```

Runs the cap at `-np 1/2/3`, asserts rank-count invariance and cap-vs-driver
equality at `-np 1`, and asserts every configured `output.fields` entry is
present in the cap output at each rank count.

### 5. Grid-contract unit test

```bash
docker run --rm -v "$PWD:/work" -w /work/build \
  -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
  cece/cece-dev:esmf bash -c "ctest -R '^test_grid_spec_esmf$' --output-on-failure"
```

Feeds synthetic ESMF-style coordinate arrays through the shared classification
entry (`cece_sim_grid_from_esmf`) and asserts topology, unit conversion,
wrapping, and loud rejection of unsupported shapes.

### 6. Field-coupling integration harness

```bash
docker run --rm -v "$PWD:/work" -w /work/build \
  -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
  cece/cece-dev:esmf bash -c "ctest -R '^test_nuopc_field_coupling$' --output-on-failure"
```

Runs the two- and three-component driver app in
`tests/nuopc_coupling/` (CECE plus a file-writing sink peer, and a constant
met-source peer) against the coupled CEDS fixture and a minimal field
dictionary, and asserts: the sink's received export matches the standalone
output bit-for-bit at `-np 1` and `-np 2`; a configured-but-unconnected export
is pruned without error; the met-source import is advertised, realized, and
copied each step without disturbing the export; the standalone NUOPC app
without a host dictionary fails at advertise naming the unknown standard
name; and with `--field-dictionary` it succeeds and matches the C++ driver
byte-for-byte.

## Related documentation

- [Configuration Reference](configuration.md) — the `nuopc:` section schema.
- [Driver Configuration Guide](driver_configuration.md) — cap execution modes,
  grid sources, and precedence rules.
- [Driver Configuration Guide (full)](driver_configuration_guide.md) — all
  `driver:` options.
- [GRIDSPEC File Input Support](mesh_file_input.md) — building and supplying
  curvilinear/unstructured grid files.
