#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys

def download_s3(s3_path, local_path):
    """
    Download a file or directory from S3 using the AWS CLI or `curl`.
    """
    # HEMCO data is on an anonymous public S3 bucket: s3://geos-chem
    # URL access is also possible: https://geos-chem.s3.amazonaws.com/

    if s3_path.startswith("s3://"):
        # Strip s3:// and the bucket name if it's there
        # Expected formats: s3://geos-chem/path/to/file or s3://path/to/file
        relative_path = s3_path[5:]
        if relative_path.startswith("geos-chem/"):
            relative_path = relative_path[10:]
        url = f"https://geos-chem.s3.amazonaws.com/{relative_path}"
    else:
        url = f"https://geos-chem.s3.amazonaws.com/{s3_path}"

    print(f"Downloading {url} to {local_path}...")

    # Create the destination directory if it doesn't exist
    os.makedirs(os.path.dirname(os.path.abspath(local_path)), exist_ok=True)

    # Using curl/wget is more portable for anonymous access than the aws-cli which needs config.
    try:
        subprocess.check_call(["curl", "-L", "-o", local_path, url])
    except subprocess.CalledProcessError as e:
        print(f"Error downloading data: {e}", file=sys.stderr)
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Download HEMCO data from AWS S3.")
    parser.add_argument("path", help="The S3 path to download (e.g., MACCity/MACCity.nc)")
    parser.add_argument("-o", "--output", help="The local output file path.")

    args = parser.parse_args()

    output_path = args.output if args.output else os.path.basename(args.path)
    download_s3(args.path, output_path)

if __name__ == "__main__":
    main()
