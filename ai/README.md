# BootBoots AI Training Guide

This guide covers training a cat recognition model using SageMaker and deploying it as a serverless inference endpoint.

## Overview

The system trains a transfer learning model (MobileNet v2 base) to classify images into 7 classes:
- **Boots**, **Chi**, **Kappa**, **Mu**, **Tau**, **Wolf** (the cats)
- **NoCat** (no cat detected)

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

Infrastructure lives in the main `infra/` directory:

```
infra/
├── bin/
│   └── infra.ts                 # CDK app (includes AiTrainingStack)
├── lib/
│   ├── ai-training-stack.ts     # Training & inference infrastructure
│   └── ...
├── lambda/
│   ├── ai-data-prep/            # Prepares training data
│   │   └── src/handler.ts
│   └── ai-training-trigger/     # Triggers SageMaker job
│       └── src/handler.ts
└── ...

ai/
├── README.md                    # This guide
└── scripts/                     # Utility scripts (optional)
```

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

```bash
AWS_PROFILE=nakom.is-sandbox aws lambda invoke \
  --function-name bootboots-trigger-training \
  --region eu-west-2 \
  response.json && cat response.json
```

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

The training uses optimized settings for this dataset:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `epochs` | 50 | Training iterations |
| `learningRate` | 0.0001 | Lower rate for fine-tuning |
| `batchSize` | 16 | Images per batch |
| `trainOnlyTopLayer` | false | Fine-tune entire network |
| `dropoutRate` | 0.3 | Regularization |

### Data Augmentation

Enabled by default to handle small dataset:
- Random horizontal/vertical flip
- Random rotation (±30%)
- Random zoom (±20%)

### Early Stopping

Training stops automatically when validation accuracy plateaus:
- Patience: 10 epochs
- Min delta: 0.001

## Addressing Class Imbalance

Current data distribution:
- NoCat: 1055 images (overrepresented)
- Tau: 188, Kappa: 188, Chi: 156, Boots: 117
- Wolf: 21, Mu: 10 (underrepresented)

**Implemented strategies:**
1. **Undersampling** - NoCat limited to 200 images
2. **Data augmentation** - Synthetic variations
3. **Stratified split** - Proportional validation set

**Recommendations:**
- Collect more images for Mu and Wolf
- Consider adding class weights (modify training trigger Lambda)

## Serverless Inference

The endpoint is configured for pay-per-request:

| Setting | Value |
|---------|-------|
| Memory | 2048 MB |
| Max concurrency | 5 |
| Cold start | 2-5 seconds |
| Cost per request | ~$0.00012 |

## Custom Training Parameters

Invoke the training Lambda with custom config:

```bash
AWS_PROFILE=nakom.is-sandbox aws lambda invoke \
  --function-name bootboots-trigger-training \
  --region eu-west-2 \
  --payload '{"epochs": 100, "learningRate": 0.00005, "skipDataPrep": false}' \
  response.json
```

Options:
- `skipDataPrep`: Skip data reorganization (use existing)
- `epochs`: Number of training epochs
- `learningRate`: Learning rate
- `batchSize`: Batch size
- `trainOnlyTopLayer`: Only train classifier layer

## Troubleshooting

### Training job fails immediately
- Check IAM role has S3 and ECR permissions
- Verify training data exists: `aws s3 ls s3://bootboots-training-data-975050268859/training/`

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

1. Deploy the training stack
2. Label more images for Mu and Wolf
3. Run training with current data
4. Evaluate results
5. Iterate on hyperparameters if needed
6. Deploy model to serverless endpoint
