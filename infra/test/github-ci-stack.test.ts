import * as cdk from 'aws-cdk-lib';
import { Match, Template } from 'aws-cdk-lib/assertions';
import { type DeployEnv, envConfig, githubSubjects, REPO } from '../lib/env-config';
import { GithubCiStack } from '../lib/github-ci-stack';

const synth = (deployEnv: DeployEnv) => {
  const app = new cdk.App();
  const stack = new GithubCiStack(app, 'Ci', {
    env: { account: '123456789012', region: 'eu-west-2' },
    deployEnv,
    config: envConfig(deployEnv),
  });
  return Template.fromStack(stack);
};

const statements = (t: Template) =>
  Object.values(t.findResources('AWS::IAM::Policy')).flatMap((p) => p.Properties.PolicyDocument.Statement);

test('subjects pin the bootboots repo by id and by name', () => {
  expect(githubSubjects(REPO)).toEqual([
    'repo:nakomis@1488244/bootboots@1015458213:*',
    'repo:nakomis/bootboots:*',
  ]);
});

test.each([
  ['sandbox' as const, githubSubjects(REPO)],
  ['prod' as const, [...githubSubjects(REPO, 'ref:refs/heads/main'), ...githubSubjects(REPO, 'environment:prod')]],
])('%s role trusts the right refs only', (deployEnv, subjects) => {
  synth(deployEnv).hasResourceProperties('AWS::IAM::Role', {
    RoleName: `nakomis-bootboots-github-ci-${deployEnv}`,
    MaxSessionDuration: 3600,
    AssumeRolePolicyDocument: {
      Statement: [
        Match.objectLike({
          Action: 'sts:AssumeRoleWithWebIdentity',
          Condition: {
            StringEquals: { 'token.actions.githubusercontent.com:aud': 'sts.amazonaws.com' },
            StringLike: { 'token.actions.githubusercontent.com:sub': subjects },
          },
        }),
      ],
    },
    ManagedPolicyArns: [Match.objectLike({ Ref: Match.stringLikeRegexp('remotedeployerpolicyarn') })],
  });
});

test.each(['sandbox', 'prod'] as const)(
  '%s role can deploy, read its params, publish firmware and call the tracker',
  (deployEnv) => {
    const bucket = `nakomis-firmware-${deployEnv}`;
    expect(statements(synth(deployEnv))).toEqual([
      { Effect: 'Allow', Action: 'sts:AssumeRole', Resource: 'arn:aws:iam::123456789012:role/cdk-hnb659fds-*' },
      {
        Effect: 'Allow',
        Action: ['ssm:GetParameter', 'ssm:GetParameters'],
        Resource: [
          'arn:aws:ssm:eu-west-2:123456789012:parameter/nakomis-bootboots/*',
          'arn:aws:ssm:eu-west-2:123456789012:parameter/bootboots/*',
        ],
      },
      {
        Effect: 'Allow',
        Action: 'kms:Decrypt',
        Resource: '*',
        Condition: { StringEquals: { 'kms:ViaService': 'ssm.eu-west-2.amazonaws.com' } },
      },
      { Effect: 'Allow', Action: ['s3:PutObject', 's3:GetObject'], Resource: `arn:aws:s3:::${bucket}/BootBoots/*` },
      { Effect: 'Allow', Action: 's3:ListBucket', Resource: `arn:aws:s3:::${bucket}` },
      {
        Effect: 'Allow',
        Action: 'execute-api:Invoke',
        Resource: 'arn:aws:execute-api:eu-west-2:*:*/*/*/deployments/*',
      },
    ]);
  },
);
