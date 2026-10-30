import * as cdk from 'aws-cdk-lib';
import { Match, Template } from 'aws-cdk-lib/assertions';
import { AiTrainingStack } from '../lib/ai-training-stack';
import { ApiGatewayStack } from '../lib/api-gateway';
import { type DeployEnv, envConfig, isDeployEnv } from '../lib/env-config';
import { IotDeviceStack } from '../lib/iot-device';

// Skip Lambda and asset bundling: these tests check the templates, not the bundles.
const newApp = () => new cdk.App({ context: { 'aws:cdk:bundling-stacks': [] } });
const env = { account: '123456789012', region: 'eu-west-2' };

test('only sandbox and prod are environments', () => {
  expect(isDeployEnv('sandbox')).toBe(true);
  expect(isDeployEnv('prod')).toBe(true);
  expect(isDeployEnv('staging')).toBe(false);
  expect(isDeployEnv(undefined)).toBe(false);
});

describe.each(['sandbox', 'prod'] as const)('%s device stacks', (deployEnv: DeployEnv) => {
  const config = envConfig(deployEnv);
  const app = newApp();
  const api = new ApiGatewayStack(app, 'Api', { env, deployEnv, config });
  const iot = new IotDeviceStack(app, 'Iot', { env, deployEnv, api: api.api });
  const apiT = Template.fromStack(api);
  const iotT = Template.fromStack(iot);

  test('API custom domain and CORS come from the environment config', () => {
    apiT.hasResourceProperties('AWS::ApiGateway::DomainName', { DomainName: config.apiDomain });
    apiT.hasResourceProperties('AWS::Route53::RecordSet', { Name: `${config.apiDomain}.` });
    apiT.hasResourceProperties('AWS::S3::Bucket', {
      BucketName: 'bootboots-images-123456789012-eu-west-2',
      CorsConfiguration: { CorsRules: [{ AllowedOrigins: config.imagesBucketCorsOrigins }] },
    });
    apiT.hasResourceProperties('AWS::Lambda::Function', {
      FunctionName: 'BootBootsEventsQuery',
      Environment: { Variables: { ALLOWED_ORIGINS: config.apiCorsOrigins.join(',') } },
    });
  });

  test('API and IoT stacks publish what the panel and publish-web.sh read', () => {
    for (const name of ['api-id', 'images-bucket', 'events-api-url']) {
      apiT.hasResourceProperties('AWS::SSM::Parameter', { Name: `/nakomis-bootboots/${deployEnv}/${name}` });
    }
    apiT.hasResourceProperties('AWS::SSM::Parameter', {
      Name: `/nakomis-bootboots/${deployEnv}/events-api-url`,
      Value: `https://${config.apiDomain}/events`,
    });
    iotT.hasResourceProperties('AWS::SSM::Parameter', {
      Name: `/nakomis-bootboots/${deployEnv}/catcam-thing`,
      Value: 'BootBootsThing',
    });
  });

  test('data survives the stack: images bucket and events table are retained, never auto-emptied', () => {
    apiT.hasResource('AWS::S3::Bucket', { DeletionPolicy: 'Retain', UpdateReplacePolicy: 'Retain' });
    apiT.hasResource('AWS::DynamoDB::Table', { DeletionPolicy: 'Retain', UpdateReplacePolicy: 'Retain' });
    apiT.resourceCountIs('Custom::S3AutoDeleteObjects', 0);
  });

  test('catadata is created by CDK only where it is managed (prod), and retained', () => {
    if (config.manageCatadataTable) {
      apiT.hasResource('AWS::DynamoDB::Table', {
        Properties: {
          TableName: 'catadata',
          DeletionProtectionEnabled: true,
          PointInTimeRecoverySpecification: { PointInTimeRecoveryEnabled: true },
          KeySchema: [
            { AttributeName: 'imageName', KeyType: 'HASH' },
            { AttributeName: 'uuid', KeyType: 'RANGE' },
          ],
        },
        DeletionPolicy: 'Retain',
      });
    } else {
      expect(Object.values(apiT.findResources('AWS::DynamoDB::Table')).map((t) => t.Properties.TableName)).toEqual([
        'catcam-events',
      ]);
    }
  });

  test('no sandbox domain leaks into prod', () => {
    const json = JSON.stringify(apiT.toJSON());
    if (deployEnv === 'prod') expect(json).not.toContain('sandbox.nakomis.com');
    else expect(json).toContain('sandbox.nakomis.com');
  });
});

describe('AI stack', () => {
  const model = { artefact: 'local-models/20260331T203825/model.tar.gz', label: '20260331T203825' };
  const sandbox = Template.fromStack(new AiTrainingStack(newApp(), 'Ai', { env, deployEnv: 'sandbox', model }));

  test('the endpoint serves the model.json model, named from its label', () => {
    sandbox.hasResourceProperties('AWS::SageMaker::Model', {
      ModelName: 'bootboots-20260331T203825',
      PrimaryContainer: {
        Image: '763104351884.dkr.ecr.eu-west-2.amazonaws.com/tensorflow-inference:2.16-cpu',
        ModelDataUrl: {
          'Fn::Join': ['', ['s3://', { Ref: Match.stringLikeRegexp('^ModelArtifactsBucket') }, '/local-models/20260331T203825/model.tar.gz']],
        },
      },
    });
    const model = Object.values(sandbox.findResources('AWS::SageMaker::Model'))[0];
    expect(model.Properties.PrimaryContainer.Environment).toBeUndefined();
    sandbox.hasResourceProperties('AWS::SageMaker::EndpointConfig', {
      EndpointConfigName: 'bootboots-config-20260331T203825',
      ProductionVariants: [
        {
          ModelName: { 'Fn::GetAtt': [Match.stringLikeRegexp('^BootBootsModel'), 'ModelName'] },
          VariantName: 'AllTraffic',
          ServerlessConfig: { MaxConcurrency: 5, MemorySizeInMB: 2048 },
        },
      ],
    });
    const config = Object.values(sandbox.findResources('AWS::SageMaker::EndpointConfig'))[0];
    expect(config.Properties.ExecutionRoleArn).toBeUndefined();
  });

  test('the endpoint keeps its name and outlives the stack', () => {
    sandbox.hasResource('AWS::SageMaker::Endpoint', {
      Properties: { EndpointName: 'bootboots' },
      DeletionPolicy: 'Retain',
    });
  });

  test('sandbox refuses to synth without a model (it would remove the live endpoint)', () => {
    expect(() => new AiTrainingStack(newApp(), 'Ai', { env, deployEnv: 'sandbox', model: null })).toThrow(/must have a model/);
  });

  test('training lives in sandbox only', () => {
    sandbox.resourceCountIs('AWS::Lambda::Function', 2);
    const prod = Template.fromStack(new AiTrainingStack(newApp(), 'Ai', { env, deployEnv: 'prod', model: null }));
    prod.resourceCountIs('AWS::Lambda::Function', 0);
    prod.resourceCountIs('AWS::SageMaker::Endpoint', 0);
    prod.hasResourceProperties('AWS::S3::Bucket', { BucketName: 'bootboots-models-123456789012' });
    prod.hasResourceProperties('AWS::IAM::Role', { RoleName: 'BootBootsSageMakerRole' });
  });
});
