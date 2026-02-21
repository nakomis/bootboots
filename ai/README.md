# BootBoots AI Training Guide

This guide covers training a cat recognition model using SageMaker and deploying it as a serverless inference endpoint.

## Overview

The system trains a transfer learning model (MobileNet v2 base) as a **binary classifier**:
- **Boots** — neighbour's cat to deter (trigger water spray)
- **NotBoots** — everyone else: household cats (Chi, Kappa, Mu, Tau, Wolf) and no-cat frames

> **Previous multi-class model** (7 classes) is preserved at git tag `multi-cat-recognition`.

### Architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  catcam-training│     │   Data Prep      │     │ training-data   │
│  (raw images)   │────▶│   Lambda         │────▶│ (by class)      │
│  + catadata DB  │     │                  │     │                 │
└─────────────────┘     └──────────────────┘     └────────┬────────┘
                                                          │
                        ┌──────────────────┐              │
                        │  SageMaker       │◀─────────────┘
                        │  Training Job    │
                        └────────┬─────────┘
                                 │
                        ┌────────▼─────────┐
                        │  Model Artifacts │
                        │  (S3)            │
                        └────────┬─────────┘
                                 │
                        ┌────────▼─────────┐
                        │  Serverless      │
                        │  Endpoint        │
                        └──────────────────┘
```

## Project Structure

```
infra/
├── bin/
│   └── infra.ts                 # CDK app (includes AiTrainingStack)
├── lib/
│   ├── ai-training-stack.ts     # Training & inference infrastructure
│   └── ...
├── lambda/
│   ├── ai-data-prep/            # Prepares training data from catadata table
│   │   └── src/handler.ts
│   └── ai-training-trigger/     # Triggers SageMaker training job
│       └── src/handler.ts
├── training/
│   └── train.py                 # Custom MobileNet v2 training script
└── ...

ai/
├── README.md                    # This guide
└── scripts/                     # Utility scripts (optional)
```

### Training Script

`infra/training/train.py` is a self-contained Python/Keras script that runs inside the SageMaker TensorFlow container. It:
- Loads images from the `training/` and `validation/` S3 input channels
- Fine-tunes MobileNet v2 (ImageNet weights) with a custom classification head
- Applies data augmentation, dropout, L2 regularisation, and early stopping
- Saves a versioned SavedModel to `/opt/ml/model/1/` (TF Serving format)
- Writes `class_names.json` alongside the model for inference mapping

**CDK packages `infra/training/` as a zip asset and uploads it to S3 automatically on every `cdk deploy`.** The trigger Lambda passes the S3 URI to SageMaker as `sagemaker_submit_directory`. No manual script management needed.

## Quick Start

### 1. Deploy the Training Stack

```bash
cd infra
AWS_PROFILE=nakom.is-sandbox npx cdk deploy BootBootsAiTrainingStack
```

This creates:
- `bootboots-training-data-{account}` - S3 bucket for organized training data
- `bootboots-models-{account}` - S3 bucket for model artifacts
- `bootboots-data-prep` - Lambda for preparing training data
- `bootboots-trigger-training` - Lambda for starting training jobs
- IAM role for SageMaker

### 2. Trigger Training

Use `--invocation-type Event` (async) - the data prep step takes ~90 seconds which can cause the CLI to time out if invoked synchronously, even though the Lambda itself completes fine.

```bash
AWS_PROFILE=nakom.is-sandbox aws lambda invoke \
  --function-name bootboots-trigger-training \
  --region eu-west-2 \
  --invocation-type Event \
  --payload '{}' \
  --cli-binary-format raw-in-base64-out \
  response.json
```

A `StatusCode: 202` response means the job was accepted. Monitor via SageMaker console or the commands below.

This will:
1. Read labeled images from `catadata` DynamoDB table
2. Copy images to organized folder structure (training/validation split)
3. Undersample the NoCat class to prevent bias
4. Start a SageMaker training job

### 3. Monitor Training

```bash
# Check training job status
AWS_PROFILE=nakom.is-sandbox aws sagemaker describe-training-job \
  --training-job-name <job-name-from-response> \
  --region eu-west-2 \
  --query '{Status:TrainingJobStatus,Metrics:FinalMetricDataList}'

# Watch CloudWatch logs
AWS_PROFILE=nakom.is-sandbox aws logs tail \
  /aws/sagemaker/TrainingJobs \
  --follow \
  --region eu-west-2
```

### 4. Deploy Trained Model (After Training Completes)

```bash
# Get the model artifact path from completed training job
AWS_PROFILE=nakom.is-sandbox aws sagemaker describe-training-job \
  --training-job-name <job-name> \
  --region eu-west-2 \
  --query 'ModelArtifacts.S3ModelArtifacts' \
  --output text

# Deploy with the model
AWS_PROFILE=nakom.is-sandbox npx cdk deploy BootBootsAiTrainingStack \
  --context modelDataUrl=s3://bootboots-models-975050268859/training-output/<job-name>/output/model.tar.gz
```

## Training Configuration

### Hyperparameters

Defaults are baked into the trigger Lambda; override by passing a payload (see Custom Training Parameters below).

| Lambda parameter | Default | Description |
|-----------------|---------|-------------|
| `epochs` | 50 | Max training epochs (early stopping usually triggers sooner) |
| `learningRate` | 0.0001 | Adam optimiser learning rate |
| `batchSize` | 16 | Images per batch |
| `trainOnlyTopLayer` | false | `false` = fine-tune whole network, `true` = classifier head only |

Fixed in `train.py` (edit the script to change):

| Setting | Value | Description |
|---------|-------|-------------|
| Dropout | 0.3 | Applied before final Dense layer |
| L2 regularisation | 0.0001 | Applied to Dense layer weights |
| Augmentation | on | Random flip, rotation ±30%, zoom ±20% |
| Early stopping patience | 10 epochs | Monitors `val_accuracy`, restores best weights |
| Early stopping min delta | 0.001 | Minimum improvement to reset patience counter |
| Image size | 224×224 | MobileNet v2 native input size |

## Addressing Class Imbalance

Binary class distribution (as of Feb 2026):
- **Boots**: ~190 images
- **NotBoots**: ~2874 images (Chi 196, Tau 255, Kappa 235, Mu 62, Wolf 59, NoCat 2067) → capped at 600

The data prep Lambda caps NotBoots at `MAX_NOTBOOTS_SAMPLES` (default 600, ~3× Boots) by randomly sampling from the full NotBoots pool, ensuring variety across all cat types and NoCat frames.

**Source images:**
- New images (catcam): `bootboots-images-{account}-{region}/catcam-training/` (timestamp-named `.jpg` files)
- Old images (pre-2026): numeric-named `.jpeg` files in the same prefix (migrated from `bootbootstraining` bucket in Feb 2026; were truncated JPEGs, patched with FF D9 EOI marker by the data prep Lambda on copy)

**Implemented strategies:**
1. **Undersampling NotBoots** — capped at `MAX_NOTBOOTS_SAMPLES` (default 600)
2. **Data augmentation** — synthetic variations
3. **Stratified split** — proportional validation set

**To improve Boots detection:**
- Collect more Boots images from the catcam (he visits regularly)
- More diverse angles/lighting in Boots training data

## Serverless Inference

The endpoint is configured for pay-per-request:

| Setting | Value |
|---------|-------|
| Memory | 2048 MB |
| Max concurrency | 5 |
| Cold start | 2-5 seconds |
| Cost per request | ~$0.00012 |

## Custom Training Parameters

Pass a JSON payload to override defaults (use `--invocation-type Event` for async):

```bash
AWS_PROFILE=nakom.is-sandbox aws lambda invoke \
  --function-name bootboots-trigger-training \
  --region eu-west-2 \
  --invocation-type Event \
  --payload '{"epochs": 100, "learningRate": 0.00005, "batchSize": 32}' \
  --cli-binary-format raw-in-base64-out \
  response.json
```

| Option | Type | Description |
|--------|------|-------------|
| `skipDataPrep` | bool | Skip S3 data reorganisation (reuse existing training/validation split) |
| `epochs` | int | Max training epochs |
| `learningRate` | float | Adam learning rate |
| `batchSize` | int | Images per batch |
| `trainOnlyTopLayer` | bool | Only train the classifier head (faster, less accurate) |

## Troubleshooting

### Training job fails immediately
- Check IAM role has S3 and ECR permissions
- Verify training data exists: `aws s3 ls s3://bootboots-training-data-975050268859/training/`
- If error is `AccessDeniedException: sagemaker:AddTags` - the trigger Lambda's IAM policy is missing `sagemaker:AddTags`. Add it to the policy in `infra/lib/ai-training-stack.ts` and redeploy.

### CDK deploy fails with Docker error
- CDK requires Docker running to bundle Lambda assets. Open Docker Desktop and retry.

### Poor accuracy
- Check class distribution in data prep output
- Try more epochs or lower learning rate
- Collect more data for underrepresented classes

### Data prep timeout
- Lambda has 10 minute timeout
- For very large datasets, consider Step Functions

### Endpoint cold start too slow
- Increase memory (max 6144 MB)
- Consider provisioned concurrency for latency-critical apps

## Cost Estimates

| Resource | Cost | Notes |
|----------|------|-------|
| Training (ml.m5.xlarge) | ~$0.23/hr | ~30-60 min typical |
| Serverless inference | ~$0.00012/req | Plus memory cost |
| S3 storage | ~$0.023/GB/mo | Training data + models |

**Estimated monthly:**
- Low usage (100 inferences/day): < $5
- Medium (1000/day): ~$15
- Training runs: ~$0.50 each

## Next Steps

1. ~~Deploy the training stack~~ ✓ Done
2. ~~Write custom training script~~ ✓ Done (`infra/training/train.py`, Feb 2026)
3. Evaluate first training run - check validation accuracy, especially for Mu and Wolf
4. Iterate on hyperparameters or collect more Mu/Wolf images if needed
5. Deploy model to serverless endpoint (see step 4 of Quick Start)
6. Label more images for Mu and Wolf, retrain to improve accuracy on those classes
