# Claude Code Guidelines for BootBoots Infrastructure

## AWS Configuration

Always use the sandbox profile when working with AWS CLI:

```bash
export AWS_PROFILE=nakom.is-sandbox
```

## File Locations

- **CDK stacks**: `lib/` directory

## CDK Deployment Requirements

**Docker must be running** before deploying - CDK uses Docker to bundle NodejsFunction Lambda assets. If you see a Docker daemon connection error, ask the user to open Docker Desktop and retry.

## IoT Resources

The `lib/iot-device.ts` stack uses `ThingWithCert` from `cdk-iot-core-certificates-v3` and creates:
- IoT Thing (`BootBootsThing`)
- Certificate (auto-generated, stored in SSM Parameter Store)
- IAM Role and Policy for device access
- Role Alias for credential provider
- IoT Policy for assuming role with certificate

## AI Training

- Training stack: `lib/ai-training-stack.ts`
- Training script: `training/train.py` - custom MobileNet v2 Keras script. CDK packages this directory as an S3 asset and uploads it automatically on every `cdk deploy`. The trigger Lambda reads the bucket/key from env vars and passes the S3 URI to SageMaker as `sagemaker_submit_directory`. **If you modify `train.py`, just run `cdk deploy BootBootsAiTrainingStack` - no manual packaging needed.**
- The `bootboots-trigger-training` Lambda's IAM policy requires `sagemaker:AddTags` (needed when `CreateTrainingJob` includes Tags) - this is already in the policy as of Feb 2026.
- Invoke the trigger Lambda with `--invocation-type Event` (async) - data prep takes ~90s and will cause CLI read timeout if invoked synchronously.
- **Old training images** (pre-2026, numeric filenames like `540.jpeg`) were migrated in Feb 2026 from `bootbootstraining` S3 bucket into `bootboots-images-{account}-{region}/catcam-training/`. They were truncated JPEGs (missing FF D9 EOI marker) — the data prep Lambda patches them on copy.
- **Binary classifier** (as of Feb 2026): model classifies Boots vs NotBoots. Previous 7-class model is at git tag `multi-cat-recognition`. `CAT_CLASSES = ['Boots', 'NotBoots']` in the stack. NotBoots is capped at `MAX_NOTBOOTS_SAMPLES` (default 600) to limit class imbalance.

## SSM Parameters

Credentials are stored in SSM Parameter Store with the `BootsBoots` prefix:
- `/BootsBoots/cert_pem` - Certificate PEM
- `/BootsBoots/priv_key` - Private key
