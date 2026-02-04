#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib';
import { ApiGatewayStack } from '../lib/api-gateway';
import { IotDeviceStack } from '../lib/iot-device';
import { AiTrainingStack } from '../lib/ai-training-stack';
import { WebSocketApiStack } from '../lib/websocket-api-stack';

const londonEnv = { env: { account: '975050268859', region: 'eu-west-2' } };

const app = new cdk.App();

// Create the API Gateway stack
const apiGateway = new ApiGatewayStack(app, 'BootBootsApiGatewayStack', londonEnv);

// Create the IoT stack
new IotDeviceStack(app, "BootBootsIoTStack", {...londonEnv, api: apiGateway.api})

// Create the WebSocket API stack for MQTT remote control
new WebSocketApiStack(app, 'BootBootsWebSocketStack', londonEnv);

// Create the AI Training stack
// Pass modelDataUrl context to deploy with a pre-trained model:
//   cdk deploy BootBootsAiTrainingStack --context modelDataUrl=s3://bucket/path/model.tar.gz
const modelDataUrl = app.node.tryGetContext('modelDataUrl');
new AiTrainingStack(app, 'BootBootsAiTrainingStack', {
    ...londonEnv,
    modelDataUrl,
});
