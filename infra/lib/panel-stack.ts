import * as cdk from 'aws-cdk-lib';
import * as cognito from 'aws-cdk-lib/aws-cognito';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as ssm from 'aws-cdk-lib/aws-ssm';
import type { Construct } from 'constructs';
import { BOOTBOOTS, type DeployEnv, githubSubjects, REPO } from './env-config';

export interface BootbootsStackProps extends cdk.StackProps {
  deployEnv: DeployEnv;
  githubOidcProviderArn: string;
}

/**
 * The BootBoots panel's AWS access: an identity pool on the shared user pool (shell client) whose
 * authenticated role can do exactly what the pages do, plus the GitHub CI role that publishes the remote.
 * Device control goes through Fleet (JWT), so no IoT permissions are needed here.
 */
export class BootbootsStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props: BootbootsStackProps) {
    super(scope, id, props);

    const { deployEnv } = props;
    const userPoolId = ssm.StringParameter.valueForStringParameter(this, `/nakomis-infra/${deployEnv}/cognito/user-pool-id`);
    const shellClientId = ssm.StringParameter.valueForStringParameter(this, `/nakomis-shell/${deployEnv}/cognito/client-id`);

    // ── Identity pool ──────────────────────────────────────────────────────
    const identityPool = new cognito.CfnIdentityPool(this, 'IdentityPool', {
      identityPoolName: `nakomis_bootboots_${deployEnv}`,
      allowUnauthenticatedIdentities: false,
      cognitoIdentityProviders: [
        {
          providerName: `cognito-idp.${this.region}.amazonaws.com/${userPoolId}`,
          clientId: shellClientId,
          serverSideTokenCheck: false,
        },
      ],
    });

    const userRole = new iam.Role(this, 'AuthenticatedRole', {
      roleName: `nakomis-bootboots-user-${deployEnv}`,
      description: 'Signed-in users of the BootBoots panel in nakomis-shell',
      assumedBy: new iam.FederatedPrincipal(
        'cognito-identity.amazonaws.com',
        {
          StringEquals: { 'cognito-identity.amazonaws.com:aud': identityPool.ref },
          'ForAnyValue:StringLike': { 'cognito-identity.amazonaws.com:amr': 'authenticated' },
        },
        'sts:AssumeRoleWithWebIdentity',
      ),
      maxSessionDuration: cdk.Duration.hours(1),
    });

    // Cat labelling and bounding: scan for work, claim and label records.
    userRole.addToPolicy(
      new iam.PolicyStatement({
        actions: ['dynamodb:Scan', 'dynamodb:UpdateItem'],
        resources: [`arn:aws:dynamodb:${this.region}:${this.account}:table/${BOOTBOOTS.catadataTable}`],
      }),
    );
    // Training images shown while labelling and bounding.
    userRole.addToPolicy(
      new iam.PolicyStatement({
        actions: ['s3:GetObject'],
        resources: [
          `arn:aws:s3:::${BOOTBOOTS.imagesBucket(this.account, this.region)}/${BOOTBOOTS.trainingPrefix}*`,
        ],
      }),
    );
    // Catches: the bootboots events API (IAM auth). Its Lambda presigns the image URLs itself.
    userRole.addToPolicy(
      new iam.PolicyStatement({
        actions: ['execute-api:Invoke'],
        resources: [`arn:aws:execute-api:${this.region}:${this.account}:${BOOTBOOTS.apiId}/*/GET/events`],
      }),
    );

    new cognito.CfnIdentityPoolRoleAttachment(this, 'IdentityPoolRoles', {
      identityPoolId: identityPool.ref,
      roles: { authenticated: userRole.roleArn },
    });

    // ── CI ─────────────────────────────────────────────────────────────────
    const oidc = iam.OpenIdConnectProvider.fromOpenIdConnectProviderArn(this, 'GithubOidc', props.githubOidcProviderArn);
    const ciRole = new iam.Role(this, 'CiRole', {
      roleName: `nakomis-nakomis-bootboots-github-ci-${deployEnv}`,
      description: `Assumed by nakomis-bootboots GitHub Actions CI (${deployEnv})`,
      assumedBy: new iam.WebIdentityPrincipal(oidc.openIdConnectProviderArn, {
        StringEquals: { 'token.actions.githubusercontent.com:aud': 'sts.amazonaws.com' },
        StringLike: { 'token.actions.githubusercontent.com:sub': githubSubjects(REPO) },
      }),
      managedPolicies: [
        iam.ManagedPolicy.fromManagedPolicyArn(
          this,
          'RemoteDeployer',
          ssm.StringParameter.valueForStringParameter(this, `/nakomis-shell/${deployEnv}/remote-deployer-policy-arn`),
        ),
      ],
    });
    ciRole.addToPolicy(
      new iam.PolicyStatement({
        actions: ['sts:AssumeRole'],
        resources: [`arn:aws:iam::${this.account}:role/cdk-hnb659fds-*`],
      }),
    );
    // The web build bakes the identity pool id in from SSM.
    ciRole.addToPolicy(
      new iam.PolicyStatement({
        actions: ['ssm:GetParameter'],
        resources: [`arn:aws:ssm:${this.region}:${this.account}:parameter/nakomis-bootboots/*`],
      }),
    );

    // ── Exports ────────────────────────────────────────────────────────────
    new ssm.StringParameter(this, 'IdentityPoolIdParam', {
      parameterName: `/nakomis-bootboots/${deployEnv}/identity-pool-id`,
      stringValue: identityPool.ref,
    });

    new cdk.CfnOutput(this, 'IdentityPoolId', { value: identityPool.ref });
    new cdk.CfnOutput(this, 'UserRoleArn', { value: userRole.roleArn });
    new cdk.CfnOutput(this, 'CiRoleArn', { value: ciRole.roleArn });
  }
}
