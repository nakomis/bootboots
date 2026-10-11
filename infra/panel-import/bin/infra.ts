#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib';
import { BootbootsStack } from '../lib/bootboots-stack';
import { ACCOUNTS, type DeployEnv } from '../lib/env-config';

const app = new cdk.App();
const deployEnv = (process.env.NPM_ENVIRONMENT ?? 'sandbox') as DeployEnv;
if (deployEnv !== 'sandbox') throw new Error('BootBoots only exists in the sandbox account');
const env = ACCOUNTS[deployEnv];

new BootbootsStack(app, 'NakomisBootbootsStack', {
  env,
  deployEnv,
  githubOidcProviderArn: `arn:aws:iam::${env.account}:oidc-provider/token.actions.githubusercontent.com`,
});

cdk.Tags.of(app).add('MH-Project', 'nakomis-bootboots');
