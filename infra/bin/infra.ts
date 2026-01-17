#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib';
import { ApiGatewayStack } from '../lib/api-gateway';
import { FirmwareCleanupStack } from '../lib/firmware-cleanup-stack';
import { IotDeviceStack } from '../lib/iot-device';

const londonEnv = { env: { account: process.env.CDK_DEFAULT_ACCOUNT, region: process.env.CDK_DEFAULT_REGION } };

const app = new cdk.App();

// Create the API Gateway stack
new ApiGatewayStack(app, 'BootBootsApiGatewayStack', londonEnv);

// Create the Firmware Cleanup stack
new FirmwareCleanupStack(app, 'BootBootsFirmwareCleanupStack', londonEnv);

// Create the IoT stack
new IotDeviceStack(app, "BootBootsIoTStack", londonEnv)