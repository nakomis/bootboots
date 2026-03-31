#!/usr/bin/env python3
"""
Convert a locally trained .keras model to the TF Serving SavedModel format
expected by the BootBoots SageMaker endpoint, then package it as model.tar.gz.

Usage:
    python export_for_sagemaker.py [--model-dir models_multiclass] [--output model.tar.gz]
"""

import argparse
import json
import shutil
import tarfile
import tempfile
from pathlib import Path

import tensorflow as tf

IMG_SIZE = (240, 240)  # EfficientNetV2B1 native resolution


def main() -> None:
    parser = argparse.ArgumentParser(description="Export local .keras model for SageMaker deployment")
    parser.add_argument("--model-dir", default="models_multiclass",
                        help="Local model directory containing best_model.keras and class_names.json (default: models_multiclass)")
    parser.add_argument("--output", default="model.tar.gz",
                        help="Output tar.gz path (default: model.tar.gz)")
    args = parser.parse_args()

    model_dir = Path(args.model_dir)
    keras_path = model_dir / "best_model.keras"
    class_names_path = model_dir / "class_names.json"

    if not keras_path.exists():
        raise FileNotFoundError(f"Model not found: {keras_path}")
    if not class_names_path.exists():
        raise FileNotFoundError(f"Class names not found: {class_names_path}")

    class_names = json.loads(class_names_path.read_text())
    print(f"Loading model: {keras_path}")
    print(f"Classes: {class_names}")
    model = tf.keras.models.load_model(str(keras_path))

    # Warn if the class list differs from the hardcoded Lambda CAT_NAMES
    lambda_classes = ["Boots", "Chi", "Kappa", "Mu", "NoCat", "Tau", "Wolf"]
    if class_names != lambda_classes:
        print()
        print("WARNING: class names differ from the Lambda's hardcoded CAT_NAMES.")
        print(f"  Model classes : {class_names}")
        print(f"  Lambda expects: {lambda_classes}")
        print("  Update CAT_NAMES in infra/lambda/infer-lambda/src/handler.ts")
        print("  and redeploy the Lambda before or after this model update.")
        print()

    @tf.function(input_signature=[
        tf.TensorSpec(shape=[None], dtype=tf.string, name="image_bytes")
    ])
    def serve_jpeg(image_bytes):
        def decode_one(img_bytes):
            img = tf.image.decode_jpeg(img_bytes, channels=3)
            img = tf.image.resize(img, IMG_SIZE)
            return tf.cast(img, tf.float32)
        images = tf.map_fn(decode_one, image_bytes, fn_output_signature=tf.float32)
        result = model(images, training=False)
        if isinstance(result, dict):
            result = next(iter(result.values()))
        return {"probabilities": result}

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)

        # TF Serving expects a versioned SavedModel at <model_dir>/1/
        saved_model_path = tmp_path / "1"
        print(f"Saving with serve_jpeg signature to {saved_model_path} ...")
        tf.saved_model.save(model, str(saved_model_path),
                            signatures={"serving_default": serve_jpeg})

        # class_names.json sits alongside the versioned directory
        shutil.copy(class_names_path, tmp_path / "class_names.json")

        # Package into model.tar.gz — contents rooted at tmp_path so tar
        # extracts as:  1/  class_names.json  (what SageMaker expects)
        output = Path(args.output)
        print(f"Packaging {output} ...")
        with tarfile.open(output, "w:gz") as tar:
            tar.add(tmp_path, arcname=".")

    print(f"\nDone. Model packaged as: {output.resolve()}")
    print(f"Classes: {class_names}")


if __name__ == "__main__":
    main()
