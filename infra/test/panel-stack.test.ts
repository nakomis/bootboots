import * as cdk from 'aws-cdk-lib';
import { Match, Template } from 'aws-cdk-lib/assertions';
import { BootbootsStack } from '../lib/bootboots-stack';
import { githubSubjects, REPO } from '../lib/env-config';

let t: Template;
beforeAll(() => {
  const app = new cdk.App();
  const stack = new BootbootsStack(app, 'Bootboots', {
    env: { account: '123456789012', region: 'eu-west-2' },
    deployEnv: 'sandbox',
    githubOidcProviderArn: 'arn:aws:iam::123456789012:oidc-provider/token.actions.githubusercontent.com',
  });
  t = Template.fromStack(stack);
});

const statementsOf = (roleName: string) => {
  const roles = t.findResources('AWS::IAM::Role', { Properties: { RoleName: roleName } });
  const roleId = Object.keys(roles)[0];
  const policies = Object.values(t.findResources('AWS::IAM::Policy')).filter((p) =>
    p.Properties.Roles.some((r: { Ref: string }) => r.Ref === roleId),
  );
  return policies.flatMap((p) => p.Properties.PolicyDocument.Statement);
};

test('identity pool trusts only the shared user pool via the shell client, with no guests', () => {
  t.hasResourceProperties('AWS::Cognito::IdentityPool', {
    IdentityPoolName: 'nakomis_bootboots_sandbox',
    AllowUnauthenticatedIdentities: false,
    CognitoIdentityProviders: [
      Match.objectLike({
        ProviderName: Match.objectLike({ 'Fn::Join': Match.anyValue() }),
        ClientId: Match.objectLike({ Ref: Match.stringLikeRegexp('nakomisshellsandboxcognitoclientid') }),
      }),
    ],
  });
  t.hasResourceProperties('AWS::Cognito::IdentityPoolRoleAttachment', {
    Roles: { authenticated: Match.anyValue() },
  });
});

test('authenticated role is assumable only by authenticated identities of this pool', () => {
  t.hasResourceProperties('AWS::IAM::Role', {
    RoleName: 'nakomis-bootboots-user-sandbox',
    AssumeRolePolicyDocument: {
      Statement: [
        Match.objectLike({
          Action: 'sts:AssumeRoleWithWebIdentity',
          Principal: { Federated: 'cognito-identity.amazonaws.com' },
          Condition: {
            StringEquals: { 'cognito-identity.amazonaws.com:aud': { Ref: 'IdentityPool' } },
            'ForAnyValue:StringLike': { 'cognito-identity.amazonaws.com:amr': 'authenticated' },
          },
        }),
      ],
    },
  });
});

test('authenticated role grants exactly what the pages use', () => {
  expect(statementsOf('nakomis-bootboots-user-sandbox')).toEqual([
    {
      Effect: 'Allow',
      Action: ['dynamodb:Scan', 'dynamodb:UpdateItem'],
      Resource: 'arn:aws:dynamodb:eu-west-2:123456789012:table/catadata',
    },
    {
      Effect: 'Allow',
      Action: 's3:GetObject',
      Resource: 'arn:aws:s3:::bootboots-images-123456789012-eu-west-2/catcam-training/*',
    },
    {
      Effect: 'Allow',
      Action: 'execute-api:Invoke',
      Resource: 'arn:aws:execute-api:eu-west-2:123456789012:yn8jvnh99j/*/GET/events',
    },
  ]);
});

test('CI role trusts both GitHub subject forms and can deploy and publish', () => {
  expect(githubSubjects(REPO)).toEqual([
    'repo:nakomis@1488244/nakomis-bootboots@1414620899:*',
    'repo:nakomis/nakomis-bootboots:*',
  ]);
  t.hasResourceProperties('AWS::IAM::Role', {
    RoleName: 'nakomis-nakomis-bootboots-github-ci-sandbox',
    AssumeRolePolicyDocument: {
      Statement: [
        Match.objectLike({
          Condition: {
            StringEquals: { 'token.actions.githubusercontent.com:aud': 'sts.amazonaws.com' },
            StringLike: { 'token.actions.githubusercontent.com:sub': githubSubjects(REPO) },
          },
        }),
      ],
    },
    ManagedPolicyArns: [Match.objectLike({ Ref: Match.stringLikeRegexp('remotedeployerpolicyarn') })],
  });
  expect(statementsOf('nakomis-nakomis-bootboots-github-ci-sandbox')).toEqual([
    { Effect: 'Allow', Action: 'sts:AssumeRole', Resource: 'arn:aws:iam::123456789012:role/cdk-hnb659fds-*' },
    {
      Effect: 'Allow',
      Action: 'ssm:GetParameter',
      Resource: 'arn:aws:ssm:eu-west-2:123456789012:parameter/nakomis-bootboots/*',
    },
  ]);
});

test('exports the identity pool id to SSM', () => {
  t.hasResourceProperties('AWS::SSM::Parameter', {
    Name: '/nakomis-bootboots/sandbox/identity-pool-id',
    Value: { Ref: 'IdentityPool' },
  });
});
