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
- The `bootboots-trigger-training` Lambda's IAM policy requires `sagemaker:AddTags` (needed when `CreateTrainingJob` includes Tags) - this is already in the policy as of Feb 2026.
- Invoke the trigger Lambda with `--invocation-type Event` (async) - data prep takes ~90s and will cause CLI read timeout if invoked synchronously.
- **Old training images** (pre-2026, numeric filenames like `540.jpeg`) were migrated in Feb 2026 from `bootbootstraining` S3 bucket into `bootboots-images-{account}-{region}/catcam-training/`. Both old and new images now live under the same prefix and are accessible to the data prep Lambda.

## SSM Parameters

Credentials are stored in SSM Parameter Store with the `BootsBoots` prefix:
- `/BootsBoots/cert_pem` - Certificate PEM
- `/BootsBoots/priv_key` - Private key
