#!/usr/bin/env python3
"""
Scan catcam-training/ images in S3 and delete any that are corrupt/undecodeable.

Usage:
    AWS_PROFILE=nakom.is-sandbox python3 scripts/clean-corrupt-images.py [--dry-run]

Options:
    --dry-run   List corrupt images without deleting them.
"""

import argparse
import io
import struct
import sys
import boto3

BUCKET = "bootboots-images-975050268859-eu-west-2"
PREFIX = "catcam-training/"


def is_valid_jpeg(data: bytes) -> bool:
    """Return False if data is not a complete, decodeable JPEG."""
    if len(data) < 4:
        return False
    # JPEG starts with FF D8 and ends with FF D9
    if data[:2] != b"\xff\xd8":
        return False
    if data[-2:] != b"\xff\xd9":
        return False
    # Try to parse with Pillow for a deeper check
    try:
        from PIL import Image
        img = Image.open(io.BytesIO(data))
        img.load()  # force full decode
    except Exception:
        return False
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true",
                        help="List corrupt images without deleting")
    args = parser.parse_args()

    s3 = boto3.client("s3")
    paginator = s3.get_paginator("list_objects_v2")

    total = 0
    corrupt = 0
    deleted = 0

    print(f"Scanning s3://{BUCKET}/{PREFIX} ...")
    for page in paginator.paginate(Bucket=BUCKET, Prefix=PREFIX):
        for obj in page.get("Contents", []):
            key = obj["Key"]
            if not key.lower().endswith((".jpg", ".jpeg", ".png")):
                continue
            total += 1

            response = s3.get_object(Bucket=BUCKET, Key=key)
            data = response["Body"].read()

            if not is_valid_jpeg(data):
                corrupt += 1
                if args.dry_run:
                    print(f"[CORRUPT] {key}  ({len(data)} bytes)")
                else:
                    s3.delete_object(Bucket=BUCKET, Key=key)
                    print(f"[DELETED] {key}  ({len(data)} bytes)")
                    deleted += 1

    print(f"\nDone. Scanned {total} images, found {corrupt} corrupt.")
    if not args.dry_run:
        print(f"Deleted {deleted} corrupt images from S3.")
    else:
        print("Dry-run mode — nothing deleted. Re-run without --dry-run to delete.")


if __name__ == "__main__":
    main()
