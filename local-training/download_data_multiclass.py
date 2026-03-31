#!/usr/bin/env python3
"""
Download labeled cat images from DynamoDB/S3 for multiclass training.
Keeps each cat as its own class rather than collapsing to Boots/NotBoots.

Run with:
    AWS_PROFILE=nakom.is-sandbox python download_data_multiclass.py

Classes discovered automatically from DynamoDB labels.
Current classes: Boots, Chi, Tau, Kappa, Mu, Wolf, NoCat

Creates:
    data_multiclass/
      training/{Boots,Chi,Tau,...}/
      validation/{Boots,Chi,Tau,...}/
      manual_test/{Boots,Chi,Tau,...}/
      download_summary.json
"""

import argparse
import json
import random
from pathlib import Path

import boto3
from botocore.exceptions import ClientError

SOURCE_PREFIX = "catcam-training/"
TABLE_NAME = "catadata"
JPEG_HEADER = bytes([0xFF, 0xD8, 0xFF])
JPEG_EOI = bytes([0xFF, 0xD9])


def scan_dynamo(client, table_name: str) -> list[dict]:
    """Fetch all records from catadata that have a cat label."""
    records = []
    kwargs: dict = {
        "TableName": table_name,
        "FilterExpression": "attribute_exists(cat)",
    }
    while True:
        resp = client.scan(**kwargs)
        for item in resp.get("Items", []):
            image_name = item.get("imageName", {}).get("S")
            cat = item.get("cat", {}).get("S")
            if image_name and cat:
                records.append({"imageName": image_name, "cat": cat})
        last_key = resp.get("LastEvaluatedKey")
        if not last_key:
            break
        kwargs["ExclusiveStartKey"] = last_key
    return records


def download_image(s3_client, bucket: str, image_name: str, dest: Path) -> bool:
    """Download one image to dest. Skips if already present. Patches truncated JPEGs."""
    if dest.exists():
        return True

    key = image_name if image_name.startswith(SOURCE_PREFIX) else f"{SOURCE_PREFIX}{image_name}"

    try:
        resp = s3_client.get_object(Bucket=bucket, Key=key)
        data: bytes = resp["Body"].read()
    except ClientError as e:
        code = e.response["Error"]["Code"]
        if code in ("NoSuchKey", "404"):
            print(f"  [missing]  {key}")
        else:
            print(f"  [error]    {key}: {e}")
        return False

    if data[:3] != JPEG_HEADER:
        print(f"  [skip]     Not a JPEG: {key}")
        return False

    if data[-2:] != JPEG_EOI:
        data = data + JPEG_EOI

    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    return True


def split_class(records: list[dict], manual_pct: float, val_split: float, seed: int) -> dict:
    """Split records for one class into manual_test / validation / training."""
    rng = random.Random(seed)
    shuffled = records[:]
    rng.shuffle(shuffled)

    n_manual = max(1, round(len(shuffled) * manual_pct / 100))
    manual = shuffled[:n_manual]
    remaining = shuffled[n_manual:]

    n_val = round(len(remaining) * val_split)
    return {
        "manual_test": manual,
        "validation": remaining[:n_val],
        "training": remaining[n_val:],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Download BootBoots multiclass training data")
    parser.add_argument("--dest", default="data_multiclass", help="Destination directory (default: data_multiclass)")
    parser.add_argument("--region", default="eu-west-2", help="AWS region (default: eu-west-2)")
    parser.add_argument("--max-nocat", type=int, default=600,
                        help="Cap NoCat samples (default: 600, as it dominates with 2300+ images)")
    parser.add_argument("--max-per-class", type=int, default=None,
                        help="Cap any class at this many samples (applied after --max-nocat)")
    parser.add_argument("--exclude", nargs="*", default=[],
                        help="Classes to exclude, e.g. --exclude Wolf")
    parser.add_argument("--manual-pct", type=float, default=1.0,
                        help="Percentage held back for manual testing (default: 1.0)")
    parser.add_argument("--val-split", type=float, default=0.2,
                        help="Fraction of non-manual images used for validation (default: 0.2)")
    parser.add_argument("--seed", type=int, default=42, help="Random seed (default: 42)")
    args = parser.parse_args()

    dest = Path(args.dest)

    session = boto3.Session(region_name=args.region)
    dynamodb = session.client("dynamodb")
    s3 = session.client("s3")
    account_id = session.client("sts").get_caller_identity()["Account"]
    bucket = f"bootboots-images-{account_id}-{args.region}"
    print(f"Bucket : {bucket}")
    print(f"Table  : {TABLE_NAME}")
    print()

    print("Scanning DynamoDB for labeled records...")
    records = scan_dynamo(dynamodb, TABLE_NAME)
    print(f"Found {len(records)} labeled records")

    # Group by class
    from collections import defaultdict
    by_class: dict[str, list] = defaultdict(list)
    for r in records:
        by_class[r["cat"]].append(r)

    print("\nClass distribution (raw):")
    for cls, recs in sorted(by_class.items(), key=lambda x: -len(x[1])):
        print(f"  {len(recs):5d}  {cls}")

    # Apply exclusions
    for cls in args.exclude:
        if cls in by_class:
            del by_class[cls]
            print(f"\nExcluded: {cls}")

    # Cap NoCat
    rng = random.Random(args.seed)
    if "NoCat" in by_class and len(by_class["NoCat"]) > args.max_nocat:
        shuffled = by_class["NoCat"][:]
        rng.shuffle(shuffled)
        by_class["NoCat"] = shuffled[:args.max_nocat]
        print(f"\nNoCat capped at {args.max_nocat}")

    # Optional per-class cap
    if args.max_per_class:
        for cls in by_class:
            if cls != "NoCat" and len(by_class[cls]) > args.max_per_class:
                shuffled = by_class[cls][:]
                rng.shuffle(shuffled)
                by_class[cls] = shuffled[:args.max_per_class]
        print(f"Per-class cap: {args.max_per_class}")

    print("\nClass distribution (after caps):")
    all_splits: dict[str, dict] = {}
    for cls in sorted(by_class.keys()):
        class_splits = split_class(by_class[cls], args.manual_pct, args.val_split, args.seed)
        all_splits[cls] = class_splits
        print(f"  {cls:12s}  train={len(class_splits['training'])}  "
              f"val={len(class_splits['validation'])}  manual_test={len(class_splits['manual_test'])}")

    print()
    total = sum(len(v) for s in all_splits.values() for v in s.values())
    done = errors = skipped = 0

    for class_name, class_splits in all_splits.items():
        for split_name, split_records in class_splits.items():
            for record in split_records:
                filename = record["imageName"].split("/")[-1]
                if not filename.lower().endswith((".jpg", ".jpeg")):
                    filename += ".jpeg"

                dest_path = dest / split_name / class_name / filename

                if dest_path.exists():
                    skipped += 1
                    done += 1
                else:
                    ok = download_image(s3, bucket, record["imageName"], dest_path)
                    done += 1
                    if not ok:
                        errors += 1

                if done % 100 == 0:
                    print(f"  {done}/{total}  ({errors} errors, {skipped} already present)")

    print(f"\nFinished. {done - errors - skipped} downloaded, {skipped} already present, {errors} errors.")
    print(f"Data written to: {dest.resolve()}")

    summary = {
        "bucket": bucket,
        "region": args.region,
        "total_images": done - errors,
        "splits": {
            cls: {split_name: len(recs) for split_name, recs in class_splits.items()}
            for cls, class_splits in all_splits.items()
        },
    }
    (dest / "download_summary.json").write_text(json.dumps(summary, indent=2))
    print(f"Summary written to {dest / 'download_summary.json'}")


if __name__ == "__main__":
    main()
