import * as cdk from 'aws-cdk-lib';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as ssm from 'aws-cdk-lib/aws-ssm';
import type { Construct } from 'constructs';
import { type DeployEnv, type EnvConfig, githubSubjects, REPO, SSM } from './env-config';

export interface GithubCiStackProps extends cdk.StackProps {
  deployEnv: DeployEnv;
  config: EnvConfig;
}

/**
 * The role GitHub Actions in nakomis/bootboots assumes (BOOT-8): deploy the CDK app (through the
 * CDK bootstrap roles), read this repo's SSM parameters, publish firmware under BootBoots/ in this
 * environment's firmware bucket, publish the web panel to the shell bucket and invalidate it, and
 * record deployments in the shared tracker (which allow-lists the role in nakomis-infra).
 *
 * Sandbox trusts any ref of the repository (PRs run as `pull_request`; forks get no OIDC token).
 * Prod trusts only `main`: only Martin merges to main. A job that names a GitHub environment (the
 * shared cdk-deploy and deploy-static workflows do) gets the subject `environment:<name>` instead of
 * its ref, so prod also trusts `environment:prod`; that environment must allow deployments from
 * `main` only (its deployment-branch rule), which keeps the 'main only' guarantee.
 */
export class GithubCiStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props: GithubCiStackProps) {
    super(scope, id, props);
    const { deployEnv, config } = props;

    const provider = iam.OpenIdConnectProvider.fromOpenIdConnectProviderArn(
      this,
      'GithubOidc',
      `arn:aws:iam::${this.account}:oidc-provider/token.actions.githubusercontent.com`,
    );
    const param = (name: string) => `arn:aws:ssm:${this.region}:${this.account}:parameter${name}`;

    const role = new iam.Role(this, 'CiRole', {
      roleName: `nakomis-bootboots-github-ci-${deployEnv}`,
      description: `Assumed by bootboots GitHub Actions to deploy and publish (${deployEnv})`,
      maxSessionDuration: cdk.Duration.hours(1),
      assumedBy: new iam.WebIdentityPrincipal(provider.openIdConnectProviderArn, {
        StringEquals: { 'token.actions.githubusercontent.com:aud': 'sts.amazonaws.com' },
        StringLike: {
          'token.actions.githubusercontent.com:sub':
            deployEnv === 'prod'
              ? [...githubSubjects(REPO, 'ref:refs/heads/main'), ...githubSubjects(REPO, 'environment:prod')]
              : githubSubjects(REPO),
        },
      }),
      // Sync to the shell bucket's remotes/bootboots/ and invalidate CloudFront: the shell's own policy.
      managedPolicies: [
        iam.ManagedPolicy.fromManagedPolicyArn(
          this,
          'RemoteDeployer',
          ssm.StringParameter.valueForStringParameter(this, SSM.shellRemoteDeployerPolicyArn(deployEnv)),
        ),
      ],
    });

    // CDK deploys go through the bootstrap roles.
    role.addToPolicy(
      new iam.PolicyStatement({
        actions: ['sts:AssumeRole'],
        resources: [`arn:aws:iam::${this.account}:role/cdk-hnb659fds-*`],
      }),
    );
    role.addToPolicy(
      new iam.PolicyStatement({
        actions: ['ssm:GetParameter', 'ssm:GetParameters'],
        resources: [param('/nakomis-bootboots/*'), param('/bootboots/*')],
      }),
    );
    // SecureStrings use the account's aws/ssm key: decrypt only through SSM.
    role.addToPolicy(
      new iam.PolicyStatement({
        actions: ['kms:Decrypt'],
        resources: ['*'],
        conditions: { StringEquals: { 'kms:ViaService': `ssm.${this.region}.amazonaws.com` } },
      }),
    );
    // Firmware for Fleet OTA. GetObject also answers HeadObject.
    role.addToPolicy(
      new iam.PolicyStatement({
        actions: ['s3:PutObject', 's3:GetObject'],
        resources: [`arn:aws:s3:::${config.firmwareBucket}/BootBoots/*`],
      }),
    );
    // So a missing object answers 404, not 403 (unconditional: S3's implicit check carries no prefix).
    role.addToPolicy(
      new iam.PolicyStatement({
        actions: ['s3:ListBucket'],
        resources: [`arn:aws:s3:::${config.firmwareBucket}`],
      }),
    );
    // The deployment tracker (nakomis-infra) lives in the prod account; for sandbox the call is
    // cross-account and the tracker's resource policy is what admits this role.
    role.addToPolicy(
      new iam.PolicyStatement({
        actions: ['execute-api:Invoke'],
        resources: [`arn:aws:execute-api:${this.region}:*:*/*/*/deployments/*`],
      }),
    );

    new cdk.CfnOutput(this, 'CiRoleArn', { value: role.roleArn });
  }
}
