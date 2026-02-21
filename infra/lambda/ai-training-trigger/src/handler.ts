import {
    SageMakerClient,
    CreateTrainingJobCommand,
    DescribeTrainingJobCommand,
} from '@aws-sdk/client-sagemaker';
import { LambdaClient, InvokeCommand } from '@aws-sdk/client-lambda';

const sagemaker = new SageMakerClient({});
const lambda = new LambdaClient({});

interface TrainingConfig {
    skipDataPrep?: boolean;
    epochs?: number;
    learningRate?: number;
    batchSize?: number;
    trainOnlyTopLayer?: boolean;
}

interface TriggerTrainingResult {
    success: boolean;
    trainingJobName?: string;
    dataPrepResult?: any;
    error?: string;
}

/**
 * Triggers a SageMaker training job:
 * 1. Invokes data prep Lambda to organize training data
 * 2. Creates a SageMaker training job with JumpStart transfer learning
 * 3. Returns the training job name for monitoring
 */
export async function handler(event: TrainingConfig = {}): Promise<TriggerTrainingResult> {
    const trainingBucket = process.env.TRAINING_BUCKET!;
    const modelBucket = process.env.MODEL_BUCKET!;
    const sagemakerRoleArn = process.env.SAGEMAKER_ROLE_ARN!;
    const dataPrepFunction = process.env.DATA_PREP_FUNCTION!;
    const numClasses = parseInt(process.env.NUM_CLASSES || '7');
    const region = process.env.AWS_REGION || 'eu-west-2';
    const trainingScriptBucket = process.env.TRAINING_SCRIPT_BUCKET!;
    const trainingScriptKey = process.env.TRAINING_SCRIPT_KEY!;
    const trainingScriptUri = `s3://${trainingScriptBucket}/${trainingScriptKey}`;

    // Hyperparameters with defaults optimized for this use case
    const epochs = event.epochs || 50;
    const learningRate = event.learningRate || 0.0001;
    const batchSize = event.batchSize || 16;
    const trainOnlyTopLayer = event.trainOnlyTopLayer ?? false; // Fine-tune entire network

    try {
        // Step 1: Prepare training data (unless skipped)
        let dataPrepResult: any;
        if (!event.skipDataPrep) {
            console.log('Invoking data prep Lambda...');
            const invokeResult = await lambda.send(new InvokeCommand({
                FunctionName: dataPrepFunction,
                InvocationType: 'RequestResponse',
            }));

            if (invokeResult.Payload) {
                dataPrepResult = JSON.parse(Buffer.from(invokeResult.Payload).toString());
                console.log('Data prep result:', dataPrepResult);

                if (!dataPrepResult.success) {
                    return {
                        success: false,
                        dataPrepResult,
                        error: 'Data preparation failed',
                    };
                }

                if (dataPrepResult.trainingImages < 50) {
                    return {
                        success: false,
                        dataPrepResult,
                        error: `Insufficient training data: only ${dataPrepResult.trainingImages} images`,
                    };
                }
            }
        }

        // Step 2: Create training job
        const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
        const trainingJobName = `bootboots-${timestamp}`;

        console.log(`Creating training job: ${trainingJobName}`);

        // JumpStart TensorFlow Image Classification container
        const trainingImage = `763104351884.dkr.ecr.${region}.amazonaws.com/tensorflow-training:2.12-cpu-py310`;

        await sagemaker.send(new CreateTrainingJobCommand({
            TrainingJobName: trainingJobName,
            RoleArn: sagemakerRoleArn,
            AlgorithmSpecification: {
                TrainingImage: trainingImage,
                TrainingInputMode: 'File',
                MetricDefinitions: [
                    { Name: 'train:loss', Regex: 'loss: ([0-9\\.]+)' },
                    { Name: 'train:accuracy', Regex: 'accuracy: ([0-9\\.]+)' },
                    { Name: 'validation:loss', Regex: 'val_loss: ([0-9\\.]+)' },
                    { Name: 'validation:accuracy', Regex: 'val_accuracy: ([0-9\\.]+)' },
                ],
            },
            HyperParameters: {
                // Model configuration
                'epochs': epochs.toString(),
                'learning_rate': learningRate.toString(),
                'batch_size': batchSize.toString(),
                'train_only_top_layer': trainOnlyTopLayer ? 'True' : 'False',
                'dropout_rate': '0.3',
                'regularizers_l2': '0.0001',

                // Data augmentation (critical for small dataset)
                'augmentation': 'True',

                // Early stopping
                'early_stopping': 'True',
                'early_stopping_patience': '10',
                'early_stopping_min_delta': '0.001',

                // Model settings
                'num_classes': numClasses.toString(),

                // Training script location (CDK asset, zip of infra/training/)
                'sagemaker_program': 'train.py',
                'sagemaker_submit_directory': trainingScriptUri,
            },
            InputDataConfig: [
                {
                    ChannelName: 'training',
                    DataSource: {
                        S3DataSource: {
                            S3DataType: 'S3Prefix',
                            S3Uri: `s3://${trainingBucket}/training/`,
                            S3DataDistributionType: 'FullyReplicated',
                        },
                    },
                    ContentType: 'application/x-image',
                    InputMode: 'File',
                },
                {
                    ChannelName: 'validation',
                    DataSource: {
                        S3DataSource: {
                            S3DataType: 'S3Prefix',
                            S3Uri: `s3://${trainingBucket}/validation/`,
                            S3DataDistributionType: 'FullyReplicated',
                        },
                    },
                    ContentType: 'application/x-image',
                    InputMode: 'File',
                },
            ],
            OutputDataConfig: {
                S3OutputPath: `s3://${modelBucket}/training-output/`,
            },
            ResourceConfig: {
                InstanceType: 'ml.m5.xlarge',
                InstanceCount: 1,
                VolumeSizeInGB: 30,
            },
            StoppingCondition: {
                MaxRuntimeInSeconds: 7200, // 2 hours max
            },
            Tags: [
                { Key: 'Project', Value: 'BootBoots' },
                { Key: 'Purpose', Value: 'CatClassification' },
            ],
        }));

        console.log(`Training job ${trainingJobName} created successfully`);

        // Verify the job was created
        const describeResult = await sagemaker.send(new DescribeTrainingJobCommand({
            TrainingJobName: trainingJobName,
        }));

        return {
            success: true,
            trainingJobName,
            dataPrepResult,
        };
    } catch (error) {
        console.error('Failed to create training job:', error);
        return {
            success: false,
            error: error instanceof Error ? error.message : String(error),
        };
    }
}
