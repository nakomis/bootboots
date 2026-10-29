import * as cdk from 'aws-cdk-lib';
import * as cognito from 'aws-cdk-lib/aws-cognito';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as ssm from 'aws-cdk-lib/aws-ssm';
import type { Construct } from 'constructs';
import { BOOTBOOTS, type DeployEnv, githubSubjects, LEGACY_PANEL_REPO, SSM } from './env-config';

export interface PanelStackProps extends cdk.StackProps {
  deployEnv: DeployEnv;
}

/**
 * The BootBoots panel's AWS access: an identity pool on the shared user pool (shell client) whose
 * authenticated role can do exactly what the pages do, plus the old nakomis-bootboots CI role.
 * Device control goes through Fleet (JWT), so no IoT permissions are needed here.
 *
 * Deployed as NakomisBootbootsStack: renaming it would recreate the identity pool.
 */
export class PanelStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props: PanelStackProps) {
    super(scope, id, props);

    const { deployEnv } = props;
    const userPoolId = ssm.StringParameter.valueForStringParameter(this, SSM.userPoolId(deployEnv));
    const shellClientId = ssm.StringParameter.valueForStringParameter(this, SSM.shellClientId(deployEnv));

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
    // The API id comes from the API stack through SSM (deploy BootBootsApiGatewayStack first).
    const apiId = ssm.StringParameter.valueForStringParameter(this, `${SSM.bootbootsPrefix(deployEnv)}/api-id`);
    userRole.addToPolicy(
      new iam.PolicyStatement({
        actions: ['execute-api:Invoke'],
        resources: [`arn:aws:execute-api:${this.region}:${this.account}:${apiId}/*/GET/events`],
      }),
    );

    new cognito.CfnIdentityPoolRoleAttachment(this, 'IdentityPoolRoles', {
      identityPoolId: identityPool.ref,
      roles: { authenticated: userRole.roleArn },
    });

    // ── Old CI ─────────────────────────────────────────────────────────────
    // nakomis-bootboots' CI role. BootBootsGithubCiStack's role replaces it; remove this once that repo is archived.
    const oidc = iam.OpenIdConnectProvider.fromOpenIdConnectProviderArn(
      this,
      'GithubOidc',
      `arn:aws:iam::${this.account}:oidc-provider/token.actions.githubusercontent.com`,
    );
    const ciRole = new iam.Role(this, 'CiRole', {
      roleName: `nakomis-nakomis-bootboots-github-ci-${deployEnv}`,
      description: `Assumed by nakomis-bootboots GitHub Actions CI (${deployEnv})`,
      assumedBy: new iam.WebIdentityPrincipal(oidc.openIdConnectProviderArn, {
        StringEquals: { 'token.actions.githubusercontent.com:aud': 'sts.amazonaws.com' },
        StringLike: { 'token.actions.githubusercontent.com:sub': githubSubjects(LEGACY_PANEL_REPO) },
      }),
      managedPolicies: [
        iam.ManagedPolicy.fromManagedPolicyArn(
          this,
          'RemoteDeployer',
          ssm.StringParameter.valueForStringParameter(this, SSM.shellRemoteDeployerPolicyArn(deployEnv)),
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
      parameterName: `${SSM.bootbootsPrefix(deployEnv)}/identity-pool-id`,
      stringValue: identityPool.ref,
    });
    new ssm.StringParameter(this, 'CatadataTableParam', {
      parameterName: `${SSM.bootbootsPrefix(deployEnv)}/catadata-table`,
      stringValue: BOOTBOOTS.catadataTable,
      description: 'Labelling table read by the web panel',
    });

    new cdk.CfnOutput(this, 'IdentityPoolId', { value: identityPool.ref });
    new cdk.CfnOutput(this, 'UserRoleArn', { value: userRole.roleArn });
    new cdk.CfnOutput(this, 'CiRoleArn', { value: ciRole.roleArn });
  }
}
