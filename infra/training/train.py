"""
BootBoots CatCam - MobileNet v2 Transfer Learning Training Script

Runs inside the SageMaker TensorFlow training container.
Input channels:
  - training/   : images organised by class subfolder
  - validation/ : images organised by class subfolder
Output:
  - SavedModel written to SM_MODEL_DIR (versioned as /1/ for TF Serving)
  - class_names.json alongside the model (for inference mapping)

Class order is determined alphabetically by Keras, which matches the
CAT_CLASSES order defined in the CDK stack:
  Boots, NotBoots
"""

import json
import os

import tensorflow as tf
from tensorflow.keras import callbacks, layers, models, optimizers
from tensorflow.keras.applications import MobileNetV2

# ---------------------------------------------------------------------------
# SageMaker paths (set automatically by the container)
# ---------------------------------------------------------------------------
TRAINING_DIR = os.environ.get("SM_CHANNEL_TRAINING", "/opt/ml/input/data/training")
VALIDATION_DIR = os.environ.get("SM_CHANNEL_VALIDATION", "/opt/ml/input/data/validation")
MODEL_DIR = os.environ.get("SM_MODEL_DIR", "/opt/ml/model")
OUTPUT_DIR = os.environ.get("SM_OUTPUT_DATA_DIR", "/opt/ml/output/data")

os.makedirs(OUTPUT_DIR, exist_ok=True)

# ---------------------------------------------------------------------------
# Hyperparameters (passed as strings via /opt/ml/input/config/hyperparameters.json)
# ---------------------------------------------------------------------------
hp_path = "/opt/ml/input/config/hyperparameters.json"
hp: dict = {}
if os.path.exists(hp_path):
    with open(hp_path) as f:
        # SageMaker JSON-encodes string values, so strip surrounding quotes
        raw = json.load(f)
        hp = {k: v.strip('"') if isinstance(v, str) else v for k, v in raw.items()}

epochs               = int(hp.get("epochs", 50))
learning_rate        = float(hp.get("learning_rate", 0.0001))
batch_size           = int(hp.get("batch_size", 16))
num_classes          = int(hp.get("num_classes", 7))
dropout_rate         = float(hp.get("dropout_rate", 0.3))
l2_reg               = float(hp.get("regularizers_l2", 0.0001))
train_only_top       = hp.get("train_only_top_layer", "False").lower() == "true"
use_augmentation     = hp.get("augmentation", "True").lower() == "true"
use_early_stopping   = hp.get("early_stopping", "True").lower() == "true"
es_patience          = int(hp.get("early_stopping_patience", 10))
es_min_delta         = float(hp.get("early_stopping_min_delta", 0.001))

IMG_SIZE = (224, 224)

print(f"Hyperparameters: epochs={epochs}, lr={learning_rate}, batch={batch_size}, "
      f"dropout={dropout_rate}, l2={l2_reg}, train_only_top={train_only_top}, "
      f"augmentation={use_augmentation}, early_stopping={use_early_stopping}")
print(f"Training dir : {TRAINING_DIR}")
print(f"Validation dir: {VALIDATION_DIR}")

# ---------------------------------------------------------------------------
# Pre-scan: remove corrupt images so the dataset loader doesn't crash
# ---------------------------------------------------------------------------
def remove_corrupt_images(directory: str) -> int:
    """Walk directory tree, delete any image that TF can't decode."""
    removed = 0
    for root, _dirs, files in os.walk(directory):
        for fname in files:
            if not fname.lower().endswith((".jpg", ".jpeg", ".png")):
                continue
            fpath = os.path.join(root, fname)
            try:
                raw = tf.io.read_file(fpath)
                tf.image.decode_jpeg(raw, channels=3)
            except Exception as exc:  # noqa: BLE001
                print(f"[corrupt] Removing {fpath}: {exc}")
                os.remove(fpath)
                removed += 1
    return removed

n_removed_train = remove_corrupt_images(TRAINING_DIR)
n_removed_val   = remove_corrupt_images(VALIDATION_DIR)
print(f"Removed corrupt images: {n_removed_train} from training, {n_removed_val} from validation")

# ---------------------------------------------------------------------------
# Datasets
# ---------------------------------------------------------------------------
train_ds = tf.keras.utils.image_dataset_from_directory(
    TRAINING_DIR,
    image_size=IMG_SIZE,
    batch_size=batch_size,
    label_mode="categorical",
    shuffle=True,
    seed=42,
)
val_ds = tf.keras.utils.image_dataset_from_directory(
    VALIDATION_DIR,
    image_size=IMG_SIZE,
    batch_size=batch_size,
    label_mode="categorical",
    shuffle=False,
)

class_names = train_ds.class_names
print(f"Classes ({len(class_names)}): {class_names}")

AUTOTUNE = tf.data.AUTOTUNE
train_ds = train_ds.cache().prefetch(AUTOTUNE)
val_ds   = val_ds.cache().prefetch(AUTOTUNE)

# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------
regularizer = tf.keras.regularizers.l2(l2_reg)

base_model = MobileNetV2(
    input_shape=IMG_SIZE + (3,),
    include_top=False,
    weights="imagenet",
)
base_model.trainable = not train_only_top

inputs = tf.keras.Input(shape=IMG_SIZE + (3,))
x = inputs

if use_augmentation:
    x = layers.RandomFlip("horizontal_and_vertical")(x)
    x = layers.RandomRotation(0.3)(x)
    x = layers.RandomZoom(0.2)(x)

x = tf.keras.applications.mobilenet_v2.preprocess_input(x)
x = base_model(x, training=False)
x = layers.GlobalAveragePooling2D()(x)
x = layers.Dropout(dropout_rate)(x)
outputs = layers.Dense(
    num_classes,
    activation="softmax",
    kernel_regularizer=regularizer,
)(x)

model = tf.keras.Model(inputs, outputs)
model.compile(
    optimizer=optimizers.Adam(learning_rate=learning_rate),
    loss="categorical_crossentropy",
    metrics=["accuracy"],
)
model.summary()

# ---------------------------------------------------------------------------
# Callbacks
# ---------------------------------------------------------------------------
cb_list = []

if use_early_stopping:
    cb_list.append(callbacks.EarlyStopping(
        monitor="val_accuracy",
        patience=es_patience,
        min_delta=es_min_delta,
        restore_best_weights=True,
        verbose=1,
    ))

cb_list.append(callbacks.ModelCheckpoint(
    os.path.join(OUTPUT_DIR, "best_model.keras"),
    monitor="val_accuracy",
    save_best_only=True,
    verbose=1,
))

# ---------------------------------------------------------------------------
# Train
# ---------------------------------------------------------------------------
history = model.fit(
    train_ds,
    validation_data=val_ds,
    epochs=epochs,
    callbacks=cb_list,
)

# ---------------------------------------------------------------------------
# Save
# ---------------------------------------------------------------------------
# TF Serving expects a versioned SavedModel: <model_dir>/1/
saved_model_path = os.path.join(MODEL_DIR, "1")
model.save(saved_model_path)
print(f"SavedModel written to {saved_model_path}")

# Save class names so the inference Lambda can map indices to names
class_names_path = os.path.join(MODEL_DIR, "class_names.json")
with open(class_names_path, "w") as f:
    json.dump(class_names, f)
print(f"Class names written to {class_names_path}: {class_names}")

# Print final metrics
final_val_acc = history.history["val_accuracy"][-1]
final_val_loss = history.history["val_loss"][-1]
print(f"Final validation accuracy: {final_val_acc:.4f}")
print(f"Final validation loss:     {final_val_loss:.4f}")
