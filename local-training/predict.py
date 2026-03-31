#!/usr/bin/env python3
"""
BootBoots - Prediction Script

Run a trained model against one or more images and print the result.

Usage:
    python predict.py path/to/image.jpg
    python predict.py data/manual_test/Boots/       # whole directory
    python predict.py img1.jpg img2.jpg img3.jpg    # multiple files

The model and class names are loaded from the models/ directory by default.
Use --model to point to a different location.
"""

import argparse
import json
import sys
from pathlib import Path

import tensorflow as tf


IMG_SIZE = (224, 224)


def load_model_and_classes(model_dir: Path) -> tuple:
    model_path = model_dir / "best_model.keras"
    class_names_path = model_dir / "class_names.json"

    if not model_path.exists():
        print(f"ERROR: No model found at {model_path}")
        print("Run train_local.py first to train a model.")
        raise SystemExit(1)

    if not class_names_path.exists():
        print(f"ERROR: class_names.json not found at {class_names_path}")
        raise SystemExit(1)

    print(f"Loading model from {model_path}...")
    model = tf.keras.models.load_model(str(model_path))

    class_names = json.loads(class_names_path.read_text())
    print(f"Classes: {class_names}")
    print()
    return model, class_names


def predict_image(model, class_names: list[str], image_path: Path) -> tuple[str, float]:
    """Run inference on a single image. Returns (predicted_class, confidence)."""
    raw = tf.io.read_file(str(image_path))
    img = tf.image.decode_jpeg(raw, channels=3)
    img = tf.image.resize(img, IMG_SIZE)
    img = tf.expand_dims(img, 0)  # Add batch dimension

    probs = model(img, training=False).numpy()[0]
    best_idx = int(probs.argmax())
    return class_names[best_idx], float(probs[best_idx])


def collect_images(paths: list[Path]) -> list[Path]:
    """Expand any directories to their constituent image files."""
    images = []
    for p in paths:
        if p.is_dir():
            for ext in ("*.jpg", "*.jpeg", "*.JPG", "*.JPEG", "*.png", "*.PNG"):
                images.extend(sorted(p.glob(ext)))
        elif p.is_file():
            images.append(p)
        else:
            print(f"WARNING: Not found: {p}")
    return images


def main() -> None:
    parser = argparse.ArgumentParser(description="Run BootBoots cat classifier on images")
    parser.add_argument("images", nargs="+", type=Path,
                        help="Image file(s) or directory/directories to classify")
    parser.add_argument("--model", type=Path, default=Path("models"),
                        help="Model directory (default: models/)")
    args = parser.parse_args()

    infer, class_names = load_model_and_classes(args.model)

    images = collect_images(args.images)
    if not images:
        print("No images found.")
        raise SystemExit(1)

    print(f"Classifying {len(images)} image(s)...\n")

    results = []
    for image_path in images:
        try:
            predicted_class, confidence = predict_image(infer, class_names, image_path)
            # Colour the output: green for Boots, yellow for NotBoots
            colour = "\033[92m" if predicted_class == "Boots" else "\033[93m"
            reset = "\033[0m"
            bar = "█" * int(confidence * 20)
            print(f"  {colour}{predicted_class:10s}{reset}  {confidence * 100:5.1f}%  {bar}  {image_path.name}")
            results.append({"file": str(image_path), "class": predicted_class, "confidence": confidence})
        except Exception as e:  # noqa: BLE001
            print(f"  ERROR  {image_path.name}: {e}")

    if len(images) > 1:
        boots_count = sum(1 for r in results if r["class"] == "Boots")
        not_boots_count = len(results) - boots_count
        print(f"\nSummary: {boots_count} Boots, {not_boots_count} NotBoots out of {len(results)} images")


if __name__ == "__main__":
    main()
