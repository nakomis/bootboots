import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as lambda from 'aws-cdk-lib/aws-lambda';
import * as s3 from 'aws-cdk-lib/aws-s3';
import * as s3n from 'aws-cdk-lib/aws-s3-notifications';
import * as logs from 'aws-cdk-lib/aws-logs';
import * as iam from 'aws-cdk-lib/aws-iam';
import { NodejsFunction } from 'aws-cdk-lib/aws-lambda-nodejs';

export class FirmwareCleanupStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    // Reference the existing S3 bucket (don't create a new one)
    const firmwareBucket = s3.Bucket.fromBucketName(
      this,
      'FirmwareBucket',
      'bootboots-firmware-updates'
    );

    // Create CloudWatch Logs group for the Lambda
    const cleanupLambdaLogGroup = new logs.LogGroup(this, 'FirmwareCleanupLogGroup', {
      logGroupName: '/aws/lambda/FirmwareCleanupLambda',
      retention: logs.RetentionDays.SIX_MONTHS,
      removalPolicy: cdk.RemovalPolicy.DESTROY,
    });

    // Create the Lambda function
    const cleanupLambda = new NodejsFunction(this, 'FirmwareCleanupFunction', {
      functionName: 'FirmwareCleanupLambda',
      runtime: lambda.Runtime.NODEJS_22_X,
      entry: `${__dirname}/../lambda/firmware-cleanup/src/handler.ts`,
      handler: 'handler',
      logGroup: cleanupLambdaLogGroup,
      timeout: cdk.Duration.seconds(60),
      memorySize: 256,
      environment: {
        BUCKET_NAME: firmwareBucket.bucketName,
      },
      bundling: {
        minify: true,
        sourceMap: false,
        target: 'node22',
        externalModules: [
          '@aws-sdk/client-s3', // Use AWS SDK v3 from Lambda runtime
        ],
      },
    });

    // Grant S3 permissions to the Lambda
    cleanupLambda.addToRolePolicy(new iam.PolicyStatement({
      effect: iam.Effect.ALLOW,
      actions: [
        's3:ListBucket',
        's3:GetObject',
        's3:GetObjectMetadata',
        's3:DeleteObject',
        's3:PutObject',
      ],
      resources: [
        firmwareBucket.bucketArn,
        `${firmwareBucket.bucketArn}/*`,
      ],
    }));

    // Add S3 event notification to trigger Lambda on firmware uploads
    // Since CDK v2, addEventNotification works on imported buckets
    firmwareBucket.addEventNotification(
      s3.EventType.OBJECT_CREATED,
      new s3n.LambdaDestination(cleanupLambda),
      {
        suffix: 'firmware.bin',
      }
    );

    // Output Lambda function details
    new cdk.CfnOutput(this, 'CleanupLambdaName', {
      value: cleanupLambda.functionName,
      description: 'Name of the firmware cleanup Lambda function',
    });

    new cdk.CfnOutput(this, 'CleanupLambdaArn', {
      value: cleanupLambda.functionArn,
      description: 'ARN of the firmware cleanup Lambda function',
    });

    new cdk.CfnOutput(this, 'LogGroupName', {
      value: cleanupLambdaLogGroup.logGroupName,
      description: 'CloudWatch Logs group for Lambda execution logs',
    });

    new cdk.CfnOutput(this, 'S3BucketName', {
      value: firmwareBucket.bucketName,
      description: 'S3 bucket with automatic firmware cleanup',
    });

    new cdk.CfnOutput(this, 'NotificationStatus', {
      value: 'S3 event notification configured via addEventNotification',
      description: 'Triggers on s3:ObjectCreated for files ending with firmware.bin',
    });
  }
}
