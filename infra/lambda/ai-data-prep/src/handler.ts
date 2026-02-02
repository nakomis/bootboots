import {
    S3Client,
    CopyObjectCommand,
    ListObjectsV2Command,
    DeleteObjectsCommand,
} from '@aws-sdk/client-s3';
import { DynamoDBClient, ScanCommand } from '@aws-sdk/client-dynamodb';

const s3 = new S3Client({});
const dynamodb = new DynamoDBClient({});

interface CatadataRecord {
    imageName: string;
    cat: string;
}

interface PrepareDataResult {
    success: boolean;
    trainingImages: number;
    validationImages: number;
    classDistribution: Record<string, { training: number; validation: number }>;
    errors: string[];
}

/**
 * Prepares training data by:
 * 1. Reading labels from DynamoDB catadata table
 * 2. Copying images from source bucket to organized folder structure
 * 3. Splitting data into training/validation sets
 * 4. Undersampling overrepresented classes (NoCat)
 */
export async function handler(): Promise<PrepareDataResult> {
    const sourceBucket = process.env.SOURCE_BUCKET!;
    const sourcePrefix = process.env.SOURCE_PREFIX!;
    const targetBucket = process.env.TARGET_BUCKET!;
    const catClasses = process.env.CAT_CLASSES!.split(',');
    const validationSplit = parseFloat(process.env.VALIDATION_SPLIT || '0.2');
    const maxNoCatSamples = parseInt(process.env.MAX_NOCAT_SAMPLES || '200');

    const errors: string[] = [];
    const classDistribution: Record<string, { training: number; validation: number }> = {};

    // Initialize class distribution
    for (const cat of catClasses) {
        classDistribution[cat] = { training: 0, validation: 0 };
    }

    console.log('Fetching labeled records from DynamoDB...');

    // Fetch all labeled records from DynamoDB
    const labeledRecords = await fetchLabeledRecords();
    console.log(`Found ${labeledRecords.length} labeled records`);

    // Group records by class
    const recordsByClass: Record<string, CatadataRecord[]> = {};
    for (const cat of catClasses) {
        recordsByClass[cat] = [];
    }

    for (const record of labeledRecords) {
        if (catClasses.includes(record.cat)) {
            recordsByClass[record.cat].push(record);
        }
    }

    // Log class distribution before processing
    console.log('Class distribution before processing:');
    for (const cat of catClasses) {
        console.log(`  ${cat}: ${recordsByClass[cat].length}`);
    }

    // Clear existing training/validation data
    await clearExistingData(targetBucket, 'training/');
    await clearExistingData(targetBucket, 'validation/');

    let totalTraining = 0;
    let totalValidation = 0;

    // Process each class
    for (const cat of catClasses) {
        let records = recordsByClass[cat];

        // Undersample NoCat if needed
        if (cat === 'NoCat' && records.length > maxNoCatSamples) {
            console.log(`Undersampling NoCat from ${records.length} to ${maxNoCatSamples}`);
            records = shuffleArray(records).slice(0, maxNoCatSamples);
        }

        // Shuffle and split into training/validation
        const shuffled = shuffleArray(records);
        const splitIndex = Math.floor(shuffled.length * (1 - validationSplit));
        const trainingRecords = shuffled.slice(0, splitIndex);
        const validationRecords = shuffled.slice(splitIndex);

        // Copy training images
        for (const record of trainingRecords) {
            try {
                await copyImage(
                    sourceBucket,
                    sourcePrefix,
                    targetBucket,
                    `training/${cat}/`,
                    record.imageName
                );
                classDistribution[cat].training++;
                totalTraining++;
            } catch (error) {
                errors.push(`Failed to copy training image ${record.imageName}: ${error}`);
            }
        }

        // Copy validation images
        for (const record of validationRecords) {
            try {
                await copyImage(
                    sourceBucket,
                    sourcePrefix,
                    targetBucket,
                    `validation/${cat}/`,
                    record.imageName
                );
                classDistribution[cat].validation++;
                totalValidation++;
            } catch (error) {
                errors.push(`Failed to copy validation image ${record.imageName}: ${error}`);
            }
        }

        console.log(`${cat}: ${classDistribution[cat].training} training, ${classDistribution[cat].validation} validation`);
    }

    console.log(`Data preparation complete: ${totalTraining} training, ${totalValidation} validation images`);
    if (errors.length > 0) {
        console.warn(`${errors.length} errors occurred:`, errors.slice(0, 10));
    }

    return {
        success: errors.length === 0,
        trainingImages: totalTraining,
        validationImages: totalValidation,
        classDistribution,
        errors: errors.slice(0, 50), // Limit error output
    };
}

async function fetchLabeledRecords(): Promise<CatadataRecord[]> {
    const records: CatadataRecord[] = [];
    let lastEvaluatedKey: Record<string, any> | undefined;

    do {
        const command = new ScanCommand({
            TableName: process.env.CATADATA_TABLE,
            FilterExpression: 'attribute_exists(cat)',
            ExclusiveStartKey: lastEvaluatedKey,
        });

        const result = await dynamodb.send(command);

        if (result.Items) {
            for (const item of result.Items) {
                if (item.imageName?.S && item.cat?.S) {
                    records.push({
                        imageName: item.imageName.S,
                        cat: item.cat.S,
                    });
                }
            }
        }

        lastEvaluatedKey = result.LastEvaluatedKey;
    } while (lastEvaluatedKey);

    return records;
}

async function copyImage(
    sourceBucket: string,
    sourcePrefix: string,
    targetBucket: string,
    targetPrefix: string,
    imageName: string
): Promise<void> {
    // Handle both full paths and just filenames
    const sourceKey = imageName.startsWith(sourcePrefix)
        ? imageName
        : `${sourcePrefix}${imageName}`;

    const filename = imageName.split('/').pop()!;
    const targetKey = `${targetPrefix}${filename}`;

    await s3.send(new CopyObjectCommand({
        CopySource: encodeURIComponent(`${sourceBucket}/${sourceKey}`),
        Bucket: targetBucket,
        Key: targetKey,
    }));
}

async function clearExistingData(bucket: string, prefix: string): Promise<void> {
    console.log(`Clearing existing data in s3://${bucket}/${prefix}...`);

    let continuationToken: string | undefined;

    do {
        const listResult = await s3.send(new ListObjectsV2Command({
            Bucket: bucket,
            Prefix: prefix,
            ContinuationToken: continuationToken,
        }));

        if (listResult.Contents && listResult.Contents.length > 0) {
            await s3.send(new DeleteObjectsCommand({
                Bucket: bucket,
                Delete: {
                    Objects: listResult.Contents.map(obj => ({ Key: obj.Key })),
                },
            }));
        }

        continuationToken = listResult.NextContinuationToken;
    } while (continuationToken);
}

function shuffleArray<T>(array: T[]): T[] {
    const shuffled = [...array];
    for (let i = shuffled.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [shuffled[i], shuffled[j]] = [shuffled[j], shuffled[i]];
    }
    return shuffled;
}
