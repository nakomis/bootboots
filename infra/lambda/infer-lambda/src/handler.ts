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

            // Parse request body
            let requestBody;
            try {
                requestBody = event.body ? JSON.parse(event.body) : {};
            } catch (parseError) {
                logger.error('Failed to parse request body', { error: parseError });
                return {
                    statusCode: 400,
                    headers: corsHeaders,
                    body: JSON.stringify({
                        error: 'Invalid JSON in request body'
                    })
                };
            }

            logger.info('Parsed request body', { requestBody });

            // Prepare SageMaker endpoint invocation
            const endpointName = 'bootboots';
            const invokeCommand = new InvokeEndpointCommand({
                EndpointName: endpointName,
                ContentType: 'application/json',
                Body: JSON.stringify(requestBody)
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