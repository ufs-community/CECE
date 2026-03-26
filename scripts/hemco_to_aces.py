#!/usr/bin/env python3
"""
hemco_to_aces.py - Convert HEMCO_Config.rc to ACES YAML configuration.

Supports all HEMCO features:
  - Hierarchy levels and categories (anthropogenic, biogenic, biomass burning, natural, aircraft)
  - Scale factor types (constant, time-varying, spatially-varying)
  - Mask types (2D geographical, 3D volumetric)
  - Operations (1=replace, 2=add)
  - Vertical distribution methods (SINGLE, RANGE, PRESSURE, HEIGHT, PBL)
  - Temporal profiles (diurnal 24h, day-of-week 7d, seasonal 12m)
  - HEMCO_Diagn.rc diagnostic conversion
"""

import yaml
import argparse
import os
import re

# ---------------------------------------------------------------------------
# HEMCO category integer -> ACES string label
# ---------------------------------------------------------------------------
HEMCO_CATEGORY_MAP = {
    "1": "anthropogenic",
    "2": "biogenic",
    "3": "biomass_burning",
    "4": "natural",
    "5": "aircraft",
    "6": "ship",
    "7": "soil",
    "8": "lightning",
    "9": "volcano",
    "10": "other",
}

# HEMCO operation codes
HEMCO_OP_ADD = "2"
HEMCO_OP_REPLACE = "1"

# Vertical distribution keyword patterns in HEMCO field names / comments
VDIST_KEYWORDS = {
    "PBL": "pbl",
    "PRESSURE": "pressure",
    "HEIGHT": "height",
    "RANGE": "range",
    "SINGLE": "single",
}


class HemcoParser:
    """Parses a HEMCO_Config.rc file into structured Python dicts."""

    def __init__(self, hemco_config_path):
        self.config_path = os.path.abspath(hemco_config_path)
        self.base_dir = os.path.dirname(self.config_path)
        self.settings = {}
        self.base_emissions = []
        self.scale_factors = {}
        self.masks = {}
        self.extensions = {}
        self.parse()

    def strip_comments(self, line):
        if "#" in line:
            return line[: line.find("#")].strip()
        return line.strip()

    def load_lines(self, filepath, current_base_dir):
        if not os.path.isabs(filepath):
            filepath = os.path.join(current_base_dir, filepath)
        if not os.path.exists(filepath):
            print(f"Warning: file {filepath} not found.")
            return []
        new_base_dir = os.path.dirname(filepath)
        all_lines = []
        with open(filepath, "r") as f:
            for line in f:
                stripped = line.strip()
                if stripped.startswith(">>>include"):
                    inc_file = stripped.split()[-1]
                    all_lines.extend(self.load_lines(inc_file, new_base_dir))
                else:
                    all_lines.append(line)
        return all_lines

    def parse(self):
        lines = self.load_lines(self.config_path, self.base_dir)
        if not lines:
            return

        current_section = None
        for line in lines:
            raw_line = line.strip()
            stripped = self.strip_comments(raw_line)

            # Section detection (works on raw line including comments)
            upper = raw_line.upper()
            if "BEGIN SECTION SETTINGS" in upper:
                current_section = "SETTINGS"
                continue
            elif "BEGIN SECTION EXTENSION SWITCHES" in upper:
                current_section = "EXTENSIONS"
                continue
            elif "BEGIN SECTION BASE EMISSIONS" in upper:
                current_section = "BASE_EMISSIONS"
                continue
            elif "BEGIN SECTION SCALE FACTORS" in upper:
                current_section = "SCALE_FACTORS"
                continue
            elif "BEGIN SECTION MASKS" in upper:
                current_section = "MASKS"
                continue
            elif "END SECTION" in upper:
                current_section = None
                continue

            if not stripped:
                continue

            if current_section == "SETTINGS":
                if ":" in stripped:
                    key, value = stripped.split(":", 1)
                    self.settings[key.strip()] = value.strip()

            elif current_section == "EXTENSIONS":
                stripped = stripped.replace("-->", "").strip()
                if ":" in stripped:
                    parts = stripped.split(":")
                    name_part = parts[0].strip().split()
                    if not name_part:
                        continue
                    ext_name = name_part[-1]
                    status_raw = parts[1].strip().split()[0] if parts[1].strip() else "off"
                    status = status_raw.lower().rstrip(":")
                    nr = name_part[0] if len(name_part) > 1 else None
                    entry = {"status": status}
                    if nr and nr.isdigit():
                        entry["nr"] = nr
                    self.extensions[ext_name] = entry
                else:
                    parts = stripped.split()
                    if len(parts) >= 3:
                        ext_nr, ext_name, status = parts[0], parts[1], parts[2].rstrip(":")
                        self.extensions[ext_name] = {
                            "nr": ext_nr,
                            "status": status.lower(),
                        }

            elif current_section == "BASE_EMISSIONS":
                parts = stripped.split()
                if len(parts) >= 12:
                    entry = {
                        "ext_nr": parts[0],
                        "name": parts[1],
                        "file": parts[2],
                        "var": parts[3],
                        "time": parts[4],
                        "cre": parts[5],
                        "dim": parts[6],
                        "unit": parts[7],
                        "species": parts[8],
                        "scal_ids": parts[9],
                        "cat": parts[10],
                        "hier": parts[11],
                    }
                    # Optional vertical distribution column (col 13+)
                    if len(parts) >= 13:
                        entry["vdist"] = parts[12]
                    self.base_emissions.append(entry)

            elif current_section == "SCALE_FACTORS":
                parts = stripped.split()
                if len(parts) >= 9:
                    scal_id = parts[0]
                    self.scale_factors[scal_id] = {
                        "name": parts[1],
                        "file": parts[2],
                        "var": parts[3],
                        "time": parts[4],
                        "cre": parts[5],
                        "dim": parts[6],
                        "unit": parts[7],
                        "oper": parts[8],
                    }

            elif current_section == "MASKS":
                parts = stripped.split()
                if len(parts) >= 9:
                    scal_id = parts[0]
                    entry = {
                        "name": parts[1],
                        "file": parts[2],
                        "var": parts[3],
                        "time": parts[4],
                        "cre": parts[5],
                        "dim": parts[6],
                        "unit": parts[7],
                        "oper": parts[8],
                    }
                    if len(parts) > 9:
                        entry["box"] = parts[9]
                    self.masks[scal_id] = entry


class DiagnParser:
    """Parses a HEMCO_Diagn.rc file into structured diagnostic entries."""

    def __init__(self, diagn_config_path):
        self.config_path = diagn_config_path
        self.variables = []
        self.parse()

    def parse(self):
        if not os.path.exists(self.config_path):
            return
        with open(self.config_path, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                # Format: Name Spec ExtNr Cat Hier Dim Unit LongName...
                if len(parts) >= 7:
                    entry = {
                        "name": parts[0],
                        "species": parts[1],
                        "ext_nr": parts[2],
                        "cat": parts[3],
                        "hier": parts[4],
                        "dim": parts[5],
                        "unit": parts[6],
                        "long_name": " ".join(parts[7:]) if len(parts) > 7 else parts[0],
                    }
                    self.variables.append(entry)
                elif len(parts) >= 1:
                    self.variables.append({"name": parts[0], "long_name": parts[0]})


class GridParser:
    def __init__(self, grid_config_path):
        self.config_path = grid_config_path
        self.grid_params = {}
        self.parse()

    def parse(self):
        if not os.path.exists(self.config_path):
            return
        with open(self.config_path, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if ":" in line:
                    key, value = line.split(":", 1)
                    self.grid_params[key.strip().upper()] = value.strip()


def _resolve_path(raw_path, root_val):
    """Expand $ROOT and $root placeholders in HEMCO file paths."""
    path = raw_path.replace("$ROOT/", root_val + "/").replace("$ROOT", root_val)
    path = path.replace("$root/", root_val + "/").replace("$root", root_val)
    return path


def _parse_temporal_factors(file_field, var_field):
    """
    Detect inline temporal profiles encoded as slash-separated floats in the
    file column (HEMCO convention for constant scale factors).
    Returns (factors_list, cycle_type) or (None, None).
    cycle_type is one of: 'diurnal' (24), 'weekly' (7), 'seasonal' (12), or None.
    """
    if "/" not in file_field or var_field != "-":
        return None, None
    try:
        values = [float(x) for x in file_field.split("/")]
    except ValueError:
        return None, None
    if len(values) == 24:
        return values, "diurnal"
    if len(values) == 7:
        return values, "weekly"
    if len(values) == 12:
        return values, "seasonal"
    return values, "custom"


def _infer_vdist(be_entry):
    """
    Infer vertical distribution from HEMCO base emission entry.
    Returns a dict suitable for the ACES layer 'vdist' key, or None for surface.
    """
    vdist_col = be_entry.get("vdist", "")
    dim = be_entry.get("dim", "2")

    if not vdist_col or vdist_col == "-":
        # 2D field -> surface only (SINGLE layer 0)
        if dim.startswith("2"):
            return None
        return None

    upper = vdist_col.upper()
    if upper.startswith("PBL"):
        return {"method": "pbl"}
    if upper.startswith("P"):
        # e.g. P(100,900) -> pressure range in hPa, convert to Pa
        m = re.match(r"P\(([0-9.]+),([0-9.]+)\)", vdist_col, re.IGNORECASE)
        if m:
            p_start = float(m.group(1)) * 100.0  # hPa -> Pa
            p_end = float(m.group(2)) * 100.0
            return {"method": "pressure", "p_start": p_start, "p_end": p_end}
        return {"method": "pressure"}
    if upper.startswith("H"):
        # e.g. H(0,2000) -> height range in m
        m = re.match(r"H\(([0-9.]+),([0-9.]+)\)", vdist_col, re.IGNORECASE)
        if m:
            return {"method": "height", "h_start": float(m.group(1)), "h_end": float(m.group(2))}
        return {"method": "height"}
    if upper.startswith("L"):
        # e.g. L(1,5) -> layer range (0-indexed in ACES)
        m = re.match(r"L\(([0-9]+),([0-9]+)\)", vdist_col, re.IGNORECASE)
        if m:
            return {
                "method": "range",
                "layer_start": int(m.group(1)) - 1,
                "layer_end": int(m.group(2)) - 1,
            }
        # Single layer e.g. L(3)
        m = re.match(r"L\(([0-9]+)\)", vdist_col, re.IGNORECASE)
        if m:
            return {"method": "single", "layer_start": int(m.group(1)) - 1}
        return {"method": "range"}
    return None


def convert_hemco_to_aces(hemco_config_path, output_path, diagn_path=None):
    """
    Convert a HEMCO_Config.rc (and optionally HEMCO_Diagn.rc) to an ACES YAML config.

    Args:
        hemco_config_path: Path to HEMCO_Config.rc
        output_path: Path for the output ACES YAML file
        diagn_path: Optional path to HEMCO_Diagn.rc
    """
    parser = HemcoParser(hemco_config_path)
    base_dir = parser.base_dir
    root_val = parser.settings.get("ROOT", "data")

    aces_config = {
        "meteorology": {},
        "scale_factors": {},
        "masks": {},
        "temporal_profiles": {},
        "species": {},
        "aces_data": {"streams": []},
    }

    # ------------------------------------------------------------------
    # Identify enabled extension numbers
    # ------------------------------------------------------------------
    enabled_ext_nrs = {"0"}
    for name, info in parser.extensions.items():
        if info.get("status") in ("on", "true", "yes", "1"):
            if "nr" in info:
                enabled_ext_nrs.add(info["nr"])

    # ------------------------------------------------------------------
    # Process base emissions
    # ------------------------------------------------------------------
    streams = {}  # name -> resolved file path

    for be in parser.base_emissions:
        ext_nr = be["ext_nr"]
        if ext_nr != "0" and ext_nr not in enabled_ext_nrs:
            # Check if any extension with this nr is enabled
            is_enabled = any(
                info.get("nr") == ext_nr and info.get("status") in ("on", "true", "yes", "1")
                for info in parser.extensions.values()
            )
            if not is_enabled:
                continue

        species_name = be["species"].lower()
        if species_name == "*":
            continue

        if species_name not in aces_config["species"]:
            aces_config["species"][species_name] = []

        # Map HEMCO operation: 1=replace, 2=add (HEMCO default is add for hier=1)
        hier = int(be["hier"])
        oper_code = be.get("cre", "2")  # 'cre' column sometimes holds operation
        # HEMCO convention: hierarchy > 1 with replace operation overrides lower
        operation = "replace" if hier > 1 else "add"

        # Map category
        cat_str = HEMCO_CATEGORY_MAP.get(be["cat"], be["cat"])

        layer = {
            "field": be["name"],
            "operation": operation,
            "scale": 1.0,
            "category": cat_str,
            "hierarchy": hier,
        }

        # ------------------------------------------------------------------
        # Vertical distribution
        # ------------------------------------------------------------------
        vdist = _infer_vdist(be)
        if vdist:
            layer["vdist"] = vdist

        # ------------------------------------------------------------------
        # Scale IDs -> masks, scale_fields, temporal profiles
        # ------------------------------------------------------------------
        scal_ids = be["scal_ids"].split("/")
        layer_masks = []
        layer_scale_fields = []

        for sid in scal_ids:
            if sid in ("-", ""):
                continue

            if sid in parser.masks:
                mask_entry = parser.masks[sid]
                mask_name = mask_entry["name"].lower()
                layer_masks.append(mask_name)
                aces_config["masks"][mask_name] = mask_entry["name"]
                if mask_entry["file"] not in ("-", ""):
                    streams[mask_name] = _resolve_path(mask_entry["file"], root_val)

            elif sid in parser.scale_factors:
                sf_entry = parser.scale_factors[sid]
                sf_name = sf_entry["name"]

                # Detect inline temporal profiles (slash-separated floats)
                factors, cycle_type = _parse_temporal_factors(sf_entry["file"], sf_entry["var"])
                if factors is not None:
                    cycle_key = sf_name.lower()
                    aces_config["temporal_profiles"][cycle_key] = factors
                    if cycle_type == "diurnal":
                        layer["diurnal_cycle"] = cycle_key
                    elif cycle_type == "weekly":
                        layer["weekly_cycle"] = cycle_key
                    elif cycle_type == "seasonal":
                        layer["seasonal_cycle"] = cycle_key
                    # custom length profiles stored but not auto-assigned
                else:
                    sf_key = sf_name.lower()
                    layer_scale_fields.append(sf_key)
                    aces_config["scale_factors"][sf_key] = sf_name
                    if sf_entry["file"] not in ("-", ""):
                        streams[sf_key] = _resolve_path(sf_entry["file"], root_val)

        if layer_masks:
            layer["mask"] = layer_masks if len(layer_masks) > 1 else layer_masks[0]
        if layer_scale_fields:
            layer["scale_fields"] = layer_scale_fields

        aces_config["species"][species_name].append(layer)

        # Register base emission file as a TIDE stream
        if be["file"] not in ("-", ""):
            streams[be["name"]] = _resolve_path(be["file"], root_val)

    # ------------------------------------------------------------------
    # Build TIDE streams list
    # ------------------------------------------------------------------
    for name, file_path in streams.items():
        aces_config["aces_data"]["streams"].append(
            {"name": name, "file": file_path}
        )

    # ------------------------------------------------------------------
    # Separate met fields from scale factors
    # ------------------------------------------------------------------
    for sf_id, sf in parser.scale_factors.items():
        sf_key = sf["name"].lower()
        if "met" in sf_key or sf["file"] == "-":
            aces_config["meteorology"][sf_key] = sf["name"]
            aces_config["scale_factors"].pop(sf_key, None)

    # ------------------------------------------------------------------
    # Diagnostics
    # ------------------------------------------------------------------
    diagnostics = {}

    # Parse grid file if referenced
    grid_file = parser.settings.get("GridFile")
    if grid_file:
        full_grid_path = (
            os.path.join(base_dir, grid_file) if not os.path.isabs(grid_file) else grid_file
        )
        grid_parser = GridParser(full_grid_path)
        if grid_parser.grid_params:
            diagnostics["grid_type"] = "gaussian"
            if "NLON" in grid_parser.grid_params:
                diagnostics["nx"] = int(grid_parser.grid_params["NLON"])
            if "NLAT" in grid_parser.grid_params:
                diagnostics["ny"] = int(grid_parser.grid_params["NLAT"])

    # Parse output interval from DiagnFreq setting
    diag_freq = parser.settings.get("DiagnFreq", "00000000 010000")
    try:
        parts = diag_freq.split()
        if len(parts) == 2:
            time_str = parts[1]
            h = int(time_str[0:2])
            m = int(time_str[2:4])
            s = int(time_str[4:6])
            diagnostics["output_interval"] = h * 3600 + m * 60 + s
        else:
            diagnostics["output_interval"] = 3600
    except Exception:
        diagnostics["output_interval"] = 3600

    # Parse HEMCO_Diagn.rc if provided or auto-discovered
    diagn_file = diagn_path or parser.settings.get("DiagnFile")
    if diagn_file:
        full_diagn_path = (
            os.path.join(base_dir, diagn_file) if not os.path.isabs(diagn_file) else diagn_file
        )
        diagnostics.update(convert_hemco_diagn(full_diagn_path))

    if diagnostics:
        aces_config["diagnostics"] = diagnostics

    with open(output_path, "w") as f:
        yaml.dump(aces_config, f, sort_keys=False, default_flow_style=False)


def convert_hemco_diagn(diagn_path):
    """
    Convert a HEMCO_Diagn.rc file to an ACES diagnostics configuration dict.

    Maps HEMCO diagnostic specifications to ACES YAML format, preserving
    all metadata: units, long names, and output frequency.

    Args:
        diagn_path: Path to HEMCO_Diagn.rc

    Returns:
        dict with 'variables' list and optional metadata fields.
    """
    diagn_parser = DiagnParser(diagn_path)
    if not diagn_parser.variables:
        return {}

    result = {}
    variables = []
    for var in diagn_parser.variables:
        entry = {"name": var["name"]}
        if "species" in var and var["species"] not in ("-", ""):
            entry["species"] = var["species"]
        if "unit" in var and var["unit"] not in ("-", ""):
            entry["units"] = var["unit"]
        if "long_name" in var and var["long_name"] != var["name"]:
            entry["long_name"] = var["long_name"]
        if "dim" in var:
            entry["dim"] = int(var["dim"]) if var["dim"].isdigit() else 2
        variables.append(entry)

    result["variables"] = variables
    return result


if __name__ == "__main__":
    arg_parser = argparse.ArgumentParser(
        description="Convert HEMCO configuration to ACES configuration."
    )
    arg_parser.add_argument("hemco_config", help="Path to the HEMCO configuration file (.rc)")
    arg_parser.add_argument(
        "-o",
        "--output",
        default="aces_config.yaml",
        help="Path to the output ACES configuration file (default: aces_config.yaml)",
    )
    arg_parser.add_argument(
        "--diagn",
        default=None,
        help="Path to HEMCO_Diagn.rc for diagnostic conversion (optional)",
    )
    args = arg_parser.parse_args()
    convert_hemco_to_aces(args.hemco_config, args.output, diagn_path=args.diagn)
    print(f"Successfully converted {args.hemco_config} to {args.output}")
