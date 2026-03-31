# Running Training Locally

## Prerequisites

Python 3.11 is required (TensorFlow 2.12 does not support 3.13+):

```bash
asdf shell python 3.11.x
pip install tensorflow==2.12.* pillow
```

## Data layout

Training data must be organised into class subfolders:

```
data/
  training/
    Boots/
      img1.jpg
      img2.jpg
    NotBoots/
      img1.jpg
  validation/
    Boots/
      ...
    NotBoots/
      ...
```

Source images are in S3 under `catcam-training/` (Boots) and `catcam-images/` (NotBoots) in the images bucket (`bootboots-images-<account>-eu-west-2`). Download them with:

```bash
BUCKET=$(AWS_PROFILE=nakom.is-sandbox aws s3api list-buckets \
  --query "Buckets[?starts_with(Name, 'bootboots-images')].Name | [0]" \
  --output text)

AWS_PROFILE=nakom.is-sandbox aws s3 sync s3://$BUCKET/catcam-training/ data/training/Boots/
AWS_PROFILE=nakom.is-sandbox aws s3 sync s3://$BUCKET/catcam-images/   data/training/NotBoots/
```

Split roughly 80/20 into `data/validation/` manually.

> **Note:** NotBoots images are capped at 600 during SageMaker data prep to limit class imbalance — consider doing the same locally.

## Running

```bash
cd infra/training

SM_CHANNEL_TRAINING=data/training \
SM_CHANNEL_VALIDATION=data/validation \
SM_MODEL_DIR=output/model \
SM_OUTPUT_DATA_DIR=output/data \
python train.py
```

## Hyperparameters

Key hyperparameters can be overridden via a JSON file at `/opt/ml/input/config/hyperparameters.json`, or by editing the defaults at the top of `train.py`:

| Parameter                  | Default | Notes                                    |
|----------------------------|---------|------------------------------------------|
| `epochs`                   | 50      | Reduced by early stopping                |
| `learning_rate`            | 0.0001  | Adam optimiser                           |
| `batch_size`               | 16      |                                          |
| `num_classes`              | **7**   | **Override to `2` for Boots/NotBoots**   |
| `dropout_rate`             | 0.3     |                                          |
| `regularizers_l2`          | 0.0001  | L2 kernel regularisation                 |
| `train_only_top_layer`     | False   | False = fine-tune full network           |
| `augmentation`             | True    | Random flip/rotation/zoom                |
| `early_stopping`           | True    |                                          |
| `early_stopping_patience`  | 10      | Epochs to wait before stopping           |
| `early_stopping_min_delta` | 0.001   | Minimum val_accuracy improvement         |

> The `num_classes` default in `train.py` is `7` (legacy multi-class model). For the current binary classifier always pass `num_classes=2`.

## Output

The SavedModel is written to `output/model/1/` with a `serving_default` signature that accepts base64-encoded JPEG bytes — no pre-resizing needed. A `class_names.json` is also written alongside the model.

## Deploying the retrained model

Once satisfied with local results, trigger a full SageMaker training run to produce a deployable artifact:

```bash
AWS_PROFILE=nakom.is-sandbox aws lambda invoke \
  --function-name bootboots-trigger-training \
  --region eu-west-2 \
  --invocation-type Event \
  --payload '{}' \
  --cli-binary-format raw-in-base64-out \
  /tmp/response.json
```

> Use `--invocation-type Event` (async) — data prep takes ~90s and will time out if invoked synchronously.

Monitor the job in the [SageMaker console](https://eu-west-2.console.aws.amazon.com/sagemaker/home?region=eu-west-2#/training-jobs) or via:

```bash
AWS_PROFILE=nakom.is-sandbox aws sagemaker describe-training-job \
  --training-job-name <job-name> \
  --region eu-west-2 \
  --query 'TrainingJobStatus'
```
