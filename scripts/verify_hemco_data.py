#!/usr/bin/env python3
import argparse
import os
import sys

try:
    import netCDF4 as nc
except ImportError:
    print("netCDF4 not found. Falling back to basic file existence check.")
    nc = None

def verify_netcdf(filepath, expected_vars=None):
    """
    Check if a file is a valid NetCDF and optionally contains certain variables.
    """
    if not os.path.exists(filepath):
        print(f"Error: File {filepath} does not exist.")
        return False

    if nc is None:
        # Fallback to a basic check for now
        return os.path.getsize(filepath) > 0

    try:
        with nc.Dataset(filepath, 'r') as ds:
            if expected_vars:
                for var in expected_vars:
                    if var not in ds.variables:
                        print(f"Error: Variable {var} missing from {filepath}.")
                        return False
        return True
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description="Verify NetCDF data validity.")
    parser.add_argument("filepath", help="Path to the NetCDF file to verify.")
    parser.add_argument("--vars", nargs="+", help="List of expected variables.")

    args = parser.parse_args()

    if verify_netcdf(args.filepath, args.vars):
        print(f"Verification successful for {args.filepath}.")
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
