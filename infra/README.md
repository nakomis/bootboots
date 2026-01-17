# BootBoots Infrastructure

CDK infrastructure for the BootBoots IoT device.

## Prerequisites

- Node.js and npm/yarn
- AWS CLI configured with `nakom.is-sandbox` profile

## Deployment

Deploy the IoT stack:

```bash
npx cdk deploy IotDeviceStack
```

The `ThingWithCert` construct automatically:
- Creates the IoT Thing (`BootBootsThing`)
- Generates and registers a certificate
- Stores credentials in SSM Parameter Store with the `BootsBoots` prefix

## SSM Parameters

After deployment, the following parameters are available in SSM:
- `/BootsBoots/cert_pem` - Certificate PEM
- `/BootsBoots/priv_key` - Private key

## CDK Commands

* `npm run build`   compile typescript to js
* `npm run watch`   watch for changes and compile
* `npm run test`    perform the jest unit tests
* `npx cdk deploy`  deploy this stack to your default AWS account/region
* `npx cdk diff`    compare deployed stack with current state
* `npx cdk synth`   emits the synthesized CloudFormation template
