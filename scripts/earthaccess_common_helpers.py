"""
Helper functions common to earthaccess download scripts.

stage_earthaccess_streams and cece_earthaccess_standalone_ingest
"""

import os
from typing import Any


def _effective_provider(stream: dict) -> Any:
    daac = stream.get("daac")
    if stream.get("cloud_hosted", True) and daac == "LPDAAC_ECS":
        return "LPCLOUD"
    return daac


def _auth_strategy() -> str:
    return os.getenv("CECE_EARTHACCESS_AUTH_STRATEGY", "all")


def _prepare_auth_environment(strategy: str) -> None:
    if strategy == "netrc":
        os.environ.pop("EARTHDATA_TOKEN", None)


def _search_granules(
    earthaccess: Any, stream: dict, provider: Any, count=-1
) -> list[Any]:
    try:
        return earthaccess.search_data(
            short_name=stream["short_name"],
            temporal=(stream["temporal_start"], stream["temporal_end"]),
            bounding_box=stream.get("bounding_box"),
            version=stream.get("version"),
            cloud_hosted=stream.get("cloud_hosted", True),
            provider=provider,
            count=count,
        )
    except RuntimeError as exc:
        if "Token does not exist" in str(exc):
            raise RuntimeError(
                "NASA CMR rejected a stale or invalid EARTHDATA_TOKEN. Unset "
                "EARTHDATA_TOKEN and use --auth-strategy netrc with a ~/.netrc entry "
                "whose password field contains the Earthdata account password, or export "
                "a newly generated valid Earthdata token."
            ) from exc
        raise


def _granule_data_links(granule: Any) -> list[str]:
    data_links = getattr(granule, "data_links", None)
    if callable(data_links):
        return [str(link) for link in data_links()]
    return [str(link) for link in getattr(granule, "data_links", []) or []]


def _first_data_link(granules: list[Any]) -> str | None:
    for granule in granules:
        for link in _granule_data_links(granule):
            lowered = link.lower()
            if lowered.startswith(("http://", "https://", "s3://")):
                return link
    return None


def _is_hdf_eos_link(link: str | None) -> bool:
    if not link:
        return False
    lowered = link.lower().split("?", 1)[0]
    return lowered.endswith(".hdf")


def _raise_if_unsupported_granule_format(stream: dict, granules: list[Any]) -> None:
    first_link = _first_data_link(granules)
    if not _is_hdf_eos_link(first_link):
        return

    raise RuntimeError(
        "EarthAccess stream "
        f"{stream.get('name', '<unnamed>')!r} returned HDF-EOS/HDF4 granules, "
        "which CECE's current cloud extras cannot read with xarray+h5netcdf. "
        f"Example data link: {first_link}. Use a NetCDF4/HDF5-compatible Earthdata "
        "collection for this field, or pre-convert the HDF-EOS granules to NetCDF "
        "on a login/data-transfer node and configure CECE to read those local files."
    )
