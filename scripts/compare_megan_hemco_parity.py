#!/usr/bin/env python3
"""
compare_megan_hemco_parity.py
─────────────────────────────
Compare CECE MEGAN / MEGAN3 isoprene outputs against the HEMCO 3.12.1
stateless parity reference on the global 4°×5° grid.

Usage
─────
  python scripts/compare_megan_hemco_parity.py \\
      --hemco   cece_hemco_megan_parity_4x5.nc \\
      --native  cece_megan_comparison.nc \\
      --megan3  cece_megan3_hemco_comparison.nc \\
      --outdir  plots/

Outputs (written to --outdir)
──────────────────────────────
  global_isop_maps.png          — side-by-side global emission maps
  isop_bias_native_vs_hemco.png — (native − HEMCO3121) bias map
  isop_bias_megan3_vs_hemco.png — (MEGAN3  − HEMCO3121) bias map
  isop_zonal_mean.png           — zonal-mean comparison
  isop_scatter_native.png       — scatter: native vs HEMCO3121
  isop_scatter_megan3.png       — scatter: MEGAN3  vs HEMCO3121
  summary_stats.txt             — global mean, RMSE, bias, spatial correlation
"""

import argparse
import os
import textwrap
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import numpy as np

# Optional netCDF4 dependency; fall back to a lightweight stub if absent.
try:
    import netCDF4 as nc  # type: ignore

    HAS_NC4 = True
except ImportError:
    HAS_NC4 = False


# ── helpers ─────────────────────────────────────────────────────────────────


def read_field(path: str, varname: str, time_idx: int = -1) -> np.ndarray:
    """Return a 2-D (lat × lon) slice from a NetCDF file.

    Averages over the time dimension if multiple timesteps are present.
    """
    if not HAS_NC4:
        raise ImportError(
            "netCDF4 is required to read model output. "
            "Install it with:  pip install netCDF4"
        )
    with nc.Dataset(path) as ds:  # type: ignore[union-attr]
        if varname not in ds.variables:
            raise KeyError(
                f"Variable '{varname}' not found in {path}. "
                f"Available: {list(ds.variables)}"
            )
        data = ds.variables[varname][:]
        # Squeeze trivial dimensions; handle (time, lat, lon) or (lat, lon).
        data = np.squeeze(data)
        if data.ndim == 3:
            data = np.nanmean(data, axis=0)
        return np.asarray(data, dtype=np.float64)


def lons_lats_4x5():
    """Return (lon_centres, lat_centres) for the HEMCO 4°×5° grid."""
    lons = np.arange(-180.0, 180.0, 5.0) + 2.5  # 72 points
    lats = np.arange(-90.0, 92.0, 4.0) - 2.0  # 46 points
    return lons, lats


def kg_to_mgC(arr: np.ndarray, mw_isop: float = 68.12) -> np.ndarray:
    """Convert kg[isop] m⁻² s⁻¹ → mg[C] m⁻² hr⁻¹ (HEMCO diagnostic convention)."""
    # 1 mol C5H8 = 5 mol C; molar mass isoprene = 68.12 g/mol, C = 12.011 g/mol
    carbon_fraction = 5.0 * 12.011 / mw_isop
    return arr * 1.0e6 * 3600.0 * carbon_fraction  # kg/m²/s → mg C/m²/hr


# ── plotting ────────────────────────────────────────────────────────────────


def _map_ax(
    ax,
    data,
    lons,
    lats,
    title,
    units,
    vmin=None,
    vmax=None,
    cmap="YlOrRd",
    diverging=False,
):
    """Filled-contour global map on a simple cylindrical projection."""
    if diverging:
        cmap = "RdBu_r"
        amax = np.nanpercentile(np.abs(data), 99)
        vmin, vmax = -amax, amax
    lon2d, lat2d = np.meshgrid(lons, lats)
    cf = ax.contourf(lon2d, lat2d, data, levels=20, cmap=cmap, vmin=vmin, vmax=vmax)
    ax.set_title(title, fontsize=9)
    ax.set_xlabel("Longitude [°]")
    ax.set_ylabel("Latitude [°]")
    ax.set_xlim(-180, 180)
    ax.set_ylim(-90, 90)
    plt.colorbar(
        cf, ax=ax, orientation="horizontal", pad=0.12, label=units, fraction=0.046
    )


def plot_global_maps(hemco, native, megan3, lons, lats, outdir):
    units = "mg C m⁻² hr⁻¹"
    fig, axes = plt.subplots(1, 3, figsize=(18, 5))
    vmax = np.nanpercentile(np.concatenate([hemco.ravel(), native.ravel()]), 99)
    _map_ax(axes[0], hemco, lons, lats, "HEMCO 3.12.1 reference", units, 0, vmax)
    _map_ax(axes[1], native, lons, lats, "CECE native MEGAN", units, 0, vmax)
    _map_ax(axes[2], megan3, lons, lats, "CECE MEGAN3 ISOP", units, 0, vmax)
    fig.suptitle(
        "MEGAN isoprene emission comparison — 2021-06-20 daily mean", fontsize=11
    )
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "global_isop_maps.png"), dpi=150)
    plt.close(fig)


def plot_bias(diff, lons, lats, label, fname, outdir):
    fig, ax = plt.subplots(figsize=(10, 5))
    _map_ax(ax, diff, lons, lats, label, "mg C m⁻² hr⁻¹", diverging=True)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, fname), dpi=150)
    plt.close(fig)


def plot_zonal(hemco, native, megan3, lats, outdir):
    fig, ax = plt.subplots(figsize=(7, 5))
    ax.plot(np.nanmean(hemco, axis=1), lats, label="HEMCO 3.12.1", color="k", lw=2)
    ax.plot(
        np.nanmean(native, axis=1),
        lats,
        label="CECE native",
        color="#1f77b4",
        lw=1.5,
        ls="--",
    )
    ax.plot(
        np.nanmean(megan3, axis=1),
        lats,
        label="CECE MEGAN3 ISOP",
        color="#d62728",
        lw=1.5,
        ls=":",
    )
    ax.set_xlabel("mg C m⁻² hr⁻¹")
    ax.set_ylabel("Latitude [°]")
    ax.set_title("Zonal-mean isoprene emission")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "isop_zonal_mean.png"), dpi=150)
    plt.close(fig)


def plot_scatter(ref, test, label, fname, outdir):
    mask = np.isfinite(ref) & np.isfinite(test) & (ref > 0)
    x, y = ref[mask], test[mask]
    fig, ax = plt.subplots(figsize=(6, 6))
    h = ax.hist2d(x, y, bins=80, norm=mcolors.LogNorm(), cmap="viridis")
    plt.colorbar(h[3], ax=ax, label="Cell count")
    lim = max(x.max(), y.max()) * 1.05
    ax.plot([0, lim], [0, lim], "k--", lw=1, label="1:1")
    ax.set_xlabel("HEMCO 3.12.1 [mg C m⁻² hr⁻¹]")
    ax.set_ylabel(f"{label} [mg C m⁻² hr⁻¹]")
    ax.set_title(f"{label} vs HEMCO 3.12.1 — isoprene")
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, fname), dpi=150)
    plt.close(fig)


def summary_stats(hemco, test, label):
    mask = np.isfinite(hemco) & np.isfinite(test)
    h, t = hemco[mask], test[mask]
    bias = float(np.mean(t - h))
    rmse = float(np.sqrt(np.mean((t - h) ** 2)))
    corr = float(np.corrcoef(h, t)[0, 1]) if len(h) > 1 else float("nan")
    mean_h = float(np.mean(h))
    mean_t = float(np.mean(t))
    return {
        "label": label,
        "mean_ref": mean_h,
        "mean_test": mean_t,
        "bias": bias,
        "rmse": rmse,
        "corr": corr,
        "n_cells": int(mask.sum()),
    }


# ── main ────────────────────────────────────────────────────────────────────


def main():
    parser = argparse.ArgumentParser(
        description=textwrap.dedent(__doc__),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--hemco", required=True, help="NetCDF from cece_config_hemco_megan_parity.yaml"
    )
    parser.add_argument(
        "--native",
        required=True,
        help="NetCDF from cece_config_megan_hemco_comparison.yaml",
    )
    parser.add_argument(
        "--megan3",
        required=True,
        help="NetCDF from cece_config_megan3_hemco_comparison.yaml",
    )
    parser.add_argument("--hemco_var", default="isoprene_emissions")
    parser.add_argument("--native_var", default="isoprene_native")
    parser.add_argument("--megan3_var", default="ISOP")
    parser.add_argument(
        "--outdir", default="plots", help="Directory for output figures and summary"
    )
    args = parser.parse_args()

    Path(args.outdir).mkdir(parents=True, exist_ok=True)
    lons, lats = lons_lats_4x5()

    print(f"Reading HEMCO reference  : {args.hemco} [{args.hemco_var}]")
    hemco_raw = read_field(args.hemco, args.hemco_var)
    print(f"Reading CECE native MEGAN: {args.native} [{args.native_var}]")
    native_raw = read_field(args.native, args.native_var)
    print(f"Reading CECE MEGAN3 ISOP : {args.megan3} [{args.megan3_var}]")
    megan3_raw = read_field(args.megan3, args.megan3_var)

    # Convert to mg C m⁻² hr⁻¹ for plotting.
    hemco = kg_to_mgC(hemco_raw)
    native = kg_to_mgC(native_raw)
    megan3 = kg_to_mgC(megan3_raw)

    print("\nGenerating plots …")
    plot_global_maps(hemco, native, megan3, lons, lats, args.outdir)
    plot_bias(
        native - hemco,
        lons,
        lats,
        "CECE native − HEMCO 3.12.1",
        "isop_bias_native_vs_hemco.png",
        args.outdir,
    )
    plot_bias(
        megan3 - hemco,
        lons,
        lats,
        "CECE MEGAN3 − HEMCO 3.12.1",
        "isop_bias_megan3_vs_hemco.png",
        args.outdir,
    )
    plot_zonal(hemco, native, megan3, lats, args.outdir)
    plot_scatter(hemco, native, "CECE native", "isop_scatter_native.png", args.outdir)
    plot_scatter(hemco, megan3, "CECE MEGAN3", "isop_scatter_megan3.png", args.outdir)

    stats_native = summary_stats(hemco, native, "CECE native MEGAN")
    stats_megan3 = summary_stats(hemco, megan3, "CECE MEGAN3")

    summary_path = os.path.join(args.outdir, "summary_stats.txt")
    with open(summary_path, "w") as fh:
        fh.write("MEGAN isoprene parity — summary statistics\n")
        fh.write("=" * 50 + "\n")
        fh.write("Reference: HEMCO 3.12.1 stateless (megan_method=hemco_3_12_1)\n")
        fh.write("Grid:      global 4°×5° (72×46), 2021-06-20 daily mean\n")
        fh.write("Units:     mg C m⁻² hr⁻¹\n\n")
        for s in (stats_native, stats_megan3):
            fh.write(f"  {s['label']}\n")
            fh.write(f"    Mean reference : {s['mean_ref']:.4f}\n")
            fh.write(f"    Mean test      : {s['mean_test']:.4f}\n")
            fh.write(f"    Bias (test-ref): {s['bias']:+.4f}\n")
            fh.write(f"    RMSE           : {s['rmse']:.4f}\n")
            fh.write(f"    Spatial corr   : {s['corr']:.6f}\n")
            fh.write(f"    Grid cells     : {s['n_cells']}\n\n")

    print(f"\nSummary statistics written to {summary_path}")
    for s in (stats_native, stats_megan3):
        print(
            f"  {s['label']:30s}  bias={s['bias']:+.4f}  "
            f"rmse={s['rmse']:.4f}  corr={s['corr']:.4f}"
        )
    print(f"\nPlots saved to {args.outdir}/")


if __name__ == "__main__":
    main()
