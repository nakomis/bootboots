import { Handler } from 'aws-lambda';
import { DynamoDBClient, QueryCommand } from '@aws-sdk/client-dynamodb';
import { ApiGatewayManagementApiClient, PostToConnectionCommand, GoneException } from '@aws-sdk/client-apigatewaymanagementapi';

const dynamoDb = new DynamoDBClient({});
const CONNECTIONS_TABLE = process.env.CONNECTIONS_TABLE!;
const WEBSOCKET_ENDPOINT = process.env.WEBSOCKET_ENDPOINT!;

// IoT Rule event structure
interface IoTRuleEvent {
    deviceId: string;  // Extracted from topic by IoT Rule SQL
    [key: string]: unknown;  // Response payload from device
}

export const handler: Handler<IoTRuleEvent> = async (event) => {
    console.log('Received IoT response:', JSON.stringify(event));

    const deviceId = event.deviceId;
    if (!deviceId) {
        console.error('No deviceId in event');
        return;
    }

    // Remove deviceId from the payload we send to clients
    const { deviceId: _, ...responsePayload } = event;

    try {
        // Query connections subscribed to this device
        const queryResult = await dynamoDb.send(new QueryCommand({
            TableName: CONNECTIONS_TABLE,
            IndexName: 'deviceIndex',
            KeyConditionExpression: 'deviceId = :deviceId',
            ExpressionAttributeValues: {
                ':deviceId': { S: deviceId },
            },
        }));

        const connections = queryResult.Items || [];
        console.log(`Found ${connections.length} connections for device ${deviceId}`);

        if (connections.length === 0) {
            console.log('No active connections for this device');
            return;
        }

        // Create API Gateway Management client
        // The endpoint URL needs to be in the format: https://{api-id}.execute-api.{region}.amazonaws.com/{stage}
        const apiClient = new ApiGatewayManagementApiClient({
            endpoint: WEBSOCKET_ENDPOINT.replace('wss://', 'https://').replace('/prod', ''),
        });

        // Send response to all connected clients
        const sendPromises = connections.map(async (connection) => {
            const connectionId = connection.connectionId?.S;
            if (!connectionId) return;

            try {
                await apiClient.send(new PostToConnectionCommand({
                    ConnectionId: connectionId,
                    Data: Buffer.from(JSON.stringify({
                        type: 'deviceResponse',
                        deviceId,
                        response: responsePayload,
                    })),
                }));
                console.log(`Sent response to connection ${connectionId}`);
            } catch (error) {
                if (error instanceof GoneException) {
                    console.log(`Connection ${connectionId} is gone, should be cleaned up`);
                    // Connection is stale - it will be cleaned up by TTL or next disconnect
                } else {
                    console.error(`Error sending to ${connectionId}:`, error);
                }
            }
        });

        await Promise.all(sendPromises);
        console.log('Finished sending responses');

    } catch (error) {
        console.error('Error routing response:', error);
        throw error;
    }
};
