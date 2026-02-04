import * as cdk from 'aws-cdk-lib';
import * as apigateway from 'aws-cdk-lib/aws-apigateway';
import { NodejsFunction } from 'aws-cdk-lib/aws-lambda-nodejs';
import { Construct } from 'constructs';
import * as logs from 'aws-cdk-lib/aws-logs';
import * as acm from 'aws-cdk-lib/aws-certificatemanager';
import * as route53 from 'aws-cdk-lib/aws-route53';
import * as targets from 'aws-cdk-lib/aws-route53-targets';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as s3 from 'aws-cdk-lib/aws-s3';
import * as s3n from 'aws-cdk-lib/aws-s3-notifications';

export class ApiGatewayStack extends cdk.Stack {
  public api: apigateway.RestApi; 

  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);


    // Create S3 bucket for storing JPEG images
    const imagesBucket = new s3.Bucket(this, 'BootBootsImagesBucket', {
        bucketName: `bootboots-images-${this.account}-${this.region}`,
        versioned: false,
        publicReadAccess: false,
        blockPublicAccess: s3.BlockPublicAccess.BLOCK_ALL,
        removalPolicy: cdk.RemovalPolicy.DESTROY,
        cors: [{
            allowedOrigins: ['https://sandbox.nakomis.com', 'http://localhost:3000'],
            allowedMethods: [s3.HttpMethods.GET, s3.HttpMethods.PUT, s3.HttpMethods.POST],
            allowedHeaders: ['*'],
        }],
        autoDeleteObjects: true,
    });

    const inferLambdaLogGroup = new logs.LogGroup(this, 'BootBootsInferLambdaLogGroup', {
        logGroupName: '/aws/lambda/BootBootsInferLambda',
        retention: logs.RetentionDays.SIX_MONTHS,
        removalPolicy: cdk.RemovalPolicy.DESTROY,
    });

    // Create log group for API Gateway access logs
    const apiGatewayLogGroup = new logs.LogGroup(this, 'BootBootsApiGatewayLogGroup', {
        logGroupName: '/aws/apigateway/BootBootsInferApi',
        retention: logs.RetentionDays.ONE_MONTH,
        removalPolicy: cdk.RemovalPolicy.DESTROY,
    });

    const inferLambda = new NodejsFunction(this, 'BootBootsInferLambdaFunction', {
        functionName: 'BootBoots',
        runtime: cdk.aws_lambda.Runtime.NODEJS_22_X,
        entry: `${__dirname}/../lambda/infer-lambda/src/handler.ts`,
        logGroup: inferLambdaLogGroup,
        timeout: cdk.Duration.seconds(30),
        memorySize: 512,
        environment: {
            IMAGES_BUCKET_NAME: imagesBucket.bucketName,
        },
        bundling: {
            minify: true,
            sourceMap: false,
            target: 'node22',
        },
    });

    // Grant SageMaker invoke permissions to the Lambda function
    inferLambda.addToRolePolicy(new iam.PolicyStatement({
        effect: iam.Effect.ALLOW,
        actions: ['sagemaker:InvokeEndpoint'],
        resources: [`arn:aws:sagemaker:${this.region}:${this.account}:endpoint/bootboots`],
    }));

    // Grant S3 write permissions to the Lambda function
    imagesBucket.grantWrite(inferLambda);

    // Grant DynamoDB write permissions for catadata table (training mode)
    inferLambda.addToRolePolicy(new iam.PolicyStatement({
        effect: iam.Effect.ALLOW,
        actions: ['dynamodb:PutItem'],
        resources: [`arn:aws:dynamodb:${this.region}:${this.account}:table/catadata`],
    }));

    // Create log group for video notification Lambda
    const videoNotificationLogGroup = new logs.LogGroup(this, 'BootBootsVideoNotificationLogGroup', {
        logGroupName: '/aws/lambda/BootBootsVideoNotification',
        retention: logs.RetentionDays.ONE_MONTH,
        removalPolicy: cdk.RemovalPolicy.DESTROY,
    });

    // Create the video notification Lambda (triggered by S3)
    const videoNotificationLambda = new NodejsFunction(this, 'BootBootsVideoNotificationFunction', {
        functionName: 'BootBootsVideoNotification',
        runtime: cdk.aws_lambda.Runtime.NODEJS_22_X,
        entry: `${__dirname}/../lambda/video-notification/src/handler.ts`,
        logGroup: videoNotificationLogGroup,
        timeout: cdk.Duration.seconds(30),
        memorySize: 256,
        environment: {
            NOTIFICATION_EMAIL: 'bootboots@nakomis.com',
            SENDER_EMAIL: 'bootboots@nakomis.com',
        },
        bundling: {
            minify: true,
            sourceMap: false,
            target: 'node22',
        },
    });

    // Grant the video notification Lambda permission to read from S3 (for generating signed URLs)
    imagesBucket.grantRead(videoNotificationLambda);

    // Grant SES permissions to the video notification Lambda
    videoNotificationLambda.addToRolePolicy(new iam.PolicyStatement({
        effect: iam.Effect.ALLOW,
        actions: ['ses:SendEmail', 'ses:SendRawEmail'],
        resources: ['*'], // SES doesn't support resource-level permissions for SendEmail
    }));

    // Add S3 event notification for catcam-videos/ prefix
    imagesBucket.addEventNotification(
        s3.EventType.OBJECT_CREATED,
        new s3n.LambdaDestination(videoNotificationLambda),
        { prefix: 'catcam-videos/' }
    );

    // Create IAM role for API Gateway to access S3
    const apiGatewayS3Role = new iam.Role(this, 'ApiGatewayS3Role', {
        assumedBy: new iam.ServicePrincipal('apigateway.amazonaws.com'),
        description: 'Role for API Gateway to write videos to S3',
    });

    // Grant the role permission to put objects in the catcam-videos prefix
    imagesBucket.grantPut(apiGatewayS3Role, 'catcam-videos/*');

    // Create the API Gateway
    this.api = new apigateway.RestApi(this, 'BootBootsInferApi', {
      restApiName: 'BootBootsInference API',
      description: 'BootBoots Inference API Gateway with POST /infer endpoint',
      binaryMediaTypes: ['image/jpeg', 'image/png', 'image/gif', 'image/webp', 'application/octet-stream', 'video/x-msvideo', 'video/avi'],
      defaultCorsPreflightOptions: {
        allowOrigins: apigateway.Cors.ALL_ORIGINS,
        allowMethods: ['POST', 'PUT', 'OPTIONS'],
        allowHeaders: ['Content-Type', 'Authorization', 'X-API-Key'],
      },
      // Disable the default endpoint to force usage of custom domain
      disableExecuteApiEndpoint: true,
      cloudWatchRole: true,
      deployOptions: {
        accessLogDestination: new apigateway.LogGroupLogDestination(apiGatewayLogGroup),
        accessLogFormat: apigateway.AccessLogFormat.jsonWithStandardFields({
          caller: true,
          httpMethod: true,
          ip: true,
          protocol: true,
          requestTime: true,
          resourcePath: true,
          responseLength: true,
          status: true,
          user: true,
        }),
        loggingLevel: apigateway.MethodLoggingLevel.INFO,
        dataTraceEnabled: true,
        metricsEnabled: true,
      },
    });

    // Create the /infer resource
    const inferResource = this.api.root.addResource('infer');

    // Create API key
    const apiKey = new apigateway.ApiKey(this, 'BootBootsApiKey', {
      apiKeyName: 'BootBootsInferenceKey',
      description: 'API key for BootBoots inference endpoint',
    });

    // Add POST method to /infer with IAM auth (no API key required for IoT devices)
    inferResource.addMethod('POST', new apigateway.LambdaIntegration(inferLambda), {
      authorizationType: apigateway.AuthorizationType.IAM,
      apiKeyRequired: false,
      methodResponses: [
        {
          statusCode: '200',
        },
        {
          statusCode: '400',
        },
        {
          statusCode: '403',
        },
        {
          statusCode: '500',
        },
      ],
    });

    // Create log group for list-devices Lambda
    const listDevicesLogGroup = new logs.LogGroup(this, 'BootBootsListDevicesLogGroup', {
        logGroupName: '/aws/lambda/BootBootsListDevices',
        retention: logs.RetentionDays.ONE_MONTH,
        removalPolicy: cdk.RemovalPolicy.DESTROY,
    });

    // Create the list-devices Lambda
    const listDevicesLambda = new NodejsFunction(this, 'BootBootsListDevicesFunction', {
        functionName: 'BootBootsListDevices',
        runtime: cdk.aws_lambda.Runtime.NODEJS_22_X,
        entry: `${__dirname}/../lambda/list-devices/src/handler.ts`,
        logGroup: listDevicesLogGroup,
        timeout: cdk.Duration.seconds(30),
        memorySize: 256,
        bundling: {
            minify: true,
            sourceMap: false,
            target: 'node22',
        },
    });

    // Grant IoT permissions to list and describe things
    listDevicesLambda.addToRolePolicy(new iam.PolicyStatement({
        effect: iam.Effect.ALLOW,
        actions: ['iot:ListThings', 'iot:DescribeThing'],
        resources: ['*'],
    }));

    // Create the /devices resource with Cognito auth
    const devicesResource = this.api.root.addResource('devices');
    devicesResource.addMethod('GET', new apigateway.LambdaIntegration(listDevicesLambda), {
        authorizationType: apigateway.AuthorizationType.IAM,
        apiKeyRequired: false,
        methodResponses: [
            { statusCode: '200' },
            { statusCode: '400' },
            { statusCode: '500' },
        ],
    });

    // Output the /devices endpoint URL
    new cdk.CfnOutput(this, 'DevicesEndpointUrl', {
      value: `https://api.bootboots.sandbox.nakomis.com/devices`,
      description: 'URL for the /devices GET endpoint',
    });

    // Create the authCheck resource for IoT device authentication verification
    const authCheckResource = this.api.root.addResource('authCheck');
    authCheckResource.addMethod('POST', new apigateway.MockIntegration({
      integrationResponses: [{
        statusCode: '200',
        responseTemplates: {
          'application/json': JSON.stringify({ status: 'authenticated' }),
        },
      }],
      requestTemplates: {
        'application/json': '{"statusCode": 200}',
      },
    }), {
      authorizationType: apigateway.AuthorizationType.IAM,
      methodResponses: [{
        statusCode: '200',
      }],
    });

    // Output the authCheck ARN for IAM policy reference
    new cdk.CfnOutput(this, 'AuthCheckApiArn', {
      value: `arn:aws:execute-api:${this.region}:${this.account}:${this.api.restApiId}/*/POST/authCheck`,
      description: 'ARN for the authCheck endpoint (for IAM policies)',
    });

    // Create the /upload-video/{filename} resource with S3 integration
    const uploadVideoResource = this.api.root.addResource('upload-video');
    const filenameResource = uploadVideoResource.addResource('{filename}');

    // S3 integration for PUT /upload-video/{filename}
    const s3Integration = new apigateway.AwsIntegration({
      service: 's3',
      integrationHttpMethod: 'PUT',
      path: `${imagesBucket.bucketName}/catcam-videos/{filename}`,
      options: {
        credentialsRole: apiGatewayS3Role,
        integrationResponses: [
          {
            statusCode: '200',
            responseParameters: {
              'method.response.header.Content-Type': 'integration.response.header.Content-Type',
            },
            responseTemplates: {
              'application/json': JSON.stringify({
                success: true,
                message: 'Video uploaded successfully'
              }),
            },
          },
          {
            statusCode: '400',
            selectionPattern: '4\\d{2}',
            responseTemplates: {
              'application/json': JSON.stringify({
                error: 'Bad request'
              }),
            },
          },
          {
            statusCode: '500',
            selectionPattern: '5\\d{2}',
            responseTemplates: {
              'application/json': JSON.stringify({
                error: 'Internal server error'
              }),
            },
          },
        ],
        requestParameters: {
          'integration.request.path.filename': 'method.request.path.filename',
          'integration.request.header.Content-Type': 'method.request.header.Content-Type',
        },
      },
    });

    filenameResource.addMethod('PUT', s3Integration, {
      authorizationType: apigateway.AuthorizationType.IAM,
      apiKeyRequired: false,
      requestParameters: {
        'method.request.path.filename': true,
        'method.request.header.Content-Type': true,
      },
      methodResponses: [
        {
          statusCode: '200',
          responseParameters: {
            'method.response.header.Content-Type': true,
          },
        },
        {
          statusCode: '400',
        },
        {
          statusCode: '500',
        },
      ],
    });

    // Output the upload-video endpoint URL
    new cdk.CfnOutput(this, 'UploadVideoEndpointUrl', {
      value: `https://api.bootboots.sandbox.nakomis.com/upload-video/{filename}`,
      description: 'URL for the /upload-video/{filename} PUT endpoint',
    });

    // Create usage plan with 500 requests per day quota
    const usagePlan = new apigateway.UsagePlan(this, 'BootBootsUsagePlan', {
      name: 'BootBootsInferenceUsagePlan',
      description: 'Usage plan for BootBoots inference API - 500 requests per day',
      quota: {
        limit: 500,
        period: apigateway.Period.DAY,
      },
      throttle: {
        rateLimit: 10, // 10 requests per second
        burstLimit: 20, // Allow bursts up to 20 requests
      },
    });

    // Associate the API key with the usage plan
    usagePlan.addApiKey(apiKey);

    // Associate the usage plan with the API stage
    usagePlan.addApiStage({
      api: this.api,
      stage: this.api.deploymentStage,
    });

    // Look up the existing hosted zone for sandbox.nakomis.com
    const hostedZone = route53.HostedZone.fromLookup(this, 'SandboxHostedZone', {
      domainName: 'sandbox.nakomis.com',
    });

    // Create SSL certificate for the custom domain with DNS validation
    const certificate = new acm.Certificate(this, 'BootBootsApiCertificate', {
      domainName: 'api.bootboots.sandbox.nakomis.com',
      validation: acm.CertificateValidation.fromDns(hostedZone),
    });

    // Create custom domain for API Gateway
    const customDomain = new apigateway.DomainName(this, 'BootBootsApiDomain', {
      domainName: 'api.bootboots.sandbox.nakomis.com',
      certificate: certificate,
      endpointType: apigateway.EndpointType.REGIONAL,
    });

    // Map the custom domain to the API Gateway
    customDomain.addBasePathMapping(this.api, {
      basePath: '',
    });

    // Create Route53 A record to point to the custom domain
    new route53.ARecord(this, 'BootBootsApiAliasRecord', {
      zone: hostedZone,
      recordName: 'api.bootboots',
      target: route53.RecordTarget.fromAlias(new targets.ApiGatewayDomain(customDomain)),
    });

    // Output the API Gateway URL
    new cdk.CfnOutput(this, 'BootBootsInferApiGatewayUrl', {
      value: this.api.url,
      description: 'URL of the API Gateway',
    });

    // Output the /infer endpoint URL
    new cdk.CfnOutput(this, 'InferEndpointUrl', {
      value: `${this.api.url}infer`,
      description: 'URL of the /infer POST endpoint',
    });

    // Output the custom domain URL
    new cdk.CfnOutput(this, 'CustomDomainUrl', {
      value: `https://api.bootboots.sandbox.nakomis.com`,
      description: 'Custom domain URL for the API',
    });

    // Output the custom domain /infer endpoint URL
    new cdk.CfnOutput(this, 'CustomDomainInferEndpointUrl', {
      value: `https://api.bootboots.sandbox.nakomis.com/infer`,
      description: 'Custom domain URL for the /infer POST endpoint',
    });

    // Output the API key value
    new cdk.CfnOutput(this, 'BootBootsApiKeyValue', {
      value: apiKey.keyId,
      description: 'API Key ID for BootBoots inference endpoint',
    });

    // Output usage instructions
    new cdk.CfnOutput(this, 'ApiUsageInstructions', {
      value: 'Use: curl -X POST -H "X-API-Key: <API_KEY>" -H "Content-Type: image/jpeg" --data-binary "@image.jpg" https://api.bootboots.sandbox.nakomis.com/infer',
      description: 'How to use the API with authentication',
    });
  }

  
}