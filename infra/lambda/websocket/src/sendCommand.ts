import { APIGatewayProxyHandler, APIGatewayProxyResult } from 'aws-lambda';
import { DynamoDBClient, UpdateItemCommand } from '@aws-sdk/client-dynamodb';
import { IoTDataPlaneClient, PublishCommand } from '@aws-sdk/client-iot-data-plane';
import { IoTClient, DescribeEndpointCommand } from '@aws-sdk/client-iot';

const dynamoDb = new DynamoDBClient({});
const iotClient = new IoTClient({});
const CONNECTIONS_TABLE = process.env.CONNECTIONS_TABLE!;

// Cache the IoT endpoint
let iotEndpoint: string | null = null;

async function getIotEndpoint(): Promise<string> {
    if (iotEndpoint) return iotEndpoint;

    const response = await iotClient.send(new DescribeEndpointCommand({
        endpointType: 'iot:Data-ATS',
    }));

    iotEndpoint = response.endpointAddress!;
    return iotEndpoint;
}

interface CommandMessage {
    action: 'sendCommand';
    deviceId: string;
    command: {
        command: string;
        [key: string]: unknown;
    };
}

export const handler: APIGatewayProxyHandler = async (event): Promise<APIGatewayProxyResult> => {
    const connectionId = event.requestContext.connectionId;

    console.log(`WebSocket sendCommand from: ${connectionId}`);
    console.log('Body:', event.body);

    try {
        if (!event.body) {
            return {
                statusCode: 400,
                body: JSON.stringify({ error: 'No message body' }),
            };
        }

        const message: CommandMessage = JSON.parse(event.body);

        if (!message.deviceId || !message.command) {
            return {
                statusCode: 400,
                body: JSON.stringify({ error: 'Missing deviceId or command' }),
            };
        }

        // Update connection to track which device it's connected to
        const ttl = Math.floor(Date.now() / 1000) + 86400;
        await dynamoDb.send(new UpdateItemCommand({
            TableName: CONNECTIONS_TABLE,
            Key: {
                connectionId: { S: connectionId! },
            },
            UpdateExpression: 'SET deviceId = :deviceId, ttl = :ttl, lastCommand = :lastCommand',
            ExpressionAttributeValues: {
                ':deviceId': { S: message.deviceId },
                ':ttl': { N: ttl.toString() },
                ':lastCommand': { S: new Date().toISOString() },
            },
        }));

        // Get IoT endpoint and create data plane client
        const endpoint = await getIotEndpoint();
        const iotDataClient = new IoTDataPlaneClient({
            endpoint: `https://${endpoint}`,
        });

        // Publish command to IoT topic
        const topic = `catcam/${message.deviceId}/commands`;
        const payload = JSON.stringify(message.command);

        console.log(`Publishing to ${topic}: ${payload}`);

        await iotDataClient.send(new PublishCommand({
            topic,
            payload: Buffer.from(payload),
            qos: 1,
        }));

        console.log(`Command published successfully to ${topic}`);

        return {
            statusCode: 200,
            body: JSON.stringify({
                success: true,
                message: 'Command sent',
                topic,
            }),
        };
    } catch (error) {
        console.error('Error sending command:', error);
        return {
            statusCode: 500,
            body: JSON.stringify({
                error: 'Failed to send command',
                message: error instanceof Error ? error.message : 'Unknown error',
            }),
        };
    }
};
