"""Scientific and lifecycle acceptance for the persistent Python model."""

import gc
import json

import cece
import numpy as np
import pytest
import xarray as xr


def test_columns_run_physics_and_do_not_accumulate_previous_outputs():
    with cece.Model(
        schemes=["native_example"],
        grid=cece.Grid.columns([0, 1, 2], [30, 31, 32]),
        options={"native_example": {"multiplier": 3.0}},
    ) as model:
        model.set_inputs({"base_anthropogenic_nox": np.array([1.0, 2.0, 4.0])})
        first = model.step(time="2022-01-01T00:00:00Z", dt=3600)
        np.testing.assert_array_equal(first.nox.values[:, 0], [3.0, 6.0, 12.0])
        model.set_inputs({"base_anthropogenic_nox": np.array([2.0, 3.0, 5.0])})
        second = model.step(time="2022-01-01T01:00:00Z", dt=3600)
        np.testing.assert_array_equal(second.nox.values[:, 0], [6.0, 9.0, 15.0])
    gc.collect()
    np.testing.assert_array_equal(first.nox.values[:, 0], [3.0, 6.0, 12.0])


def test_xarray_dimension_order_and_units_on_nonsquare_grid():
    grid = cece.Grid.structured(longitude=[0, 1, 2], latitude=[30, 31])
    source = xr.Dataset({
        "source": (
            ("longitude", "latitude"),
            np.arange(6.0).reshape(3, 2),
            {"units": "1"},
        )
    })
    with cece.Model(["native_example"], grid=grid) as model:
        model.set_inputs(source, mapping={"source": "base_anthropogenic_nox"})
        result = model.step(time=0, dt=1)
        assert result.nox.dims == ("latitude", "longitude", "level")
        np.testing.assert_array_equal(
            result.nox.values[:, :, 0],
            source.source.transpose("latitude", "longitude").values * 2,
        )


def test_invalid_field_does_not_advance_time_and_reports_scheme_requirement():
    with cece.Model(["native_example"], grid=cece.Grid.point(0, 30)) as model:
        with pytest.raises(ValueError, match="base_anthropogenic_nox"):
            model.step(time=0, dt=1)
        model.set_inputs({"base_anthropogenic_nox": 3.0})
        with pytest.raises(ValueError, match="positive"):
            model.step(time=0, dt=-1)
        model.step(time=0, dt=1)
        with pytest.raises(ValueError, match="contiguous"):
            model.step(time=0, dt=1)


def test_restart_and_independent_instances():
    grid = cece.Grid.columns([0, 1], [30, 31])
    with cece.Model(["native_example"], grid=grid) as original:
        original.set_inputs({"base_anthropogenic_nox": [2.0, 4.0]})
        original.step(time=0, dt=1)
        snapshot = json.loads(json.dumps(original.state_dict()))
        expected = original.step(time=1, dt=1)
        with cece.Model(["native_example"], grid=grid) as restored:
            restored.load_state_dict(snapshot)
            actual = restored.step(time=1, dt=1)
            xr.testing.assert_equal(actual, expected)
        original.step(time=2, dt=1)


def megan_inputs(ncol):
    return {
        "temperature": np.full(ncol, 303.0),
        "leaf_area_index": np.full(ncol, 3.0),
        "par_direct": np.full(ncol, 300.0),
        "par_diffuse": np.full(ncol, 100.0),
        "solar_cosine": np.full(ncol, 0.8),
        "soil_nox_emissions": np.zeros(ncol),
    }


def test_megan3_batch_matches_independent_columns_and_responds_to_forcing():
    with cece.Model(
        ["megan3"], grid=cece.Grid.columns([0, 1, 2], [30, 31, 32])
    ) as batch:
        data = megan_inputs(3)
        data["temperature"] = np.array([295.0, 303.0, 310.0])
        batch.set_inputs(data)
        actual = batch.step(time=0, dt=3600)
        assert "MEGAN_ISOP" in actual
        assert np.all(actual.MEGAN_ISOP.values > 0)
        for i in range(3):
            with cece.Model(["megan3"], grid=cece.Grid.point(i, 30 + i)) as column:
                column.set_inputs({k: v[i] for k, v in data.items()})
                single = column.step(time=0, dt=3600)
                for name in actual:
                    np.testing.assert_allclose(
                        actual[name].values[i],
                        single[name].values[0],
                        rtol=2e-12,
                        atol=1e-27,
                    )
        data["par_direct"] = np.zeros(3)
        data["par_diffuse"] = np.zeros(3)
        data["solar_cosine"] = np.zeros(3)
        batch.set_inputs(data)
        night = batch.step(time=3600, dt=3600)
        assert np.all(night.MEGAN_ISOP.values < actual.MEGAN_ISOP.values)


def test_megan_requires_explicit_soil_no_policy():
    with cece.Model(["megan3"], grid=cece.Grid.point(0, 30)) as model:
        data = megan_inputs(1)
        del data["soil_nox_emissions"]
        model.set_inputs(data)
        with pytest.raises(ValueError, match="soil_nox_emissions"):
            model.step(time=0, dt=1)


def test_bdsnp_dependency_order_and_external_collision():
    with pytest.raises(ValueError, match="ordered"):
        cece.Model(
            ["megan3", "bdsnp"],
            grid=cece.Grid.point(0, 30),
            options={"bdsnp": {"soil_no_method": "yl95"}},
        )
    with cece.Model(
        ["bdsnp", "megan3"],
        grid=cece.Grid.point(0, 30),
        options={"bdsnp": {"soil_no_method": "yl95"}},
    ) as model:
        data = megan_inputs(1)
        with pytest.raises(ValueError, match="output"):
            model.set_inputs(data)
        del data["soil_nox_emissions"]
        data.update(soil_temperature=np.array([303.0]), soil_moisture=np.array([0.2]))
        model.set_inputs(data)
        result = model.step(time=0, dt=1)
        assert result.soil_nox_emissions.item() > 0


def test_unsupported_scheme_and_bad_units_are_explicit():
    with pytest.raises(ValueError, match="reviewed column contract"):
        cece.Model(["volcano"], grid=cece.Grid.point(0, 30))
    with cece.Model(["megan3"], grid=cece.Grid.point(0, 30)) as model:
        ds = xr.Dataset({"temperature": ("column", [303.0], {"units": "bananas"})})
        with pytest.raises(ValueError, match="units"):
            model.set_inputs(ds)


def test_profile_axis_is_preserved():
    grid = cece.Grid.columns([0, 1], [30, 31], levels=[1000.0, 500.0, 100.0])
    forcing = xr.DataArray(
        np.arange(6.0).reshape(3, 2),
        dims=("level", "column"),
        coords={"level": [1000.0, 500.0, 100.0]},
        attrs={"units": "kg m-2 s-1"},
    )
    with cece.Model(["native_example"], grid=grid) as model:
        model.set_inputs({"base_anthropogenic_nox": forcing})
        result = model.step(time=0, dt=1)
        np.testing.assert_array_equal(
            result.nox.values, forcing.transpose("column", "level").values * 2
        )


def test_invalid_update_preserves_previous_forcing():
    with cece.Model(["native_example"], grid=cece.Grid.point(0, 0)) as model:
        model.set_inputs({"base_anthropogenic_nox": 3.0})
        with pytest.raises(ValueError, match="non-finite"):
            model.set_inputs({
                "base_anthropogenic_nox": 4.0,
                "multiplier_input": np.nan,
            })
        assert model.step(time=0, dt=1).nox.item() == 6.0


def test_kernel_failure_poisoning_prevents_invalid_publication():
    with cece.Model(
        ["native_example"],
        grid=cece.Grid.point(0, 0),
        options={"native_example": {"multiplier": 1.0e308}},
    ) as model:
        model.set_inputs({"base_anthropogenic_nox": 1.0e308})
        with pytest.raises(cece.CeceException, match="Non-finite.*output"):
            model.step(time=0, dt=1)
        with pytest.raises(cece.CeceException, match="failed"):
            model.step(time=0, dt=1)


def test_numpy_coordinates_make_json_restart_and_options_are_snapshots():
    grid = cece.Grid.columns(
        np.array([0, 1]), np.array([30, 31]), levels=np.array([1000, 500])
    )
    with cece.Model(
        ["native_example"], grid=grid, options={"native_example": {"multiplier": 3.0}}
    ) as model:
        model.set_inputs({"base_anthropogenic_nox": np.ones((2, 2))})
        exposed = model.options
        exposed["native_example"]["multiplier"] = 99.0
        snapshot = json.loads(json.dumps(model.state_dict()))
        assert snapshot["options"]["native_example"]["multiplier"] == 3.0
        assert model.step(time=0, dt=1).nox.values[0, 0] == 3.0


def test_caller_units_survive_physics_and_restart():
    grid = cece.Grid.point(0, 0)
    source = xr.DataArray([3.0], dims=['column'], attrs={'units': 'kg m-2 s-1'})
    with cece.Model(['native_example'], grid=grid) as model:
        model.set_inputs({'base_anthropogenic_nox': source})
        assert model.step(time=0, dt=1).nox.attrs['units'] == 'kg m-2 s-1'
        state = model.state_dict()
        with cece.Model(['native_example'], grid=grid) as restored:
            restored.load_state_dict(state)
            assert restored.step(time=1, dt=1).nox.attrs['units'] == 'kg m-2 s-1'
