#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib';
import { ApiGatewayStack } from '../lib/api-gateway';
import { IotDeviceStack } from '../lib/iot-device';
import { AiTrainingStack } from '../lib/ai-training-stack';
import { PanelStack } from '../lib/panel-stack';
import { GithubCiStack } from '../lib/github-ci-stack';
import { envConfig, isDeployEnv } from '../lib/env-config';

const npmEnvironment = process.env.NPM_ENVIRONMENT;
if (!isDeployEnv(npmEnvironment)) {
    throw new Error('NPM_ENVIRONMENT must be "sandbox" or "prod". Use `pnpm synth-sandbox`, `pnpm diff-sandbox` etc.');
}
const deployEnv = npmEnvironment;
const config = envConfig(deployEnv);

// The account comes from the profile the deploy runs under, never a literal.
const env = { account: process.env.CDK_DEFAULT_ACCOUNT, region: process.env.CDK_DEFAULT_REGION ?? 'eu-west-2' };

const app = new cdk.App();

const apiGateway = new ApiGatewayStack(app, 'BootBootsApiGatewayStack', { env, deployEnv, config });

new IotDeviceStack(app, 'BootBootsIoTStack', { env, deployEnv, api: apiGateway.api });

// Pass modelDataUrl context to deploy with a pre-trained model:
//   pnpm deploy-sandbox BootBootsAiTrainingStack --context modelDataUrl=s3://bucket/path/model.tar.gz
// Without it the stack has no endpoint, and deploying it deletes the live one.
const modelDataUrl = app.node.tryGetContext('modelDataUrl');
new AiTrainingStack(app, 'BootBootsAiTrainingStack', { env, modelDataUrl });

// The web panel's AWS access. Named before the repos merged; renaming would recreate the identity pool.
const panel = new PanelStack(app, 'NakomisBootbootsStack', {
    env,
    deployEnv,
    description: `BootBoots web panel: identity pool and its role (${deployEnv})`,
});
// The panel reads the API id the API stack writes to SSM.
panel.addDependency(apiGateway);

new GithubCiStack(app, 'BootBootsGithubCiStack', {
    env,
    deployEnv,
    config,
    description: `bootboots GitHub Actions role: deploy, publish web and firmware (${deployEnv})`,
});

// No MH-Version tag: a per-release tag on every resource makes every deploy update everything (PIPE-8).
// The release version lives in the deployment tracker and the build artefacts.
cdk.Tags.of(app).add('MH-Project', 'bootboots');
