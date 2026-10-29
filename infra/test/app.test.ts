import * as cdk from 'aws-cdk-lib';
import { Template } from 'aws-cdk-lib/assertions';
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

  test('no sandbox domain leaks into prod', () => {
    const json = JSON.stringify(apiT.toJSON());
    if (deployEnv === 'prod') expect(json).not.toContain('sandbox.nakomis.com');
    else expect(json).toContain('sandbox.nakomis.com');
  });
});

test('AI training stack has an endpoint only when given a model', () => {
  const without = Template.fromStack(new AiTrainingStack(newApp(), 'NoModel', { env }));
  without.resourceCountIs('AWS::SageMaker::Endpoint', 0);
  const withModel = Template.fromStack(
    new AiTrainingStack(newApp(), 'Model', { env, modelDataUrl: 's3://bucket/model.tar.gz' }),
  );
  withModel.hasResourceProperties('AWS::SageMaker::Endpoint', { EndpointName: 'bootboots' });
});
