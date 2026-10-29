import * as path from 'path';
import * as cdk from 'aws-cdk-lib';
import * as s3 from 'aws-cdk-lib/aws-s3';
import * as s3assets from 'aws-cdk-lib/aws-s3-assets';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as lambda from 'aws-cdk-lib/aws-lambda';
import * as logs from 'aws-cdk-lib/aws-logs';
import * as sagemaker from 'aws-cdk-lib/aws-sagemaker';
import { NodejsFunction } from 'aws-cdk-lib/aws-lambda-nodejs';
import { Construct } from 'constructs';
import { BOOTBOOTS, type DeployEnv } from './env-config';
import type { ModelSpec } from './model-config';

/**
 * Multiclass classification: one class per cat (alphabetical order).
 * Boots is index 0 — inference Lambda checks probs[0] for spray decision.
 */
export const CAT_CLASSES = ['Boots', 'Chi', 'Kappa', 'Mu', 'NoCat', 'Tau', 'Wolf'] as const;

/** TF Serving container the model runs in (AWS's deep learning container registry). */
const TF_SERVING_IMAGE = (region: string) => `763104351884.dkr.ecr.${region}.amazonaws.com/tensorflow-inference:2.16-cpu`;

export interface AiTrainingStackProps extends cdk.StackProps {
    deployEnv: DeployEnv;
    /** The model the `bootboots` endpoint serves (infra/model.json); null for no endpoint. */
    model: ModelSpec | null;
}

export class AiTrainingStack extends cdk.Stack {
    public readonly modelBucket: s3.IBucket;
    public readonly sagemakerRole: iam.Role;

    constructor(scope: Construct, id: string, props: AiTrainingStackProps) {
        super(scope, id, props);

        // Model artefacts and the role SageMaker serves them with: every environment.
        this.modelBucket = new s3.Bucket(this, 'ModelArtifactsBucket', {
            bucketName: `bootboots-models-${this.account}`,
            versioned: true, // Keep model versions
            publicReadAccess: false,
            blockPublicAccess: s3.BlockPublicAccess.BLOCK_ALL,
            removalPolicy: cdk.RemovalPolicy.RETAIN,
        });

        // IAM role for SageMaker training jobs and the endpoint's model
        this.sagemakerRole = new iam.Role(this, 'SageMakerExecutionRole', {
            roleName: 'BootBootsSageMakerRole',
            assumedBy: new iam.ServicePrincipal('sagemaker.amazonaws.com'),
            managedPolicies: [
                iam.ManagedPolicy.fromAwsManagedPolicyName('AmazonSageMakerFullAccess'),
            ],
        });
        this.modelBucket.grantReadWrite(this.sagemakerRole);

        // Training only happens in sandbox; other environments get a trained model promoted to them.
        if (props.deployEnv === 'sandbox') {
            this.createTrainingPipeline();
        }

        if (props.model) {
            this.createInferenceEndpoint(props.model);
        }

        new cdk.CfnOutput(this, 'ModelBucketName', {
            value: this.modelBucket.bucketName,
            description: 'S3 bucket for model artifacts',
        });

        new cdk.CfnOutput(this, 'SageMakerRoleArn', {
            value: this.sagemakerRole.roleArn,
            description: 'IAM role for SageMaker',
        });
    }

    private createTrainingPipeline(): void {

        // Reference the existing images bucket
        const imagesBucket = s3.Bucket.fromBucketName(
            this,
            'ImagesBucket',
            BOOTBOOTS.imagesBucket(this.account, this.region)
        );

        // Create bucket for organized training data (folder structure by class)
        const trainingBucket = new s3.Bucket(this, 'TrainingDataBucket', {
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

        // Grant S3 access to SageMaker role
        trainingBucket.grantRead(this.sagemakerRole);
        imagesBucket.grantRead(this.sagemakerRole);

        // Package the training script as a tar.gz CDK asset uploaded to the
        // bootstrap bucket on every `cdk deploy`. Docker bundling is used so
        // that SageMaker's TF container receives a proper tar.gz (it does not
        // handle plain zip files). CDK re-bundles automatically when the
        // content hash of infra/training/ changes.
        const trainingScriptAsset = new s3assets.Asset(this, 'TrainingScriptAsset', {
            path: path.join(__dirname, '../training'),
            bundling: {
                image: cdk.DockerImage.fromRegistry('public.ecr.aws/docker/library/alpine:3.18'),
                command: [
                    'sh', '-c',
                    'tar -czf /asset-output/sourcedir.tar.gz -C /asset-input .',
                ],
                outputType: cdk.BundlingOutput.ARCHIVED,
            },
        });
        trainingScriptAsset.grantRead(this.sagemakerRole);

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
                TARGET_BUCKET: trainingBucket.bucketName,
                CATADATA_TABLE: BOOTBOOTS.catadataTable,
                CAT_CLASSES: CAT_CLASSES.join(','),
                VALIDATION_SPLIT: '0.2', // 20% for validation
                MAX_NOTBOOTS_SAMPLES: '600', // Cap NotBoots at ~3× Boots count to limit imbalance
            },
            bundling: {
                minify: true,
                sourceMap: false,
                target: 'node22',
            },
        });

        // Grant permissions to data prep Lambda
        imagesBucket.grantRead(dataPrepLambda);
        trainingBucket.grantReadWrite(dataPrepLambda);
        dataPrepLambda.addToRolePolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['dynamodb:Scan'],
            resources: [`arn:aws:dynamodb:${this.region}:${this.account}:table/${BOOTBOOTS.catadataTable}`],
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
                TRAINING_BUCKET: trainingBucket.bucketName,
                MODEL_BUCKET: this.modelBucket.bucketName,
                SAGEMAKER_ROLE_ARN: this.sagemakerRole.roleArn,
                DATA_PREP_FUNCTION: dataPrepLambda.functionName,
                NUM_CLASSES: CAT_CLASSES.length.toString(),
                TRAINING_SCRIPT_BUCKET: trainingScriptAsset.s3BucketName,
                TRAINING_SCRIPT_KEY: trainingScriptAsset.s3ObjectKey,
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

        trainingBucket.grantRead(trainingTriggerLambda);
        this.modelBucket.grantRead(trainingTriggerLambda);

        new cdk.CfnOutput(this, 'TrainingBucketName', {
            value: trainingBucket.bucketName,
            description: 'S3 bucket for training data',
        });

        new cdk.CfnOutput(this, 'TriggerTrainingCommand', {
            value: `aws lambda invoke --function-name bootboots-trigger-training --region ${this.region} response.json`,
            description: 'Command to trigger training',
        });
    }

    /**
     * The `bootboots` serverless endpoint, serving the model named in infra/model.json. SageMaker
     * models and endpoint configs are immutable, so both are named from the model's label: a new
     * label creates a new pair and the endpoint switches to it in place (no downtime), after which
     * CloudFormation deletes the old pair. Matches what local-training/deploy_model.sh used to create
     * by hand (no container environment, no execution role on the config).
     */
    private createInferenceEndpoint(spec: ModelSpec): void {
        const model = new sagemaker.CfnModel(this, 'BootBootsModel', {
            modelName: `bootboots-${spec.label}`,
            executionRoleArn: this.sagemakerRole.roleArn,
            primaryContainer: {
                image: TF_SERVING_IMAGE(this.region),
                modelDataUrl: `s3://${this.modelBucket.bucketName}/${spec.artefact}`,
            },
        });

        const endpointConfig = new sagemaker.CfnEndpointConfig(this, 'BootBootsEndpointConfig', {
            endpointConfigName: `bootboots-config-${spec.label}`,
            productionVariants: [{
                modelName: model.attrModelName,
                variantName: 'AllTraffic',
                serverlessConfig: {
                    maxConcurrency: 5,
                    memorySizeInMb: 2048,
                },
            }],
        });

        const endpoint = new sagemaker.CfnEndpoint(this, 'BootBootsEndpoint', {
            endpointName: 'bootboots',
            endpointConfigName: endpointConfig.attrEndpointConfigName,
        });
        // Taking the model out of model.json (or the stack away) leaves the endpoint running.
        endpoint.applyRemovalPolicy(cdk.RemovalPolicy.RETAIN);

        new cdk.CfnOutput(this, 'EndpointName', {
            value: endpoint.endpointName!,
            description: 'SageMaker inference endpoint name',
        });
        new cdk.CfnOutput(this, 'ModelLabel', {
            value: spec.label,
            description: 'The model the endpoint serves (infra/model.json)',
        });
    }
}
