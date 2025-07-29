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

export class ApiGatewayStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    // Create S3 bucket for storing JPEG images
    const imagesBucket = new s3.Bucket(this, 'BootBootsImagesBucket', {
        bucketName: `bootboots-images-${this.account}-${this.region}`,
        versioned: false,
        publicReadAccess: false,
        blockPublicAccess: s3.BlockPublicAccess.BLOCK_ALL,
        removalPolicy: cdk.RemovalPolicy.DESTROY,
        autoDeleteObjects: true,
    });

    const inferLambdaLogGroup = new logs.LogGroup(this, 'BootBootsInferLambdaLogGroup', {
        logGroupName: '/aws/lambda/BootBootsInferLambda',
        retention: logs.RetentionDays.SIX_MONTHS,
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


    // Create the API Gateway
    const api = new apigateway.RestApi(this, 'BootBootsInferApi', {
      restApiName: 'BootBootsInference API',
      description: 'BootBoots Inference API Gateway with POST /infer endpoint',
      binaryMediaTypes: ['image/jpeg', 'image/png', 'image/gif', 'image/webp', 'application/octet-stream'],
      defaultCorsPreflightOptions: {
        allowOrigins: apigateway.Cors.ALL_ORIGINS,
        allowMethods: ['POST', 'OPTIONS'],
        allowHeaders: ['Content-Type', 'Authorization', 'X-API-Key'],
      },
      // Disable the default endpoint to force usage of custom domain
      disableExecuteApiEndpoint: true,
    });

    // Create the /infer resource
    const inferResource = api.root.addResource('infer');

    // Create API key
    const apiKey = new apigateway.ApiKey(this, 'BootBootsApiKey', {
      apiKeyName: 'BootBootsInferenceKey',
      description: 'API key for BootBoots inference endpoint',
    });

    // Add POST method to /infer with API key requirement
    inferResource.addMethod('POST', new apigateway.LambdaIntegration(inferLambda), {
      apiKeyRequired: true,
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
      api: api,
      stage: api.deploymentStage,
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
    customDomain.addBasePathMapping(api, {
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
      value: api.url,
      description: 'URL of the API Gateway',
    });

    // Output the /infer endpoint URL
    new cdk.CfnOutput(this, 'InferEndpointUrl', {
      value: `${api.url}infer`,
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