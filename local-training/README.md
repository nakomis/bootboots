# BootBoots — Local Training

Train and test the cat classifier on your M-series Mac, without going anywhere near SageMaker.

---

## How it all fits together

```
                    ┌─────────────────────────────────────────┐
                    │              AWS (existing)              │
                    │                                          │
                    │  DynamoDB: catadata                      │
                    │  ┌──────────────────────┐               │
                    │  │ imageName │ cat       │               │
                    │  │ 540.jpeg  │ Chi       │               │
                    │  │ 2025-07.. │ Boots     │               │
                    │  │ 2026-01.. │ Tau       │               │
                    │  └──────────────────────┘               │
                    │           │                              │
                    │           │ labels                       │
                    │           ▼                              │
                    │  S3: bootboots-images-…/catcam-training/ │
                    │  ┌────────────────────┐                  │
                    │  │ 540.jpeg           │                  │
                    │  │ 2025-07-29T21…jpg  │                  │
                    │  │ 2026-01-18T00…jpg  │                  │
                    │  └────────────────────┘                  │
                    └──────────────┬──────────────────────────┘
                                   │
                    download_data_multiclass.py
                    (reads labels + downloads images)
                                   │
                                   ▼
          ┌────────────────────────────────────────────────────┐
          │             local-training/data_multiclass/         │
          │                                                      │
          │  training/         (≈79% of images)                 │
          │  ├── Boots/                                          │
          │  ├── Chi/                                            │
          │  ├── Kappa/                                          │
          │  ├── Mu/                                             │
          │  ├── NoCat/                                          │
          │  ├── Tau/                                            │
          │  └── Wolf/                                           │
          │                                                      │
          │  validation/       (≈20% of images)                 │
          │  └── (same class dirs)                               │
          │        ← used during training each epoch             │
          │                                                      │
          │  manual_test/      (1% of images)                   │
          │  └── (same class dirs)                               │
          │        ← NEVER seen during training                  │
          └───────────────┬────────────────────────────────────┘
                          │
                    train_multiclass.py
                    (EfficientNetV2B1 on Metal GPU)
                          │
                          ▼
          ┌────────────────────────────┐
          │  local-training/models_multiclass/ │
          │                            │
          │  best_model.keras          │
          │  class_names.json          │
          └──────────────┬─────────────┘
                         │
               predict.py / serve.py
               (manual testing + sandbox badge)
```

---

## The two filename formats

The catcam has used two naming schemes over time:

| Era | Format | Example |
|-----|--------|---------|
| Pre-2026 (old) | Numeric | `540.jpeg` |
| 2025 onwards | ISO timestamp | `2025-07-29T21-20-54-225Z.jpg` |

`download_data_multiclass.py` handles both transparently. The old images also had
a bug where they were saved without a JPEG end-of-file marker — the script patches
those automatically as it downloads them.

---

## Model architecture

The classifier is built on **EfficientNetV2B1** (ImageNet pretrained), with a
GlobalAveragePooling → Dropout → Dense(7, softmax) head. Input resolution: 240×240.

At inference time the seven-class output collapses to a binary spray decision:
`Boots → spray`, anything else → don't spray.

Data augmentation runs in the **data pipeline** (not inside the model), applied to
training images only:

| Augmentation | Value | Rationale |
|---|---|---|
| `RandomFlip` | horizontal only | Cats on a floor; vertical flips aren't realistic |
| `RandomRotation` | ±15% | Fixed camera — extreme rotation unlikely |
| `RandomZoom` | zoom in 0–30% | Handles cats not filling the frame |
| `RandomContrast` | ±20% | Kitchen lighting varies by time of day |
| `RandomBrightness` | ±20% | Same reason |

Keeping augmentation out of the model means `export_for_sagemaker.py` can use a
simple `tf.saved_model.save()` call — no serialisation workarounds needed.

---

## Setup (first time only)

You need Python 3.11 (TensorFlow doesn't support 3.12+ yet).

```bash
# Select Python 3.11 via asdf
asdf shell python 3.11.x

cd local-training

# Create a virtual environment
python -m venv .venv
source .venv/bin/activate

# Install dependencies (tensorflow-metal gives you M-series GPU acceleration)
pip install -r requirements.txt
```

---

## Workflow

### Step 1 — Download the data

```bash
AWS_PROFILE=nakom.is-sandbox python download_data_multiclass.py
```

This creates the `data_multiclass/` directory structure shown in the diagram above.
It's safe to re-run — already-downloaded images are skipped.

Optional flags:
```
--dest data_multiclass   Where to put the data (default: data_multiclass)
--max-nocat 600          Cap NoCat to limit class imbalance (default: 600)
--manual-pct 1.0         Percentage held back for manual testing (default: 1.0)
--val-split 0.2          Fraction of remaining used for validation (default: 0.2)
```

### Step 2 — Find a good learning rate (optional but recommended)

Before committing to a full training run, find a good learning rate:

```bash
python train_multiclass.py --find-lr
```

This runs a learning rate range test (1e-6 → 1e-1 over 100 steps), prints the
suggested rate, and saves `lr_finder.png`. Look for the point of steepest descent
before the loss diverges. Use that value (or 3–10× lower) as `--lr`.

### Step 3 — Train

```bash
python train_multiclass.py
```

Or with a learning rate from the finder:
```bash
python train_multiclass.py --lr 3e-4
```

You'll see output like this each epoch:
```
Epoch 12/50
 - loss: 0.3241 - accuracy: 0.8812 - val_loss: 0.2918 - val_accuracy: 0.9043
```

`val_accuracy` is the one to watch. Training stops automatically once it stops
improving (`--patience 10` means 10 epochs with no improvement).

The best model is saved to `models_multiclass/` automatically.

Optional flags:
```
--lr 0.0001          Learning rate (default: 0.0001; use --find-lr first)
--epochs 50          Max epochs before giving up (default: 50)
--batch-size 16      Images processed at once
--patience 10        Early stopping patience in epochs (default: 10)
--train-base         Also fine-tune the EfficientNetV2B1 base (slower, often better)
--dropout 0.3        Dropout rate (default: 0.3)
--l2 0.0001          L2 regularisation weight (default: 0.0001)
```

**Recommended starting point:** `--find-lr` first, then `--lr <suggested> --train-base`

`--train-base` unfreezes the EfficientNetV2B1 backbone so it adapts to cat-specific
visual patterns rather than relying purely on ImageNet features. It runs slower per
epoch but reaches meaningfully higher accuracy — especially for visually similar
classes (Boots/Kappa/Chi). Early stopping typically kicks in around epoch 20–25.

### Step 4 — Troubleshooting

| Problem | Things to try |
|---------|--------------|
| Low accuracy overall | Add `--train-base`; run `--find-lr` and set a better `--lr` |
| Overfitting (train acc >> val acc) | Higher `--dropout 0.5`; higher `--l2 0.001` |
| One class dominates | Adjust per-class caps in the download script |

### Step 5 — Test with the manual test set

```bash
python predict.py data_multiclass/manual_test/Boots/
python predict.py data_multiclass/manual_test/Chi/
```

Or test a specific image:
```bash
python predict.py data_multiclass/manual_test/Boots/2025-07-29T21-20-54-225Z.jpg
```

---

## Deploying to production

Once you're happy with the model:

```bash
./deploy_model.sh
```

This script:
1. Exports `models_multiclass/best_model.keras` to TF Serving SavedModel format
2. Packages it as `model.tar.gz`
3. Uploads to S3
4. Creates a new SageMaker model + endpoint config (timestamped names)
5. Updates the live `bootboots` endpoint in-place
6. Waits for `InService`

Remember to also update `CAT_NAMES` in `infra/lambda/infer-lambda/src/handler.ts`
and redeploy the Lambda if the class list has changed.

---

## Sandbox app integration

The sandbox labelling UI shows a prediction badge for each image using a local
inference server. The badge appears only when the server is running — entirely opt-in.

```
Browser (sandbox.nakomis.com)            Your Mac
┌──────────────────────────────┐         ┌────────────────────────────────┐
│  BootBootsPage.tsx           │         │  serve.py  (localhost:8765)    │
│                              │         │                                │
│  [cat image]                 │  POST   │  POST /predict                 │
│  ┌──────────────────────┐    │────────►│  raw JPEG bytes                │
│  │ Boots  92%           │    │◄────────│  { prediction, confidence }    │
│  └──────────────────────┘    │         │                                │
│  (green = Boots, grey = not) │         │  loads models_multiclass/      │
└──────────────────────────────┘         │         best_model.keras       │
                                         └────────────────────────────────┘
```

Start the server:
```bash
./serve.sh
```

Or manually:
```bash
source .venv/bin/activate
python serve.py
```

---

## Files

| File | What it does |
|------|-------------|
| `download_data_multiclass.py` | Downloads images from S3/DynamoDB, one class per cat |
| `download_data.py` | Legacy binary (Boots/NotBoots) download script |
| `train_multiclass.py` | Trains the EfficientNetV2B1 seven-class classifier |
| `train_local.py` | Legacy binary (Boots/NotBoots) training script |
| `predict.py` | Runs the trained model on one or more images |
| `serve.py` | Inference server for the sandbox app badge (localhost:8765) |
| `serve.sh` | Convenience script to start `serve.py` in the venv |
| `export_for_sagemaker.py` | Exports `.keras` model to TF Serving SavedModel + `model.tar.gz` |
| `deploy_model.sh` | Full deploy pipeline: export → S3 upload → SageMaker endpoint update |
| `requirements.txt` | Python dependencies |
| `data/` | Created by `download_data.py` — not committed to git |
| `data_multiclass/` | Created by `download_data_multiclass.py` — not committed to git |
| `models/` | Created by `train_local.py` — not committed to git |
| `models_multiclass/` | Created by `train_multiclass.py` — not committed to git |
