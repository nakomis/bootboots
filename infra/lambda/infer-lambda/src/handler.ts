import { APIGatewayProxyEvent, APIGatewayProxyResult, Context } from 'aws-lambda';
import { Logger } from '@aws-lambda-powertools/logger';
import { SageMakerRuntimeClient, InvokeEndpointCommand } from '@aws-sdk/client-sagemaker-runtime';

const logger = new Logger();
const sagemakerClient = new SageMakerRuntimeClient({ region: process.env.AWS_REGION });

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

            // Base64 decode the request body and convert to buffer
            let imageBuffer: Buffer;
            try {
                imageBuffer = Buffer.from(event.body, 'base64');
                logger.info('Successfully decoded base64 image', { 
                    originalSize: event.body.length,
                    decodedSize: imageBuffer.length 
                });
            } catch (decodeError) {
                logger.error('Failed to decode base64 request body', { error: decodeError });
                return {
                    statusCode: 400,
                    headers: corsHeaders,
                    body: JSON.stringify({
                        error: 'Invalid base64 encoded request body'
                    })
                };
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