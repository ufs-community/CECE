#!/usr/bin/env python3
"""Train an annual pollen-production RF and map it to a CECE grid."""

from __future__ import annotations

import argparse
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
import xarray as xr
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import GridSearchCV, train_test_split


DEFAULT_FEATURES = (
    "temperature_avg,temperature_max,temperature_min,wind_speed,precipitation,"
    "relative_humidity,sunshine_hours,altitude,pressure"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fit annual pollen production from observations and meteorology, then predict a gridded CECE input field."
    )
    parser.add_argument("training_csv", type=Path, help="Station/year training table")
    parser.add_argument(
        "predictor_netcdf", type=Path, help="Gridded predictor variables"
    )
    parser.add_argument(
        "output_netcdf", type=Path, help="Output RF annual-production map"
    )
    parser.add_argument(
        "--target",
        default="annual_pollen_production",
        help="Training target column [grains m-2 yr-1]",
    )
    parser.add_argument(
        "--taxon", required=True, help="Taxon label stored in output metadata"
    )
    parser.add_argument(
        "--features",
        default=DEFAULT_FEATURES,
        help="Comma-separated training columns and NetCDF variables",
    )
    parser.add_argument(
        "--model-output",
        type=Path,
        help="Optional joblib path for the fitted estimator",
    )
    parser.add_argument("--random-state", type=int, default=42)
    parser.add_argument(
        "--training-source",
        required=True,
        help="Observation dataset and version used to derive the target",
    )
    parser.add_argument(
        "--meteorology-source",
        default="NASA MERRA-2",
        help="Predictor dataset stored in output provenance",
    )
    parser.add_argument("--year", type=int, default=2025)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    features = [name.strip() for name in args.features.split(",") if name.strip()]
    training = pd.read_csv(args.training_csv).dropna(subset=[*features, args.target])
    if len(training) < 10:
        raise ValueError("At least 10 complete station/year records are required")

    train_x, test_x, train_y, test_y = train_test_split(
        training[features],
        training[args.target],
        test_size=0.2,
        random_state=args.random_state,
    )
    search = GridSearchCV(
        RandomForestRegressor(random_state=args.random_state, n_jobs=-1),
        {
            "n_estimators": [200, 500],
            "max_depth": [None, 12, 24],
            "min_samples_split": [2, 5],
            "min_samples_leaf": [1, 2],
        },
        cv=5,
        scoring="neg_root_mean_squared_error",
        n_jobs=-1,
    )
    search.fit(train_x, train_y)
    model = search.best_estimator_

    predictors = xr.open_dataset(args.predictor_netcdf)
    missing = [name for name in features if name not in predictors]
    if missing:
        raise KeyError(f"Predictor NetCDF is missing variables: {', '.join(missing)}")
    broadcast = xr.broadcast(*(predictors[name] for name in features))
    valid = np.logical_and.reduce([np.isfinite(field.values) for field in broadcast])
    matrix = pd.DataFrame(
        np.column_stack([field.values[valid] for field in broadcast]), columns=features
    )
    prediction = np.full(broadcast[0].shape, np.nan, dtype=np.float64)
    prediction[valid] = np.maximum(0.0, model.predict(matrix))

    annual = xr.DataArray(
        prediction,
        coords=broadcast[0].coords,
        dims=broadcast[0].dims,
        name="annual_pollen_production",
    )
    if "time" not in annual.dims:
        annual = annual.expand_dims(time=[np.datetime64(f"{args.year}-01-01")])
    annual.attrs.update(
        units="grains m-2 yr-1",
        long_name=f"random_forest_annual_{args.taxon}_pollen_production",
        taxon=args.taxon,
        rf_test_score_r2=float(model.score(test_x, test_y)),
        rf_features=",".join(features),
        training_source=args.training_source,
        meteorology_source=args.meteorology_source,
        climatology_year=args.year,
        source="Li et al. (2025), doi:10.5194/acp-25-3583-2025",
    )
    args.output_netcdf.parent.mkdir(parents=True, exist_ok=True)
    annual.to_dataset().to_netcdf(args.output_netcdf)
    if args.model_output:
        args.model_output.parent.mkdir(parents=True, exist_ok=True)
        joblib.dump(model, args.model_output)


if __name__ == "__main__":
    main()
