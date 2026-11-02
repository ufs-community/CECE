# Python column and grid physics

This is the first generic-interface increment, based on CECE develop 25f7209.
It invokes existing native kernels through a persistent `SchemeRuntime`, not the
standalone driver facade. Standalone and NUOPC retain their existing execution paths.

```python
import cece

with cece.Model(
    schemes=['megan3'],
    grid=cece.Grid.point(longitude=-105., latitude=40.),
) as model:
    print(model.required_inputs())
    model.set_inputs({
        'temperature': 303.,             # K
        'leaf_area_index': 3.,           # dimensionless
        'par_direct': 300.,              # W m-2
        'par_diffuse': 100.,             # W m-2
        'solar_cosine': 0.8,
        'soil_nox_emissions': 0.,        # explicitly choose zero kg NO m-2 s-1
    })
    result = model.step(time='2022-07-01T12:00:00Z', dt=3600.)
    print(result['MEGAN_ISOP'])
```

NumPy/mapping inputs use canonical units listed by `required_inputs()`. Scalar
forcing broadcasts explicitly over the selected domain. Optional xarray input
requires units, named dimensions and matching coordinates; it is transposed by
axis meaning, never by guessing from equal sizes. Outputs are xarray datasets
when xarray is installed and dictionaries of owned NumPy arrays otherwise.
Old results remain valid after another step or model closure.

Use `Grid.columns(longitude, latitude, levels=...)` for independent columns or
`Grid.structured(longitude=..., latitude=..., levels=...)` for a rectilinear grid.
Structured flattening is latitude/longitude C order, so longitude varies fastest.
Native storage remains `(column, 1, axis)` in Kokkos LayoutLeft. Atmospheric
`level` and BDSNP `biome` axes are distinct. A surface input needs no artificial
vertical dimension. Grid geometry is fixed for an instance.

For labeled datasets map source variable names explicitly:

```python
model.set_inputs(dataset[['T2M']], mapping={'T2M': 'temperature'})
```

An update validates all supplied fields before changing forcing. Unmentioned
fields retain their previous values. `validate_inputs()` checks a prospective
complete set, including missing required fields. Unknown fields, duplicate
mapped producers, incompatible units or coordinates and non-finite values fail.
There is no automatic regridding, vertical interpolation, unit guessing, data
acquisition, or implicit emission stacking in this increment. Prepare source
fields explicitly before calling the model. MONETIO and earthaccess providers,
shared regridding and full `CeceSimulation` bindings are subsequent plan increments.

## Supported execution contracts

- `native_example`: profile-capable demonstration, with optional multiplier.
- `megan3`: native bulk surface MEGAN3 with configurable existing speciation files.
- `bdsnp`: native YL95 or effective-input BDSNP. The canonical mode requires its
  24-biome arrays and effective canopy/fertilizer/deposition inputs; these are not
  interchangeable with raw observed fields. See `soil_nox.md` for source-contract units.
- Other registered schemes are explicitly rejected until their column contracts
  are reviewed; kernel registration alone is not proof of independent-column support.

A composed model uses explicit dependency order, for example
`schemes=['bdsnp', 'megan3']`. Supplying an external soil-NO field in that case
is an error. A MEGAN3-only model requires explicit soil NO, including explicit zero
if desired. It does not quietly invent the missing contribution.

## Time, state, and ownership

`step(time, dt)` describes `[time, time+dt)` in UTC. Numeric times are Unix seconds;
naive datetimes are interpreted as UTC. Calls must be contiguous. The reviewed
kernels return fluxes rather than integrated amounts: `dt` does not multiply those
fluxes or alter their formulas. Solar geometry and effective history/day-of-year
remain explicit forcing/configuration, as in the existing native implementations;
`time` does not silently synthesize those scientific inputs.

MEGAN3's `MeganHistory` is configured scalar effective history, not an evolving
rolling average. BDSNP pulse/canopy/deposition history likewise arrives as explicit
inputs. JSON-serializable `state_dict()` captures forcing, options, geometry and
next interval time. `load_state_dict()` checks compatibility and replaces the
executor atomically. This is not a general restart implementation for unreviewed
prognostic schemes. Relative/absolute scientific parameter paths must still resolve
in the restore environment.

Independent models own separate buffers and scheme objects. Same-instance native
calls are guarded against concurrent execution; callers should serialize Python
updates and stepping on an individual Model. Kokkos is initialized if necessary
and remains available to the embedding process; closing a model does not finalize
a host-owned runtime. A kernel failure poisons its native executor and publishes
no result; recreate or explicitly restore the model before further computation.

## Build and checks

Build the repository development Docker image; include Python development headers.
Configure `-DBUILD_PYTHON_BINDINGS=ON`, then build `_cece_core` and use
`PYTHONPATH=build/src/python` against the resulting package. The package includes
its default speciation YAML files. The native core must remain available through
the existing CMake install/library search arrangement; this increment does not
provide a self-contained binary wheel.

Run the new `test_python_model`, configuration, lifetime and extension-identity
pytest suites. Preserve old `cece.initialize/compute/finalize`: that API performs
stacking, while `Model` performs selected physics. All configuration forms now use
the same native parser; computed results retain their allocation safely.
