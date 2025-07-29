import type { LambdaInterface } from '@aws-lambda-powertools/commons/types';
import { Context } from 'aws-lambda';
import { Logger } from '@aws-lambda-powertools/logger';
import { DynamoDBClient, PutItemCommand } from '@aws-sdk/client-dynamodb';
import { IoTDataPlaneClient, PublishCommand } from "@aws-sdk/client-iot-data-plane";

const logger = new Logger();

type MushroomCommand = {
    deviceId: string;
    commandType: string;
    action: string;
};

class Lambda implements LambdaInterface {
    @logger.injectLambdaContext()
    async handler(_event: any, _context: Context) {
        logger.info('Command Processor Lambda function invoked', {
            event: _event,
            context: _context,
        });
        const body: MushroomCommand = JSON.parse(_event?.body || '{}');
        const ddbClient = new DynamoDBClient({});
        const tableName = 'MushroomCommands';

        // Calculate TTL as 30 days from now (in seconds since epoch)
        const ttlSeconds = Math.floor(Date.now() / 1000) + 30 * 24 * 60 * 60;

        await ddbClient.send(
            new PutItemCommand({
                TableName: tableName,
                Item: {
                    deviceId: { S: body.deviceId },
                    timestamp: { S: new Date().toISOString() },
                    commandType: { S: body.commandType },
                    action: { S: body.action },
                    ttl: { N: ttlSeconds.toString() } // 30 days TTL
                },
            })
        );

        const iotClient = new IoTDataPlaneClient();
        const topic = "MushroomThing/commands";
        const message = {
            deviceId: body.deviceId,
            timestamp: new Date().toISOString(),
            commandType: body.commandType,
            action: body.action,
        };

        try {
            logger.info('Publishing command to IoT topic', {
                topic: topic,
                message: message,
            });
            await iotClient.send(
                new PublishCommand({
                    topic,
                    payload: Buffer.from(JSON.stringify(message)),
                    qos: 0
                })
            );
        } catch (error) {
            logger.error('Failed to publish command to IoT topic', {
                error: error instanceof Error ? error.message : String(error),
                topic: topic,
                message: message,
            });
        }

        return {
            statusCode: 200,
            body: JSON.stringify({
                message: 'Hello from the command processor Lambda function!',
                commandType: body.commandType,
                action: body.action,
            }),
        };
    }
}
const lambda = new Lambda();
export const handler = lambda.handler.bind(lambda);