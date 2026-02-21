#!/usr/bin/env python3
"""
BootBoots local inference script.

Downloads the latest trained model from S3 (if not already cached locally)
and runs inference on one or more JPEG images, printing Boots/NotBoots
confidence scores.

Usage:
    python infer_local.py image.jpg [image2.jpg ...]

    # Force re-download the model even if already cached:
    python infer_local.py --refresh image.jpg

    # Specify a particular training job's model explicitly:
    python infer_local.py --job bootboots-2026-02-21T16-29-42 image.jpg

Requirements (Python 3.11 via asdf):
    pip install tensorflow boto3

AWS credentials:
    The script uses your nakom.is-sandbox profile.
    Set it before running:
        export AWS_PROFILE=nakom.is-sandbox
"""

import argparse
import json
import os
import sys
import tarfile
import tempfile

MODEL_CACHE_DIR = os.path.expanduser("~/.cache/bootboots/model")
MODELS_BUCKET = "bootboots-models-975050268859"
TRAINING_OUTPUT_PREFIX = "training-output"
AWS_REGION = "eu-west-2"


def find_latest_job(s3_client) -> str:
    """Return the name of the most recently completed training job artifact on S3."""
    paginator = s3_client.get_paginator("list_objects_v2")
    pages = paginator.paginate(
        Bucket=MODELS_BUCKET,
        Prefix=f"{TRAINING_OUTPUT_PREFIX}/",
        Delimiter="/"
    )
    prefixes = []
    for page in pages:
        for p in page.get("CommonPrefixes", []):
            prefixes.append(p["Prefix"].rstrip("/").split("/")[-1])

    if not prefixes:
        raise RuntimeError(f"No training jobs found in s3://{MODELS_BUCKET}/{TRAINING_OUTPUT_PREFIX}/")

    # Sort by name — jobs are named bootboots-YYYY-MM-DDTHH-MM-SS so lexicographic = chronological
    prefixes.sort()
    latest = prefixes[-1]
    print(f"  Latest training job: {latest}")
    return latest


def download_model(job_name: str) -> str:
    """
    Download model.tar.gz for the given job and extract to MODEL_CACHE_DIR/<job_name>/.
    Returns the path to the extracted SavedModel directory (the '1/' subdirectory).
    """
    import boto3

    model_dir = os.path.join(MODEL_CACHE_DIR, job_name)
    saved_model_path = os.path.join(model_dir, "1")

    if os.path.isdir(saved_model_path):
        print(f"  Using cached model at {saved_model_path}")
        return model_dir

    s3_key = f"{TRAINING_OUTPUT_PREFIX}/{job_name}/output/model.tar.gz"
    print(f"  Downloading s3://{MODELS_BUCKET}/{s3_key} ...")

    session = boto3.Session()
    s3 = session.client("s3", region_name=AWS_REGION)

    os.makedirs(model_dir, exist_ok=True)
    tar_path = os.path.join(model_dir, "model.tar.gz")

    s3.download_file(MODELS_BUCKET, s3_key, tar_path)
    print(f"  Extracting to {model_dir} ...")

    with tarfile.open(tar_path, "r:gz") as tar:
        tar.extractall(model_dir)

    os.remove(tar_path)

    if not os.path.isdir(saved_model_path):
        # Older artifacts may not have the '1/' subdirectory — use model_dir directly
        print(f"  Warning: no '1/' subdir found; using {model_dir} as SavedModel root")
        return model_dir

    return model_dir


def load_class_names(model_dir: str) -> list:
    """Load class names from class_names.json alongside the model."""
    path = os.path.join(model_dir, "class_names.json")
    if os.path.isfile(path):
        with open(path) as f:
            return json.load(f)
    # Fallback to binary default
    return ["Boots", "NotBoots"]


def run_inference(model_dir: str, image_paths: list) -> None:
    """Load SavedModel and run inference on each image, printing results."""
    import numpy as np
    import tensorflow as tf

    print(f"\nLoading model from {model_dir} ...")
    model = tf.saved_model.load(os.path.join(model_dir, "1") if os.path.isdir(os.path.join(model_dir, "1")) else model_dir)
    infer = model.signatures["serving_default"]

    class_names = load_class_names(model_dir)
    print(f"Classes: {class_names}\n")

    IMG_SIZE = (224, 224)

    for image_path in image_paths:
        if not os.path.isfile(image_path):
            print(f"[SKIP] File not found: {image_path}")
            continue

        # Load and preprocess — model includes preprocess_input internally
        raw = tf.io.read_file(image_path)
        img = tf.image.decode_jpeg(raw, channels=3)
        img = tf.image.resize(img, IMG_SIZE)
        img = tf.cast(img, tf.float32)           # raw [0, 255] — model applies normalisation
        img = tf.expand_dims(img, axis=0)        # add batch dimension

        output = infer(img)
        # The output key is typically 'dense' or 'output_0' — grab the first value
        probs = list(output.values())[0].numpy()[0]

        winner_idx = int(np.argmax(probs))
        winner_name = class_names[winner_idx] if winner_idx < len(class_names) else "Unknown"
        winner_conf = probs[winner_idx]

        scores = "  ".join(
            f"{class_names[i] if i < len(class_names) else i}={probs[i]*100:.1f}%"
            for i in range(len(probs))
        )

        print(f"{os.path.basename(image_path)}")
        print(f"  {scores}")
        print(f"  => {winner_name} ({winner_conf*100:.1f}%)")
        print()


def main():
    parser = argparse.ArgumentParser(description="Run BootBoots Boots/NotBoots inference locally.")
    parser.add_argument("images", nargs="+", help="JPEG image file(s) to classify")
    parser.add_argument("--job", help="Training job name (default: latest in S3)", default=None)
    parser.add_argument("--refresh", action="store_true", help="Re-download model even if cached")
    args = parser.parse_args()

    # Separate flags from image paths
    image_paths = [p for p in args.images]

    try:
        import boto3
    except ImportError:
        sys.exit("boto3 not found. Run: pip install boto3")

    try:
        import tensorflow  # noqa: F401
    except ImportError:
        sys.exit("tensorflow not found. Run: pip install tensorflow  (requires Python 3.11)")

    session = boto3.Session()
    s3 = session.client("s3", region_name=AWS_REGION)

    if args.job:
        job_name = args.job
        print(f"Using specified job: {job_name}")
    else:
        print("Finding latest training job ...")
        job_name = find_latest_job(s3)

    if args.refresh:
        import shutil
        cache_path = os.path.join(MODEL_CACHE_DIR, job_name)
        if os.path.exists(cache_path):
            print(f"Removing cached model at {cache_path} ...")
            shutil.rmtree(cache_path)

    model_dir = download_model(job_name)
    run_inference(model_dir, image_paths)


if __name__ == "__main__":
    main()
