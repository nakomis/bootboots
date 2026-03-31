#!/usr/bin/env python3
"""
BootBoots local inference server.

Loads a trained .keras model and exposes a single POST /predict endpoint
that the sandbox app calls whilst you're labelling images.

Run from local-training/ with the venv active:

    source .venv/bin/activate
    python serve.py

Or to use the binary model instead of multiclass:

    python serve.py --model models
"""

import argparse
import json
from pathlib import Path

import tensorflow as tf
from flask import Flask, jsonify, request
from flask_cors import CORS

IMG_SIZE = (224, 224)
PORT = 8765
SCRIPT_DIR = Path(__file__).parent


def load_model(model_dir: str) -> tuple[tf.keras.Model, list[str]]:
    model_path = SCRIPT_DIR / model_dir / "best_model.keras"
    class_names_path = SCRIPT_DIR / model_dir / "class_names.json"

    if not model_path.exists():
        print(f"ERROR: Model not found: {model_path}")
        print("Train a model first — see README.md for instructions.")
        raise SystemExit(1)

    print(f"Loading model from {model_path} ...")
    model = tf.keras.models.load_model(str(model_path))
    class_names: list[str] = json.loads(class_names_path.read_text())
    print(f"Classes ({len(class_names)}): {', '.join(class_names)}")
    return model, class_names


def create_app(model: tf.keras.Model, class_names: list[str]) -> Flask:
    app = Flask(__name__)
    CORS(app)

    # Chrome's Private Network Access policy blocks requests from public HTTPS
    # origins (sandbox.nakomis.com) to loopback addresses unless the server
    # explicitly opts in via this header on both preflight and actual responses.
    @app.after_request
    def allow_private_network(response):  # type: ignore[return]
        response.headers["Access-Control-Allow-Private-Network"] = "true"
        return response

    @app.get("/health")
    def health():  # type: ignore[return]
        return jsonify({"status": "ok", "classes": class_names})

    @app.post("/predict")
    def predict():  # type: ignore[return]
        data = request.get_data()
        if not data:
            return jsonify({"error": "Empty request body — send raw JPEG bytes"}), 400

        try:
            raw = tf.constant(data)
            img = tf.image.decode_jpeg(raw, channels=3)
            img = tf.image.resize(img, IMG_SIZE)
            img = tf.expand_dims(img, 0)
            probs = model(img, training=False).numpy()[0]
        except Exception as exc:  # noqa: BLE001
            return jsonify({"error": f"Inference failed: {exc}"}), 500

        max_idx = int(probs.argmax())
        return jsonify({
            "prediction": class_names[max_idx],
            "confidence": round(float(probs[max_idx]), 3),
        })

    return app


def main() -> None:
    parser = argparse.ArgumentParser(description="BootBoots local inference server")
    parser.add_argument(
        "--model",
        default="models_multiclass",
        help="Model directory name inside local-training/ (default: models_multiclass)",
    )
    parser.add_argument("--port", type=int, default=PORT, help=f"Port to listen on (default: {PORT})")
    args = parser.parse_args()

    model, class_names = load_model(args.model)
    app = create_app(model, class_names)

    print(f"\nBootBoots inference server  →  http://localhost:{args.port}")
    print("POST /predict   send raw JPEG bytes → { prediction, confidence }")
    print("GET  /health    check server is up\n")

    app.run(host="0.0.0.0", port=args.port)


if __name__ == "__main__":
    main()
