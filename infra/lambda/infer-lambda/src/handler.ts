import { APIGatewayProxyEvent, APIGatewayProxyResult, Context } from 'aws-lambda';
import { Logger } from '@aws-lambda-powertools/logger';
import { SageMakerRuntimeClient, InvokeEndpointCommand } from '@aws-sdk/client-sagemaker-runtime';
import { S3Client, PutObjectCommand } from '@aws-sdk/client-s3';

const logger = new Logger();
const sagemakerClient = new SageMakerRuntimeClient({ region: process.env.AWS_REGION });
const s3Client = new S3Client({ region: process.env.AWS_REGION });

// Function to save image to S3
async function saveImageToS3(imageBuffer: Buffer): Promise<string> {
    const bucketName = process.env.IMAGES_BUCKET_NAME;
    if (!bucketName) {
        throw new Error('IMAGES_BUCKET_NAME environment variable is not set');
    }

    const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
    const key = `catcam-images/${timestamp}.jpg`;

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

            // Save image to S3 bucket
            let s3Key: string;
            try {
                s3Key = await saveImageToS3(imageBuffer);
                logger.info('Image saved to S3 successfully', { s3Key });
            } catch (s3Error) {
                logger.error('Failed to save image to S3, continuing with inference', { error: s3Error });
                // Continue with SageMaker inference even if S3 upload fails
            }

            // Prepare SageMaker endpoint invocation
            const endpointName = 'bootboots';
            const invokeCommand = new InvokeEndpointCommand({
                EndpointName: endpointName,
                ContentType: 'application/x-image',
                Body: imageBuffer
            });

            logger.info('Invoking SageMaker endpoint', { endpointName });

            // Invoke SageMaker endpoint
            const sagemakerResponse = await sagemakerClient.send(invokeCommand);
            
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

            // Return successful response
            return {
                statusCode: 200,
                headers: corsHeaders,
                body: JSON.stringify({
                    success: true,
                    data: sagemakerResult,
                    metadata: {
                        endpointName,
                        contentType: sagemakerResponse.ContentType,
                        timestamp: new Date().toISOString()
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