import { APIGatewayProxyEvent, APIGatewayProxyResult, Context } from 'aws-lambda';
import { Logger } from '@aws-lambda-powertools/logger';
import { SageMakerRuntimeClient, InvokeEndpointCommand } from '@aws-sdk/client-sagemaker-runtime';
import { S3Client, PutObjectCommand } from '@aws-sdk/client-s3';
import { DynamoDBClient } from '@aws-sdk/client-dynamodb';
import { DynamoDBDocumentClient, PutCommand } from '@aws-sdk/lib-dynamodb';
import { randomUUID } from 'crypto';
import Anthropic from '@anthropic-ai/sdk';

const logger = new Logger();
const sagemakerClient = new SageMakerRuntimeClient({ region: process.env.AWS_REGION });
const s3Client = new S3Client({ region: process.env.AWS_REGION });
const dynamoClient = new DynamoDBClient({ region: process.env.AWS_REGION });
const docClient = DynamoDBDocumentClient.from(dynamoClient);
const anthropicClient = new Anthropic({ apiKey: process.env.ANTHROPIC_API_KEY });

// Cat names in order corresponding to the probability array indices
// [0]=Boots, [1]=NotBoots (binary classifier)
const CAT_NAMES = ['Boots', 'NotBoots'];

// Minimum Boots confidence to record an event in catcam-events table
const EVENTS_MIN_CONFIDENCE = parseFloat(process.env.EVENTS_MIN_CONFIDENCE ?? '0.5');

// Function to get the most likely cat from probabilities
function getMostLikelyCat(probabilities: number[]): { name: string; confidence: number; index: number } {
    let maxIndex = 0;
    let maxProbability = probabilities[0];
    
    for (let i = 1; i < probabilities.length; i++) {
        if (probabilities[i] > maxProbability) {
            maxProbability = probabilities[i];
            maxIndex = i;
        }
    }
    
    return {
        name: CAT_NAMES[maxIndex] || 'Unknown',
        confidence: maxProbability,
        index: maxIndex
    };
}

// Function to record an inference event in catcam-events table
async function createCatcamEventRecord(s3Key: string, bootsConfidence: number, claudeResult?: ClaudeResult): Promise<void> {
    const eventsTable = process.env.CATCAM_EVENTS_TABLE_NAME;
    if (!eventsTable) {
        throw new Error('CATCAM_EVENTS_TABLE_NAME environment variable is not set');
    }

    const item: Record<string, any> = {
        id: randomUUID(),
        timestamp: new Date().toISOString(),
        imageName: s3Key,
        bootsConfidence,
    };
    if (claudeResult) {
        item.claudeResult = claudeResult;
    }

    const putCommand = new PutCommand({
        TableName: eventsTable,
        Item: item,
    });

    try {
        await docClient.send(putCommand);
        logger.info('Recorded catcam event', { imageName: s3Key, bootsConfidence });
    } catch (error) {
        logger.error('Failed to record catcam event', { error, imageName: s3Key });
        throw error;
    }
}

interface ClaudeResult {
    cat: string;
    confidence: string;
    reasoning: string;
}

const CLAUDE_SYSTEM_PROMPT = `You are a cat identification system for a home monitoring project. Identify which cat is visible in the image from the following list:

- Boots: solid black, short-to-medium coat, bright green eyes. This is the neighbour's cat and the primary target of the system.
- Chi: Maine Coon, dark brown/black tabby pattern, long fluffy fur, green eyes.
- Tau: black and white tuxedo pattern (black head/back, white chest/belly), short hair.
- Kappa: very dark brown/near-black, long fluffy fur (son of Chi and Tau, inherited Chi's coat).
- Mu: Maine Coon, ginger/cream coloured, long fluffy fur, regal expression.
- Wolf: a grey/white stuffed toy cat — not a real cat.

Respond with valid JSON only, no other text:
{"cat": "<name>", "confidence": "high|medium|low", "reasoning": "<one sentence>"}

If no cat is clearly visible, respond with:
{"cat": "Unknown", "confidence": "low", "reasoning": "<brief description of what is visible>"}`;

async function invokeClaudeVision(imageBuffer: Buffer): Promise<ClaudeResult> {
    const base64Image = imageBuffer.toString('base64');

    const response = await anthropicClient.messages.create({
        model: 'claude-haiku-4-5-20251001',
        max_tokens: 150,
        system: CLAUDE_SYSTEM_PROMPT,
        messages: [{
            role: 'user',
            content: [{
                type: 'image',
                source: { type: 'base64', media_type: 'image/jpeg', data: base64Image },
            }],
        }],
    });

    const text = response.content[0].type === 'text' ? response.content[0].text : '';
    try {
        return JSON.parse(text) as ClaudeResult;
    } catch {
        logger.warn('Could not parse Claude response as JSON', { text });
        return { cat: 'Unknown', confidence: 'low', reasoning: text.substring(0, 200) };
    }
}

// Function to create a catadata record for training images
async function createCatadataRecord(s3Key: string): Promise<void> {
    const uuid = randomUUID();

    const putCommand = new PutCommand({
        TableName: 'catadata',
        Item: {
            imageName: s3Key,
            uuid: uuid,
            createdAt: new Date().toISOString(),
        }
    });

    try {
        await docClient.send(putCommand);
        logger.info('Successfully created catadata record', { imageName: s3Key, uuid });
    } catch (error) {
        logger.error('Failed to create catadata record', { error, imageName: s3Key });
        throw error;
    }
}

// Function to save image to S3
async function saveImageToS3(imageBuffer: Buffer, timestamp: string, trainingMode: boolean = false): Promise<string> {
    const bucketName = process.env.IMAGES_BUCKET_NAME;
    if (!bucketName) {
        throw new Error('IMAGES_BUCKET_NAME environment variable is not set');
    }

    const imagePrefix = trainingMode ? 'catcam-training' : 'catcam-images';
    const key = `${imagePrefix}/${timestamp}.jpg`;

    const putCommand = new PutObjectCommand({
        Bucket: bucketName,
        Key: key,
        Body: imageBuffer,
        ContentType: 'image/jpeg',
        Metadata: {
            'uploaded-by': 'bootboots-lambda',
            'timestamp': new Date().toISOString()
        }
    });

    try {
        await s3Client.send(putCommand);
        logger.info('Successfully saved image to S3', { bucket: bucketName, key });
        return key;
    } catch (error) {
        logger.error('Failed to save image to S3', { error, bucket: bucketName, key });
        throw error;
    }
}

// Function to save SageMaker response to S3 as JSON
async function saveSageMakerResponseToS3(responseData: any, timestamp: string, mostLikelyCat: any): Promise<string> {
    const bucketName = process.env.IMAGES_BUCKET_NAME;
    if (!bucketName) {
        throw new Error('IMAGES_BUCKET_NAME environment variable is not set');
    }

    const key = `catcam-images/${timestamp}.txt`;
    
    const responseWithCatInfo = {
        ...responseData,
        mostLikelyCat,
        catNames: CAT_NAMES,
        timestamp: new Date().toISOString()
    };

    const putCommand = new PutObjectCommand({
        Bucket: bucketName,
        Key: key,
        Body: JSON.stringify(responseWithCatInfo, null, 2),
        ContentType: 'application/json',
        Metadata: {
            'uploaded-by': 'bootboots-lambda',
            'timestamp': new Date().toISOString(),
            'most-likely-cat': mostLikelyCat.name
        }
    });

    try {
        await s3Client.send(putCommand);
        logger.info('Successfully saved SageMaker response to S3', { bucket: bucketName, key, mostLikelyCat: mostLikelyCat.name });
        return key;
    } catch (error) {
        logger.error('Failed to save SageMaker response to S3', { error, bucket: bucketName, key });
        throw error;
    }
}

export async function handler(event: APIGatewayProxyEvent, _context: Context): Promise<APIGatewayProxyResult> {
        logger.info('BootBoots Infer Lambda function invoked', {
            httpMethod: event.httpMethod,
            path: event.path,
            headers: event.headers
        });

        // CORS headers for all responses
        const corsHeaders = {
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'POST, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type, Authorization',
            'Content-Type': 'application/json'
        };

        try {
            // Handle preflight OPTIONS request
            if (event.httpMethod === 'OPTIONS') {
                return {
                    statusCode: 200,
                    headers: corsHeaders,
                    body: ''
                };
            }

            // Validate HTTP method
            if (event.httpMethod !== 'POST') {
                return {
                    statusCode: 405,
                    headers: corsHeaders,
                    body: JSON.stringify({
                        error: 'Method not allowed. Only POST requests are supported.'
                    })
                };
            }

            // Check for training mode and Claude inference flag
            const trainingMode = event.queryStringParameters?.mode === 'training';
            const claudeInfer = event.queryStringParameters?.claude === '1';
            if (trainingMode) {
                logger.info('Training mode detected - will skip SageMaker inference');
            }
            if (claudeInfer) {
                logger.info('Claude vision inference enabled for this request');
            }

            // Validate request body exists
            if (!event.body) {
                return {
                    statusCode: 400,
                    headers: corsHeaders,
                    body: JSON.stringify({
                        error: 'Request body is required'
                    })
                };
            }

            // Handle both raw binary data and base64-encoded data
            let imageBuffer: Buffer;
            
            // Handle the data based on its actual format, not just the isBase64Encoded flag
            let bodyData = event.body;
            
            // Check if the body looks like base64-encoded JPEG data
            // Base64-encoded JPEG typically starts with '/9j/' or similar
            if (bodyData.startsWith('/9j/') || bodyData.startsWith('iVBOR') || 
                (bodyData.length > 100 && /^[A-Za-z0-9+/]+=*$/.test(bodyData.substring(0, 100)))) {
                // This looks like base64 data, decode it
                try {
                    imageBuffer = Buffer.from(bodyData, 'base64');
                    logger.info('Decoded base64 image data', { 
                        originalSize: bodyData.length,
                        decodedSize: imageBuffer.length,
                        isBase64Encoded: event.isBase64Encoded,
                        firstBytes: imageBuffer.subarray(0, 10).toString('hex'),
                        bodyStart: bodyData.substring(0, 20)
                    });
                } catch (decodeError) {
                    logger.error('Failed to decode what appeared to be base64 data', { error: decodeError });
                    imageBuffer = Buffer.from(bodyData, 'binary');
                }
            } else if (event.isBase64Encoded) {
                // API Gateway says it's base64 encoded, but doesn't look like typical base64
                // Check if data appears to be URL-encoded
                if (bodyData.startsWith('%2F') || bodyData.includes('%')) {
                    try {
                        bodyData = decodeURIComponent(bodyData);
                        logger.info('URL decoded the request body');
                    } catch (decodeError) {
                        logger.warn('Failed to URL decode, using original data', { error: decodeError });
                    }
                }
                
                imageBuffer = Buffer.from(bodyData, 'binary');
                logger.info('Using API Gateway decoded binary data', { 
                    dataSize: imageBuffer.length,
                    isBase64Encoded: event.isBase64Encoded,
                    firstBytes: imageBuffer.subarray(0, 10).toString('hex'),
                    bodyStart: event.body.substring(0, 20)
                });
            } else {
                // Data might be base64-encoded string, try to decode it
                try {
                    imageBuffer = Buffer.from(event.body, 'base64');
                    logger.info('Successfully decoded base64 image', { 
                        originalSize: event.body.length,
                        decodedSize: imageBuffer.length,
                        isBase64Encoded: event.isBase64Encoded,
                        firstBytes: imageBuffer.subarray(0, 10).toString('hex')
                    });
                } catch (decodeError) {
                    // If base64 decode fails, treat as raw binary string
                    imageBuffer = Buffer.from(event.body, 'binary');
                    logger.info('Using raw binary data (base64 decode failed)', { 
                        dataSize: imageBuffer.length,
                        isBase64Encoded: event.isBase64Encoded,
                        firstBytes: imageBuffer.subarray(0, 10).toString('hex')
                    });
                }
            }
            
            // Validate JPEG header (should start with FFD8)
            const jpegHeader = imageBuffer.subarray(0, 2);
            if (jpegHeader[0] !== 0xFF || jpegHeader[1] !== 0xD8) {
                logger.warn('Image does not appear to be a valid JPEG', {
                    headerHex: jpegHeader.toString('hex'),
                    expectedHex: 'ffd8'
                });
            }

            // Generate timestamp for consistent naming
            // FIXME: We should pass the filename in from the device. The body is binary, so
            // we'll probably need to pass it in the form [fixed width<filename-buffer>][dynamic width<body>]
            const timestamp = new Date().toISOString();

            // Save image to S3 bucket
            let s3ImageKey: string | null = null;
            try {
                s3ImageKey = await saveImageToS3(imageBuffer, timestamp, trainingMode);
                logger.info('Image saved to S3 successfully', { s3ImageKey, trainingMode });
            } catch (s3Error) {
                logger.error('Failed to save image to S3, continuing with inference', { error: s3Error });
                // Continue with SageMaker inference even if S3 upload fails
            }

            // If training mode, create catadata record and return early (skip SageMaker inference)
            if (trainingMode) {
                logger.info('Training mode: skipping SageMaker inference');

                // Create catadata record for the training image
                if (s3ImageKey) {
                    try {
                        await createCatadataRecord(s3ImageKey);
                    } catch (dbError) {
                        logger.error('Failed to create catadata record, but image was saved', { error: dbError });
                        // Continue - image was saved successfully even if DB write failed
                    }
                }

                return {
                    statusCode: 200,
                    headers: corsHeaders,
                    body: JSON.stringify({
                        success: true,
                        mode: 'training',
                        metadata: {
                            timestamp: new Date().toISOString(),
                            s3ImageKey: s3ImageKey || null
                        }
                    })
                };
            }

            // Invoke SageMaker (and optionally Claude) in parallel
            const endpointName = 'bootboots';
            const invokeCommand = new InvokeEndpointCommand({
                EndpointName: endpointName,
                ContentType: 'application/x-image',
                Body: imageBuffer
            });

            logger.info('Invoking SageMaker endpoint', { endpointName, claudeInfer });

            const [sagemakerResponse, claudeResult] = await Promise.all([
                sagemakerClient.send(invokeCommand),
                claudeInfer
                    ? invokeClaudeVision(imageBuffer).catch((err) => {
                        logger.error('Claude inference failed, continuing without it', { error: err });
                        return undefined;
                    })
                    : Promise.resolve(undefined),
            ]);

            if (claudeResult) {
                logger.info('Claude vision result', { claudeResult });
            }

            // Parse SageMaker response
            const responseBody = new TextDecoder().decode(sagemakerResponse.Body);
            let sagemakerResult;

            try {
                sagemakerResult = JSON.parse(responseBody);
            } catch (parseError) {
                logger.warn('SageMaker response is not JSON, returning as text', { responseBody });
                sagemakerResult = { result: responseBody };
            }

            logger.info('SageMaker endpoint response received', {
                contentType: sagemakerResponse.ContentType,
                resultPreview: JSON.stringify(sagemakerResult).substring(0, 200)
            });

            // Determine the most likely cat from probabilities
            let mostLikelyCat = { name: 'Unknown', confidence: 0, index: -1 };
            if (sagemakerResult && sagemakerResult.probabilities && Array.isArray(sagemakerResult.probabilities)) {
                mostLikelyCat = getMostLikelyCat(sagemakerResult.probabilities);
                logger.info('Most likely cat detected', { mostLikelyCat });
            }

            // Save SageMaker response to S3 as JSON file
            try {
                const s3ResponseKey = await saveSageMakerResponseToS3(sagemakerResult, timestamp, mostLikelyCat);
                logger.info('SageMaker response saved to S3 successfully', { s3ResponseKey });
            } catch (s3Error) {
                logger.error('Failed to save SageMaker response to S3', { error: s3Error });
                // Continue with response even if S3 save fails
            }

            // Record event in catcam-events if Boots confidence meets threshold
            const bootsConfidence = sagemakerResult?.probabilities?.[0] ?? mostLikelyCat.confidence;
            if (s3ImageKey && bootsConfidence >= EVENTS_MIN_CONFIDENCE) {
                try {
                    await createCatcamEventRecord(s3ImageKey, bootsConfidence, claudeResult ?? undefined);
                } catch (dbError) {
                    logger.error('Failed to record catcam event, continuing', { error: dbError });
                    // Non-fatal: inference result is still returned
                }
            }

            // Return successful response with most likely cat information
            return {
                statusCode: 200,
                headers: corsHeaders,
                body: JSON.stringify({
                    success: true,
                    data: sagemakerResult,
                    mostLikelyCat,
                    metadata: {
                        endpointName,
                        contentType: sagemakerResponse.ContentType,
                        timestamp: new Date().toISOString(),
                        s3ImageKey: s3ImageKey || null
                    }
                })
            };

        } catch (error: any) {
            logger.error('Error processing request', { error });
            
            // Handle specific SageMaker errors
            if (error?.name === 'ValidationException') {
                return {
                    statusCode: 400,
                    headers: corsHeaders,
                    body: JSON.stringify({
                        error: 'Invalid request to SageMaker endpoint',
                        details: error.message
                    })
                };
            }
            
            if (error?.name === 'ModelError') {
                return {
                    statusCode: 422,
                    headers: corsHeaders,
                    body: JSON.stringify({
                        error: 'Model processing error',
                        details: error.message
                    })
                };
            }

            // Generic error response
            return {
                statusCode: 500,
                headers: corsHeaders,
                body: JSON.stringify({
                    error: 'Internal server error',
                    message: 'Failed to process inference request'
                })
            };
        }
}