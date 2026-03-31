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
                          download_data.py
                          (reads labels + downloads images)
                                   │
                                   ▼
          ┌────────────────────────────────────────────────────┐
          │                  local-training/data/               │
          │                                                      │
          │  training/         (≈79% of images)                 │
          │  ├── Boots/                                          │
          │  └── NotBoots/                                       │
          │                                                      │
          │  validation/       (≈20% of images)                 │
          │  ├── Boots/         ← used during training to        │
          │  └── NotBoots/         measure accuracy each epoch   │
          │                                                      │
          │  manual_test/      (1% of images)                   │
          │  ├── Boots/         ← NEVER seen during training;    │
          │  └── NotBoots/         you use these at the end      │
          └───────────────┬────────────────────────────────────┘
                          │
                    train_local.py
                    (MobileNetV2 on Metal GPU)
                          │
                          ▼
          ┌────────────────────────────┐
          │    local-training/models/  │
          │                            │
          │  saved_model/              │
          │  best_model.keras          │
          │  class_names.json          │
          └──────────────┬─────────────┘
                         │
                   predict.py
                   (run on manual_test/ images)
                         │
                         ▼
          Boots      87.3%  ████████████████░░░░  2025-07-29T…jpg
          NotBoots   94.1%  ██████████████████░░  2026-01-18T…jpg
          …
```

---

## The two filename formats

The catcam has used two naming schemes over time:

| Era | Format | Example |
|-----|--------|---------|
| Pre-2026 (old) | Numeric | `540.jpeg` |
| 2025 onwards | ISO timestamp | `2025-07-29T21-20-54-225Z.jpg` |

`download_data.py` handles both transparently. The old images also had a bug where they were saved without a JPEG end-of-file marker — the script patches those automatically as it downloads them.

---

## The binary classifier

Everything gets sorted into two buckets:

```
All labeled cats
      │
      ├── cat == "Boots"  →  class: Boots
      │
      └── cat == anything else  →  class: NotBoots
             (Chi, Tau, Kappa, Mu, Wolf, …)
```

The model learns one question: *"Is this Boots, or not?"*

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
AWS_PROFILE=nakom.is-sandbox python download_data.py
```

This creates the `data/` directory structure shown in the diagram above.
It's safe to re-run — already-downloaded images are skipped.

Optional flags:
```
--dest data          Where to put the data (default: data)
--max-notboots 600   Cap NotBoots to limit class imbalance (default: no cap)
--manual-pct 1.0     Percentage held back for manual testing (default: 1.0)
--val-split 0.2      Fraction of remaining used for validation (default: 0.2)
```

### Step 2 — Train

```bash
python train_local.py
```

You'll see output like this each epoch:
```
Epoch 12/50
 - loss: 0.3241 - accuracy: 0.8812 - val_loss: 0.2918 - val_accuracy: 0.9043
```

`val_accuracy` is the one to watch — it's how well the model does on images it
hasn't trained on. Training stops automatically once it stops improving
(`--patience 10` means 10 epochs with no improvement).

The best model is saved to `models/` automatically.

Optional flags:
```
--epochs 50          Max epochs before giving up (default: 50)
--batch-size 16      Images processed at once (larger = faster but needs more RAM)
--patience 10        Early stopping patience in epochs (default: 10)
--train-base         Also fine-tune MobileNetV2 itself, not just the top layer
                     (slower, sometimes squeezes out a few more % accuracy)
```

### Step 3 — Test with the manual test set

```bash
python predict.py data/manual_test/Boots/
python predict.py data/manual_test/NotBoots/
```

Or test a specific image:
```bash
python predict.py data/manual_test/Boots/2025-07-29T21-20-54-225Z.jpg
```

You'll see colour-coded output:
```
  Boots       92.4%  ██████████████████░░  2025-07-29T21-20-54-225Z.jpg
  NotBoots    87.1%  █████████████████░░░  2026-01-18T00-12-34-882Z.jpg
```

Green = Boots prediction, yellow = NotBoots prediction.

### Step 4 — Tweak and repeat

Look at the results. If something's wrong (e.g. Boots is being misclassified as
NotBoots), try adjusting the training:

| Problem | Things to try |
|---------|--------------|
| Low accuracy overall | More epochs (`--epochs 100`), lower learning rate, add `--train-base` |
| Overfitting (train acc >> val acc) | Higher dropout (`--dropout 0.5`), higher l2 (`--l2 0.001`) |
| Class imbalance | Cap NotBoots (`--max-notboots 200`) to match Boots count more closely |

**Recommended starting point (from experimentation):** `--train-base --batch-size 32`

`--train-base` unfreezes all 2.25M MobileNetV2 parameters so the feature
extractor adapts to cat-specific visual patterns, not just ImageNet features.
It runs ~10× slower per epoch but reaches meaningfully higher accuracy —
especially for visually similar classes (Boots/Kappa/Chi). Early stopping
typically kicks in around epoch 20–25.

Then re-run `train_local.py` and `predict.py` to see if it improved.

---

## Files

| File | What it does |
|------|-------------|
| `download_data.py` | Downloads images from S3/DynamoDB and organises them into binary splits |
| `download_data_multiclass.py` | Downloads images, keeping one class per cat |
| `train_local.py` | Trains a binary (Boots/NotBoots) MobileNetV2 classifier |
| `train_multiclass.py` | Trains a seven-class classifier (one class per cat) |
| `predict.py` | Runs the trained model on one or more images |
| `export_tfjs.py` | Converts a `.keras` model to TFJs format for the local server |
| `serve/` | TypeScript Express inference server (see below) |
| `requirements.txt` | Python dependencies |
| `data/` | Created by `download_data.py` — not committed to git |
| `data_multiclass/` | Created by `download_data_multiclass.py` — not committed to git |
| `models/` | Created by `train_local.py` — not committed to git |
| `models_multiclass/` | Created by `train_multiclass.py` — not committed to git |

---

## Sandbox app integration

The sandbox labelling UI shows a prediction badge for each image using a local
TypeScript inference server. The badge appears only when the server is running —
entirely opt-in.

```
Browser (sandbox.nakomis.com)            Your Mac
┌──────────────────────────────┐         ┌────────────────────────────────┐
│  BootBootsPage.tsx           │         │  serve/  (localhost:8765)      │
│                              │         │                                │
│  [cat image]                 │  POST   │  POST /predict                 │
│  ┌──────────────────────┐    │────────►│  raw JPEG bytes                │
│  │ Boots  92%           │    │◄────────│  { prediction, confidence }    │
│  └──────────────────────┘    │         │                                │
│  (green = Boots, grey = not) │         │  loads models_multiclass/      │
└──────────────────────────────┘         │         tfjs_model/model.json  │
                                         └────────────────────────────────┘
```

The browser already has the image downloaded (as a blob URL). It fetches its own
blob and POSTs the raw JPEG bytes to the server — no AWS credentials needed on the
server side.

### Step 5 — Export the model to TFJs format

This is a one-time step after training (repeat if you retrain):

```bash
pip install tensorflowjs   # already in requirements.txt
python export_tfjs.py
```

This converts `models_multiclass/best_model.keras` →
`models_multiclass/tfjs_model/model.json` (+ weight shards).

For the binary model:
```bash
python export_tfjs.py --model models/best_model.keras
```

### Step 6 — Start the inference server

```bash
cd serve
npm install        # first time only
npm start
```

The server loads the model and listens on `http://localhost:8765`. Open the sandbox
app — a prediction badge will appear in the top-right corner of each image.

Switch between multiclass and binary models:
```bash
npm start -- --model ../models/tfjs_model
```
