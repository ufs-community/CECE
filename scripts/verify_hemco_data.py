#!/usr/bin/env python3
import argparse
import os
import sys


def verify_netcdf(filepath):
    """Basic NetCDF validity check (existence and non-zero size)."""
    if not os.path.exists(filepath):
        print(f"Error: File {filepath} does not exist.")
        return False
    if os.path.getsize(filepath) == 0:
        print(f"Error: File {filepath} is empty.")
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description="Verify NetCDF data validity.")
    parser.add_argument("filepath", help="Path to the file to verify.")
    args = parser.parse_args()
    if verify_netcdf(args.filepath):
        print(f"Verification successful for {args.filepath}.")
        sys.exit(0)
    else:
        sys.exit(1)


if __name__ == "__main__":
    main()
