#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib';
import { ApiGatewayStack } from '../lib/api-gateway';
import { FirmwareCleanupStack } from '../lib/firmware-cleanup-stack';
import { IotDeviceStack } from '../lib/iot-device';

const londonEnv = { env: { account: '975050268859', region: 'eu-west-2' } };

const app = new cdk.App();

// Create the API Gateway stack
const apiGateway = new ApiGatewayStack(app, 'BootBootsApiGatewayStack', londonEnv);

// Create the IoT stack
new IotDeviceStack(app, "BootBootsIoTStack", {...londonEnv, api: apiGateway.api})

// Create the Firmware Cleanup stack
new FirmwareCleanupStack(app, 'BootBootsFirmwareCleanupStack', londonEnv);
