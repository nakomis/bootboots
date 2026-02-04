import { IoTClient, ListThingsCommand, DescribeThingCommand } from '@aws-sdk/client-iot';
import { APIGatewayProxyEvent, APIGatewayProxyResult } from 'aws-lambda';

const iotClient = new IoTClient({});

interface DeviceInfo {
    thingName: string;
    thingArn?: string;
    project?: string;
    deviceType?: string;
    capabilities?: string[];
}

export const handler = async (event: APIGatewayProxyEvent): Promise<APIGatewayProxyResult> => {
    try {
        // Parse query parameters
        const project = event.queryStringParameters?.project;

        // List all IoT things
        const listCommand = new ListThingsCommand({
            maxResults: 100,
        });

        const listResult = await iotClient.send(listCommand);
        const things = listResult.things || [];

        // Get detailed info for each thing
        const devices: DeviceInfo[] = [];

        for (const thing of things) {
            if (!thing.thingName) continue;

            // Get full thing details including attributes
            const describeCommand = new DescribeThingCommand({
                thingName: thing.thingName,
            });

            try {
                const thingDetails = await iotClient.send(describeCommand);
                const attributes = thingDetails.attributes || {};

                // Filter by project if specified
                if (project && attributes.project !== project) {
                    continue;
                }

                const device: DeviceInfo = {
                    thingName: thing.thingName,
                    thingArn: thing.thingArn,
                    project: attributes.project,
                    deviceType: attributes.deviceType,
                    capabilities: attributes.capabilities?.split(',') || [],
                };

                devices.push(device);
            } catch (err) {
                console.error(`Error describing thing ${thing.thingName}:`, err);
                // Continue with other things even if one fails
            }
        }

        return {
            statusCode: 200,
            headers: {
                'Content-Type': 'application/json',
                'Access-Control-Allow-Origin': '*',
                'Access-Control-Allow-Headers': 'Content-Type,Authorization',
            },
            body: JSON.stringify({
                devices,
                count: devices.length,
            }),
        };
    } catch (error) {
        console.error('Error listing devices:', error);
        return {
            statusCode: 500,
            headers: {
                'Content-Type': 'application/json',
                'Access-Control-Allow-Origin': '*',
            },
            body: JSON.stringify({
                error: 'Failed to list devices',
                message: error instanceof Error ? error.message : 'Unknown error',
            }),
        };
    }
};
