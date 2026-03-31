#!/usr/bin/env python3
"""
Convert a trained .keras model to TensorFlow.js GraphModel format so it can be
loaded by the local TypeScript inference server (serve/).

Run once after training:

    python export_tfjs.py
    # or for the binary model:
    python export_tfjs.py --model models/best_model.keras --output models/tfjs_model

Requires tensorflowjs (already in requirements.txt):
    pip install tensorflowjs
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Export .keras model to TensorFlow.js format")
    parser.add_argument(
        "--model",
        default="models_multiclass/best_model.keras",
        help="Path to the .keras model file (default: models_multiclass/best_model.keras)",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Output directory (default: <model_dir>/tfjs_model)",
    )
    args = parser.parse_args()

    model_path = Path(args.model)
    if not model_path.exists():
        print(f"ERROR: Model not found: {model_path}")
        print("Train a model first with train_multiclass.py (or train_local.py for binary).")
        raise SystemExit(1)

    output_dir = Path(args.output) if args.output else model_path.parent / "tfjs_model"

    if not shutil.which("tensorflowjs_converter"):
        print("ERROR: tensorflowjs_converter not found.")
        print("Install it: pip install tensorflowjs")
        raise SystemExit(1)

    print(f"Converting {model_path} → {output_dir}/")

    result = subprocess.run(
        [
            "tensorflowjs_converter",
            "--input_format", "keras",
            "--output_format", "tfjs_graph_model",
            str(model_path),
            str(output_dir),
        ],
        check=False,
    )

    if result.returncode != 0:
        print("\nConversion failed — see error above.")
        raise SystemExit(result.returncode)

    print(f"\nDone. TFJs model written to {output_dir.resolve()}/")
    print("Start the inference server:")
    print("    cd serve && npm install && npm start")


if __name__ == "__main__":
    main()
