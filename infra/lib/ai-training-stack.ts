import * as cdk from 'aws-cdk-lib';
import * as s3 from 'aws-cdk-lib/aws-s3';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as lambda from 'aws-cdk-lib/aws-lambda';
import * as logs from 'aws-cdk-lib/aws-logs';
import * as sagemaker from 'aws-cdk-lib/aws-sagemaker';
import { NodejsFunction } from 'aws-cdk-lib/aws-lambda-nodejs';
import { Construct } from 'constructs';

/**
 * Cat classes for the model - order matters as it maps to probability array indices
 */
export const CAT_CLASSES = ['Boots', 'Chi', 'Kappa', 'Mu', 'NoCat', 'Tau', 'Wolf'] as const;

export interface AiTrainingStackProps extends cdk.StackProps {
    /**
     * Optional S3 path to a pre-trained model to deploy
     * If not provided, a new training job must be run first
     */
    modelDataUrl?: string;
}

export class AiTrainingStack extends cdk.Stack {
    public readonly trainingBucket: s3.IBucket;
    public readonly modelBucket: s3.IBucket;
    public readonly sagemakerRole: iam.Role;

    constructor(scope: Construct, id: string, props?: AiTrainingStackProps) {
        super(scope, id, props);

        // Reference the existing images bucket
        const imagesBucket = s3.Bucket.fromBucketName(
            this,
            'ImagesBucket',
            `bootboots-images-${this.account}-${this.region}`
        );

        // Create bucket for organized training data (folder structure by class)
        this.trainingBucket = new s3.Bucket(this, 'TrainingDataBucket', {
            bucketName: `bootboots-training-data-${this.account}`,
            versioned: false,
            publicReadAccess: false,
            blockPublicAccess: s3.BlockPublicAccess.BLOCK_ALL,
            removalPolicy: cdk.RemovalPolicy.RETAIN,
            lifecycleRules: [
                {
                    // Clean up old training data after 90 days
                    expiration: cdk.Duration.days(90),
                    prefix: 'training/',
                },
            ],
        });

        // Create bucket for model artifacts
        this.modelBucket = new s3.Bucket(this, 'ModelArtifactsBucket', {
            bucketName: `bootboots-models-${this.account}`,
            versioned: true, // Keep model versions
            publicReadAccess: false,
            blockPublicAccess: s3.BlockPublicAccess.BLOCK_ALL,
            removalPolicy: cdk.RemovalPolicy.RETAIN,
        });

        // IAM role for SageMaker training jobs
        this.sagemakerRole = new iam.Role(this, 'SageMakerExecutionRole', {
            roleName: 'BootBootsSageMakerRole',
            assumedBy: new iam.ServicePrincipal('sagemaker.amazonaws.com'),
            managedPolicies: [
                iam.ManagedPolicy.fromAwsManagedPolicyName('AmazonSageMakerFullAccess'),
            ],
        });

        // Grant S3 access to SageMaker role
        this.trainingBucket.grantRead(this.sagemakerRole);
        this.modelBucket.grantReadWrite(this.sagemakerRole);
        imagesBucket.grantRead(this.sagemakerRole);

        // Log group for data prep Lambda
        const dataPrepLogGroup = new logs.LogGroup(this, 'DataPrepLogGroup', {
            logGroupName: '/aws/lambda/bootboots-data-prep',
            retention: logs.RetentionDays.ONE_MONTH,
            removalPolicy: cdk.RemovalPolicy.DESTROY,
        });

        // Lambda for preparing training data
        const dataPrepLambda = new NodejsFunction(this, 'DataPrepFunction', {
            functionName: 'bootboots-data-prep',
            runtime: lambda.Runtime.NODEJS_22_X,
            entry: `${__dirname}/../lambda/ai-data-prep/src/handler.ts`,
            handler: 'handler',
            logGroup: dataPrepLogGroup,
            timeout: cdk.Duration.minutes(10),
            memorySize: 1024,
            environment: {
                SOURCE_BUCKET: imagesBucket.bucketName,
                SOURCE_PREFIX: 'catcam-training/',
                TARGET_BUCKET: this.trainingBucket.bucketName,
                CATADATA_TABLE: 'catadata',
                CAT_CLASSES: CAT_CLASSES.join(','),
                VALIDATION_SPLIT: '0.2', // 20% for validation
                MAX_NOCAT_SAMPLES: '200', // Undersample NoCat class
            },
            bundling: {
                minify: true,
                sourceMap: false,
                target: 'node22',
            },
        });

        // Grant permissions to data prep Lambda
        imagesBucket.grantRead(dataPrepLambda);
        this.trainingBucket.grantReadWrite(dataPrepLambda);
        dataPrepLambda.addToRolePolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['dynamodb:Scan'],
            resources: [`arn:aws:dynamodb:${this.region}:${this.account}:table/catadata`],
        }));

        // Log group for training trigger Lambda
        const trainingTriggerLogGroup = new logs.LogGroup(this, 'TrainingTriggerLogGroup', {
            logGroupName: '/aws/lambda/bootboots-trigger-training',
            retention: logs.RetentionDays.ONE_MONTH,
            removalPolicy: cdk.RemovalPolicy.DESTROY,
        });

        // Lambda for triggering SageMaker training
        const trainingTriggerLambda = new NodejsFunction(this, 'TrainingTriggerFunction', {
            functionName: 'bootboots-trigger-training',
            runtime: lambda.Runtime.NODEJS_22_X,
            entry: `${__dirname}/../lambda/ai-training-trigger/src/handler.ts`,
            handler: 'handler',
            logGroup: trainingTriggerLogGroup,
            timeout: cdk.Duration.minutes(5),
            memorySize: 512,
            environment: {
                TRAINING_BUCKET: this.trainingBucket.bucketName,
                MODEL_BUCKET: this.modelBucket.bucketName,
                SAGEMAKER_ROLE_ARN: this.sagemakerRole.roleArn,
                DATA_PREP_FUNCTION: dataPrepLambda.functionName,
                NUM_CLASSES: CAT_CLASSES.length.toString(),
                // JumpStart model settings
                // Available models (trade-off between accuracy and speed):
                //   Model           | Size  | Accuracy | Speed   | MODEL_ID
                //   ================|=======|==========|=========|=============================================================
                //   MobileNet v2    | 14MB  | Good     | Fast    | tensorflow-ic-imagenet-mobilenet-v2-100-224-classification-4
                //   EfficientNet B0 | 29MB  | Better   | Medium  | tensorflow-ic-imagenet-efficientnet-b0-classification-4
                //   ResNet50        | 98MB  | Better   | Slower  | tensorflow-ic-imagenet-resnet-50-classification-4
                //   EfficientNet B4 | 75MB  | Best     | Slowest | tensorflow-ic-imagenet-efficientnet-b4-classification-4
                MODEL_ID: 'tensorflow-ic-imagenet-mobilenet-v2-100-224-classification-4',
                MODEL_VERSION: '*',
            },
            bundling: {
                minify: true,
                sourceMap: false,
                target: 'node22',
            },
        });

        // Grant permissions to training trigger Lambda
        trainingTriggerLambda.addToRolePolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: [
                'sagemaker:CreateTrainingJob',
                'sagemaker:DescribeTrainingJob',
                'sagemaker:ListTrainingJobs',
                'sagemaker:AddTags',
            ],
            resources: ['*'],
        }));

        trainingTriggerLambda.addToRolePolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['iam:PassRole'],
            resources: [this.sagemakerRole.roleArn],
        }));

        trainingTriggerLambda.addToRolePolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['lambda:InvokeFunction'],
            resources: [dataPrepLambda.functionArn],
        }));

        this.trainingBucket.grantRead(trainingTriggerLambda);
        this.modelBucket.grantRead(trainingTriggerLambda);

        // Create serverless inference endpoint if model URL provided
        if (props?.modelDataUrl) {
            this.createInferenceEndpoint(props.modelDataUrl);
        }

        // Outputs
        new cdk.CfnOutput(this, 'TrainingBucketName', {
            value: this.trainingBucket.bucketName,
            description: 'S3 bucket for training data',
        });

        new cdk.CfnOutput(this, 'ModelBucketName', {
            value: this.modelBucket.bucketName,
            description: 'S3 bucket for model artifacts',
        });

        new cdk.CfnOutput(this, 'SageMakerRoleArn', {
            value: this.sagemakerRole.roleArn,
            description: 'IAM role for SageMaker',
        });

        new cdk.CfnOutput(this, 'TriggerTrainingCommand', {
            value: `aws lambda invoke --function-name bootboots-trigger-training --region ${this.region} response.json`,
            description: 'Command to trigger training',
        });
    }

    private createInferenceEndpoint(modelDataUrl: string): void {
        // Create SageMaker Model
        const model = new sagemaker.CfnModel(this, 'BootBootsModel', {
            modelName: 'bootboots-cat-classifier',
            executionRoleArn: this.sagemakerRole.roleArn,
            primaryContainer: {
                // TensorFlow serving container for JumpStart models
                image: `763104351884.dkr.ecr.${this.region}.amazonaws.com/tensorflow-inference:2.12-cpu`,
                modelDataUrl: modelDataUrl,
                environment: {
                    SAGEMAKER_PROGRAM: 'inference.py',
                },
            },
        });

        // Create Serverless Inference Config
        const endpointConfig = new sagemaker.CfnEndpointConfig(this, 'BootBootsEndpointConfig', {
            endpointConfigName: 'bootboots-serverless-config',
            productionVariants: [{
                modelName: model.modelName!,
                variantName: 'AllTraffic',
                serverlessConfig: {
                    maxConcurrency: 5,
                    memorySizeInMb: 2048,
                },
            }],
        });

        endpointConfig.addDependency(model);

        // Create Endpoint
        const endpoint = new sagemaker.CfnEndpoint(this, 'BootBootsEndpoint', {
            endpointName: 'bootboots',
            endpointConfigName: endpointConfig.endpointConfigName!,
        });

        endpoint.addDependency(endpointConfig);

        new cdk.CfnOutput(this, 'EndpointName', {
            value: endpoint.endpointName!,
            description: 'SageMaker inference endpoint name',
        });
    }
}
