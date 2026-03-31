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

Then re-run `train_local.py` and `predict.py` to see if it improved.

---

## Files

| File | What it does |
|------|-------------|
| `download_data.py` | Downloads images from S3/DynamoDB and organises them into splits |
| `train_local.py` | Trains MobileNetV2 on the local data using Metal GPU acceleration |
| `predict.py` | Runs the trained model on one or more images |
| `requirements.txt` | Python dependencies |
| `data/` | Created by `download_data.py` — not committed to git |
| `models/` | Created by `train_local.py` — not committed to git |

---

## Next Steps

### Sandbox app integration

The goal is to show a live prediction badge in the sandbox app's cat labelling UI
(`BootBootsPage.tsx`) as you browse images — so you can see what your locally-trained
model thinks, right alongside the image, without leaving the browser.

Since browsers can't load TF SavedModels directly, the plan is a two-part change:

```
Browser (sandbox.nakomis.com)            Your Mac
┌──────────────────────────────┐         ┌────────────────────────────┐
│  BootBootsPage.tsx           │         │  serve.py                  │
│                              │         │  (FastAPI, localhost:8765)  │
│  [cat image]                 │  fetch  │                            │
│  ┌──────────────────────┐    │────────►│  POST /predict             │
│  │ 🟢 Boots  92.4%      │    │◄────────│  { class, confidence }     │
│  └──────────────────────┘    │         │                            │
│                              │         │  loads models/saved_model/ │
└──────────────────────────────┘         └────────────────────────────┘
```

**Part 1 — `serve.py`** (new file in `local-training/`):
- FastAPI app that loads the SavedModel on startup
- Single endpoint: `POST /predict` — accepts a JPEG URL or raw bytes, returns `{ class, confidence }`
- CORS enabled so the browser can call it
- Run with: `python serve.py`

**Part 2 — `BootBootsPage.tsx`** (in `sandboxsite/sandbox-app/`):
- When displaying an image, fire a request to `http://localhost:8765/predict` with the image URL
- Show the result as a small badge (green for Boots, grey for NotBoots)
- Gracefully handle the case where `serve.py` isn't running (just show nothing)

This keeps the integration entirely opt-in — the badge only appears when you have
the local server running.
