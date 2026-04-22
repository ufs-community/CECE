#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys


def download_s3(s3_path, local_path):
    """
    Download a file from the public geos-chem S3 bucket using HTTPS.
    """
    bucket = "geos-chem"
    base_url = f"https://{bucket}.s3.amazonaws.com"

    clean_path = s3_path
    if clean_path.startswith("s3://"):
        clean_path = clean_path[5:]

    if clean_path.startswith(f"{bucket}/"):
        clean_path = clean_path[len(bucket) + 1 :]

    url = f"{base_url}/{clean_path}"

    print(f"Downloading {url} to {local_path}...")

    target_dir = os.path.dirname(os.path.abspath(local_path))
    if target_dir:
        os.makedirs(target_dir, exist_ok=True)

    try:
        subprocess.check_call(["curl", "-L", "-f", "-o", local_path, url])
    except subprocess.CalledProcessError as e:
        print(f"Error downloading data: {e}", file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="Download HEMCO data from AWS S3.")
    parser.add_argument(
        "path",
        help="The S3 path to download (e.g., HEMCO/MACCITY/v2014-07/MACCity_4x5.nc)",
    )
    parser.add_argument("-o", "--output", help="The local output file path.")
    args = parser.parse_args()
    output_path = args.output if args.output else os.path.basename(args.path)
    download_s3(args.path, output_path)


if __name__ == "__main__":
    main()
