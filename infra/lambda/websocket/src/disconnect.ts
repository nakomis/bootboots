import { APIGatewayProxyHandler, APIGatewayProxyResult } from 'aws-lambda';
import { DynamoDBClient, DeleteItemCommand } from '@aws-sdk/client-dynamodb';

const dynamoDb = new DynamoDBClient({});
const CONNECTIONS_TABLE = process.env.CONNECTIONS_TABLE!;

export const handler: APIGatewayProxyHandler = async (event): Promise<APIGatewayProxyResult> => {
    const connectionId = event.requestContext.connectionId;

    console.log(`WebSocket disconnect: ${connectionId}`);

    try {
        await dynamoDb.send(new DeleteItemCommand({
            TableName: CONNECTIONS_TABLE,
            Key: {
                connectionId: { S: connectionId! },
            },
        }));

        console.log(`Connection removed: ${connectionId}`);

        return {
            statusCode: 200,
            body: 'Disconnected',
        };
    } catch (error) {
        console.error('Error removing connection:', error);
        return {
            statusCode: 500,
            body: 'Failed to disconnect',
        };
    }
};
