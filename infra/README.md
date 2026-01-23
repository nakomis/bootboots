# BootBoots Infrastructure

AWS CDK infrastructure for the BootBoots IoT system.

## Prerequisites

- Node.js and npm/yarn
- AWS CLI configured with `nakom.is-sandbox` profile

## Stacks

### IoT Device Stack
Creates AWS IoT resources for device authentication:
- IoT Thing (`BootBootsThing`)
- Auto-generated certificate stored in SSM Parameter Store
- IAM Role and Policy for device access
- Role Alias for credential provider
- IoT Policy for assuming role with certificate

### Firmware Cleanup Stack
Lambda function that manages firmware versions in S3:
- Triggered by S3 upload events (suffix: `firmware.bin`)
- Keeps only the 3 most recent versions per project
- Auto-updates `manifest.json` with version metadata
- See `lambda/firmware-cleanup/README.md` for details

### Infer Lambda Stack
Lambda function for AI inference (cat recognition).

## Deployment

```bash
# Install dependencies
npm install

# Set AWS profile
export AWS_PROFILE=nakom.is-sandbox

# Deploy all stacks
npx cdk deploy --all

# Or deploy specific stacks
npx cdk deploy IotDeviceStack
npx cdk deploy BootBootsFirmwareCleanupStack
```

## SSM Parameters

After deployment, the following parameters are available in SSM:
- `/BootsBoots/cert_pem` - Device certificate PEM
- `/BootsBoots/priv_key` - Device private key

## Project Structure

```
infra/
|-- lib/                    # CDK stack definitions
|-- lambda/
|   |-- firmware-cleanup/   # Firmware version management Lambda
|   +-- infer-lambda/       # AI inference Lambda
|-- bin/                    # CDK app entry point
+-- cdk.out/                # Synthesized CloudFormation (gitignored)
```

## CDK Commands

* `npm run build`   compile typescript to js
* `npm run watch`   watch for changes and compile
* `npm run test`    perform the jest unit tests
* `npx cdk deploy`  deploy this stack to your default AWS account/region
* `npx cdk diff`    compare deployed stack with current state
* `npx cdk synth`   emits the synthesized CloudFormation template
