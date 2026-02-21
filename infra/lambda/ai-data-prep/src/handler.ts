import {
    S3Client,
    CopyObjectCommand,
    GetObjectCommand,
    PutObjectCommand,
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
    const validationSplit = parseFloat(process.env.VALIDATION_SPLIT || '0.2');
    const maxNotBootsSamples = parseInt(process.env.MAX_NOTBOOTS_SAMPLES || '600');

    const errors: string[] = [];
    const classDistribution: Record<string, { training: number; validation: number }> = {
        Boots: { training: 0, validation: 0 },
        NotBoots: { training: 0, validation: 0 },
    };

    console.log('Fetching labeled records from DynamoDB...');

    const labeledRecords = await fetchLabeledRecords();
    console.log(`Found ${labeledRecords.length} labeled records`);

    // Split into binary classes: Boots vs everything else
    const bootRecords: CatadataRecord[] = [];
    const notBootsRecords: CatadataRecord[] = [];

    for (const record of labeledRecords) {
        if (record.cat === 'Boots') {
            bootRecords.push(record);
        } else if (record.cat) {
            notBootsRecords.push({ ...record, cat: 'NotBoots' });
        }
    }

    console.log(`Before capping — Boots: ${bootRecords.length}, NotBoots: ${notBootsRecords.length}`);

    // Cap NotBoots to limit class imbalance (shuffle first for variety across all cat types)
    const cappedNotBoots = shuffleArray(notBootsRecords).slice(0, maxNotBootsSamples);
    console.log(`After capping — NotBoots: ${cappedNotBoots.length} (max ${maxNotBootsSamples})`);

    // Clear existing training/validation data
    await clearExistingData(targetBucket, 'training/');
    await clearExistingData(targetBucket, 'validation/');

    let totalTraining = 0;
    let totalValidation = 0;

    for (const [className, records] of [['Boots', bootRecords], ['NotBoots', cappedNotBoots]] as const) {
        const shuffled = shuffleArray(records);
        const splitIndex = Math.floor(shuffled.length * (1 - validationSplit));
        const trainingRecords = shuffled.slice(0, splitIndex);
        const validationRecords = shuffled.slice(splitIndex);

        // Copy training images
        for (const record of trainingRecords) {
            try {
                const copied = await copyImage(sourceBucket, sourcePrefix, targetBucket, `training/${className}/`, record.imageName);
                if (copied) { classDistribution[className].training++; totalTraining++; }
            } catch (error) {
                errors.push(`Failed to copy training image ${record.imageName}: ${error}`);
            }
        }

        // Copy validation images
        for (const record of validationRecords) {
            try {
                const copied = await copyImage(sourceBucket, sourcePrefix, targetBucket, `validation/${className}/`, record.imageName);
                if (copied) { classDistribution[className].validation++; totalValidation++; }
            } catch (error) {
                errors.push(`Failed to copy validation image ${record.imageName}: ${error}`);
            }
        }

        console.log(`${className}: ${classDistribution[className].training} training, ${classDistribution[className].validation} validation`);
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

const JPEG_HEADER = Buffer.from([0xFF, 0xD8, 0xFF]);
const JPEG_EOI    = Buffer.from([0xFF, 0xD9]);

/**
 * Copy an image from the source bucket to the training bucket.
 *
 * - If the image is a well-formed JPEG (starts FF D8 FF, ends FF D9):
 *   use a server-side S3 copy (fast, no data transfer).
 * - If it has a valid JPEG header but is missing the EOI marker (FF D9):
 *   download, append FF D9, and re-upload. Many old catcam images were
 *   captured with a truncated stream and only need this two-byte fix.
 * - Otherwise: skip and return false so the caller doesn't count it.
 */
async function copyImage(
    sourceBucket: string,
    sourcePrefix: string,
    targetBucket: string,
    targetPrefix: string,
    imageName: string
): Promise<boolean> {
    const sourceKey = imageName.startsWith(sourcePrefix)
        ? imageName
        : `${sourcePrefix}${imageName}`;
    const filename  = imageName.split('/').pop()!;
    const targetKey = `${targetPrefix}${filename}`;

    // Peek at start and end bytes using cheap range GETs
    const [startRes, endRes] = await Promise.all([
        s3.send(new GetObjectCommand({ Bucket: sourceBucket, Key: sourceKey, Range: 'bytes=0-2' })),
        s3.send(new GetObjectCommand({ Bucket: sourceBucket, Key: sourceKey, Range: 'bytes=-2' })),
    ]);
    const start = Buffer.from(await startRes.Body!.transformToByteArray());
    const end   = Buffer.from(await endRes.Body!.transformToByteArray());

    const hasJpegHeader = start[0] === 0xFF && start[1] === 0xD8 && start[2] === 0xFF;
    const hasEoi        = end[0] === 0xFF && end[1] === 0xD9;

    if (!hasJpegHeader) {
        console.warn(`Skipping non-JPEG file: ${sourceKey}`);
        return false;
    }

    if (hasEoi) {
        // Already valid - use a fast server-side copy
        await s3.send(new CopyObjectCommand({
            CopySource: encodeURIComponent(`${sourceBucket}/${sourceKey}`),
            Bucket: targetBucket,
            Key: targetKey,
            ContentType: 'image/jpeg',
        }));
        return true;
    }

    // Truncated JPEG - download, append EOI, re-upload
    console.log(`Patching truncated JPEG: ${sourceKey}`);
    const fullRes = await s3.send(new GetObjectCommand({ Bucket: sourceBucket, Key: sourceKey }));
    const original = Buffer.from(await fullRes.Body!.transformToByteArray());
    const patched  = Buffer.concat([original, JPEG_EOI]);

    await s3.send(new PutObjectCommand({
        Bucket: targetBucket,
        Key: targetKey,
        Body: patched,
        ContentType: 'image/jpeg',
    }));
    return true;
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
