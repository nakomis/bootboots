import { APIGatewayProxyEvent, APIGatewayProxyResult, Context } from 'aws-lambda';
import { Logger } from '@aws-lambda-powertools/logger';
import { S3Client, GetObjectCommand } from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';
import { DynamoDBClient } from '@aws-sdk/client-dynamodb';
import { DynamoDBDocumentClient, ScanCommand } from '@aws-sdk/lib-dynamodb';

const logger = new Logger({ serviceName: 'events-query' });
const s3Client = new S3Client({ region: process.env.AWS_REGION });
const dynamoClient = new DynamoDBClient({ region: process.env.AWS_REGION });
const docClient = DynamoDBDocumentClient.from(dynamoClient);

const SIGNED_URL_EXPIRY_SECONDS = 60 * 60; // 1 hour

const corsHeaders = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type, Authorization',
    'Content-Type': 'application/json'
};

export async function handler(event: APIGatewayProxyEvent, _context: Context): Promise<APIGatewayProxyResult> {
    logger.info('Events query Lambda invoked', { httpMethod: event.httpMethod });

    if (event.httpMethod === 'OPTIONS') {
        return { statusCode: 200, headers: corsHeaders, body: '' };
    }

    const eventsTable = process.env.CATCAM_EVENTS_TABLE_NAME;
    const imagesBucket = process.env.IMAGES_BUCKET_NAME;

    if (!eventsTable || !imagesBucket) {
        logger.error('Missing required environment variables');
        return {
            statusCode: 500,
            headers: corsHeaders,
            body: JSON.stringify({ error: 'Server configuration error' })
        };
    }

    try {
        const minConfidence = parseFloat(event.queryStringParameters?.minConfidence ?? '0.5');

        const scanResult = await docClient.send(new ScanCommand({
            TableName: eventsTable,
            FilterExpression: 'bootsConfidence >= :min',
            ExpressionAttributeValues: { ':min': minConfidence },
        }));

        const items = scanResult.Items ?? [];

        // Sort by timestamp descending (newest first)
        items.sort((a, b) => (b.timestamp as string).localeCompare(a.timestamp as string));

        // Generate signed URLs for each image
        const events = await Promise.all(items.map(async (item) => {
            let imageUrl: string | null = null;
            try {
                const command = new GetObjectCommand({
                    Bucket: imagesBucket,
                    Key: item.imageName as string,
                });
                imageUrl = await getSignedUrl(s3Client, command, { expiresIn: SIGNED_URL_EXPIRY_SECONDS });
            } catch (err) {
                logger.warn('Failed to generate signed URL for image', { imageName: item.imageName, error: err });
            }

            return {
                id: item.id,
                timestamp: item.timestamp,
                imageName: item.imageName,
                bootsConfidence: item.bootsConfidence,
                imageUrl,
            };
        }));

        return {
            statusCode: 200,
            headers: corsHeaders,
            body: JSON.stringify(events)
        };

    } catch (error: any) {
        logger.error('Error querying events', { error });
        return {
            statusCode: 500,
            headers: corsHeaders,
            body: JSON.stringify({ error: 'Failed to query events' })
        };
    }
}
