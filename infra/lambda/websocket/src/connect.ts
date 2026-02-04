import { APIGatewayProxyHandler, APIGatewayProxyResult } from 'aws-lambda';
import { DynamoDBClient, PutItemCommand } from '@aws-sdk/client-dynamodb';

const dynamoDb = new DynamoDBClient({});
const CONNECTIONS_TABLE = process.env.CONNECTIONS_TABLE!;

export const handler: APIGatewayProxyHandler = async (event): Promise<APIGatewayProxyResult> => {
    const connectionId = event.requestContext.connectionId;

    console.log(`WebSocket connect: ${connectionId}`);

    try {
        // Store connection with TTL of 24 hours
        const ttl = Math.floor(Date.now() / 1000) + 86400;

        await dynamoDb.send(new PutItemCommand({
            TableName: CONNECTIONS_TABLE,
            Item: {
                connectionId: { S: connectionId! },
                connectedAt: { S: new Date().toISOString() },
                ttl: { N: ttl.toString() },
            },
        }));

        console.log(`Connection stored: ${connectionId}`);

        return {
            statusCode: 200,
            body: 'Connected',
        };
    } catch (error) {
        console.error('Error storing connection:', error);
        return {
            statusCode: 500,
            body: 'Failed to connect',
        };
    }
};
