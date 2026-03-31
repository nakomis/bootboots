#!/usr/bin/env python3
"""
BootBoots - Multiclass Training Script (M-series Mac)

Trains a MobileNetV2-based classifier to recognise each individual cat
(Boots, Chi, Tau, Kappa, Mu, Wolf, NoCat). Expects the directory structure
created by download_data_multiclass.py.

At inference time the result can still be used as a binary Boots detector:
any prediction other than Boots → don't spray.

Run with:
    python train_multiclass.py

Or with custom settings:
    python train_multiclass.py --data data_multiclass --epochs 50

Requires Python 3.11 and:
    pip install tensorflow-macos tensorflow-metal
"""

import argparse
import json
from pathlib import Path

import tensorflow as tf
from tensorflow.keras import callbacks, layers, optimizers
from tensorflow.keras.applications import MobileNetV2

IMG_SIZE = (224, 224)


def remove_corrupt_images(directory: Path) -> int:
    """Walk directory tree and delete any image that TF can't decode."""
    removed = 0
    for fpath in directory.rglob("*"):
        if fpath.suffix.lower() not in (".jpg", ".jpeg", ".png"):
            continue
        try:
            raw = tf.io.read_file(str(fpath))
            tf.image.decode_jpeg(raw, channels=3)
        except Exception as exc:  # noqa: BLE001
            print(f"  [corrupt] Removing {fpath.name}: {exc}")
            fpath.unlink()
            removed += 1
    return removed


def build_model(num_classes: int, dropout_rate: float, l2_reg: float, train_base: bool) -> tf.keras.Model:
    regularizer = tf.keras.regularizers.l2(l2_reg)
    base_model = MobileNetV2(input_shape=IMG_SIZE + (3,), include_top=False, weights="imagenet")
    base_model.trainable = train_base

    inputs = tf.keras.Input(shape=IMG_SIZE + (3,))
    x = layers.RandomFlip("horizontal_and_vertical")(inputs)
    x = layers.RandomRotation(0.3)(x)
    x = layers.RandomZoom(0.2)(x)
    x = tf.keras.applications.mobilenet_v2.preprocess_input(x)
    x = base_model(x, training=False)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(dropout_rate)(x)
    outputs = layers.Dense(num_classes, activation="softmax", kernel_regularizer=regularizer)(x)

    model = tf.keras.Model(inputs, outputs)
    model.compile(
        optimizer=optimizers.Adam(learning_rate=0.0001),
        loss="categorical_crossentropy",
        metrics=["accuracy"],
    )
    return model


def main() -> None:
    parser = argparse.ArgumentParser(description="Train BootBoots multiclass cat classifier locally")
    parser.add_argument("--data", default="data_multiclass", help="Data directory (default: data_multiclass)")
    parser.add_argument("--output", default="models_multiclass", help="Output directory (default: models_multiclass)")
    parser.add_argument("--epochs", type=int, default=50, help="Max training epochs (default: 50)")
    parser.add_argument("--batch-size", type=int, default=16, help="Batch size (default: 16)")
    parser.add_argument("--dropout", type=float, default=0.3, help="Dropout rate (default: 0.3)")
    parser.add_argument("--l2", type=float, default=0.0001, help="L2 regularisation weight (default: 0.0001)")
    parser.add_argument("--train-base", action="store_true",
                        help="Also fine-tune the MobileNetV2 base (slower, sometimes better)")
    parser.add_argument("--no-early-stopping", action="store_true", help="Disable early stopping")
    parser.add_argument("--patience", type=int, default=10, help="Early stopping patience (default: 10)")
    args = parser.parse_args()

    data_dir = Path(args.data)
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    train_dir = data_dir / "training"
    val_dir = data_dir / "validation"

    if not train_dir.exists():
        print(f"ERROR: Training directory not found: {train_dir}")
        print("Run download_data_multiclass.py first.")
        raise SystemExit(1)

    gpus = tf.config.list_physical_devices("GPU")
    print(f"TensorFlow {tf.__version__}")
    print(f"Devices: {[d.name for d in tf.config.list_physical_devices()]}")
    if gpus:
        print(f"Metal GPU acceleration active ({len(gpus)} device(s))")
    else:
        print("No GPU found — running on CPU (install tensorflow-metal for M-series acceleration)")
    print()

    print("Scanning for corrupt images...")
    n_bad_train = remove_corrupt_images(train_dir)
    n_bad_val = remove_corrupt_images(val_dir)
    print(f"  Removed {n_bad_train} corrupt from training, {n_bad_val} from validation\n")

    train_ds = tf.keras.utils.image_dataset_from_directory(
        train_dir,
        image_size=IMG_SIZE,
        batch_size=args.batch_size,
        label_mode="categorical",
        shuffle=True,
        seed=42,
    )
    val_ds = tf.keras.utils.image_dataset_from_directory(
        val_dir,
        image_size=IMG_SIZE,
        batch_size=args.batch_size,
        label_mode="categorical",
        shuffle=False,
    )

    class_names: list[str] = train_ds.class_names
    num_classes = len(class_names)
    print(f"Classes ({num_classes}): {class_names}")

    # Compute class weights from training directory counts
    class_counts = {name: sum(1 for _ in (train_dir / name).iterdir()) for name in class_names}
    total = sum(class_counts.values())
    class_weight = {i: total / (num_classes * count) for i, (_, count) in enumerate(class_counts.items())}
    print(f"Class counts : {class_counts}")
    print(f"Class weights: { {class_names[i]: f'{w:.2f}' for i, w in class_weight.items()} }")
    print()

    AUTOTUNE = tf.data.AUTOTUNE
    train_ds = train_ds.cache().prefetch(AUTOTUNE)
    val_ds = val_ds.cache().prefetch(AUTOTUNE)

    model = build_model(num_classes, args.dropout, args.l2, args.train_base)
    model.summary()

    cb_list = []
    if not args.no_early_stopping:
        cb_list.append(callbacks.EarlyStopping(
            monitor="val_accuracy",
            patience=args.patience,
            min_delta=0.001,
            restore_best_weights=True,
            verbose=1,
        ))
    cb_list.append(callbacks.ModelCheckpoint(
        str(output_dir / "best_model.keras"),
        monitor="val_accuracy",
        save_best_only=True,
        verbose=1,
    ))

    print(f"Training for up to {args.epochs} epochs...")
    history = model.fit(
        train_ds,
        validation_data=val_ds,
        epochs=args.epochs,
        callbacks=cb_list,
        class_weight=class_weight,
    )

    best_keras_path = output_dir / "best_model.keras"
    class_names_path = output_dir / "class_names.json"
    class_names_path.write_text(json.dumps(class_names))

    print(f"\nBest model : {best_keras_path}")
    print(f"Class names: {class_names_path}  →  {class_names}")

    final_val_acc = history.history["val_accuracy"][-1]
    final_val_loss = history.history["val_loss"][-1]
    print(f"\nFinal validation accuracy : {final_val_acc:.4f}  ({final_val_acc * 100:.1f}%)")
    print(f"Final validation loss     : {final_val_loss:.4f}")


if __name__ == "__main__":
    main()
