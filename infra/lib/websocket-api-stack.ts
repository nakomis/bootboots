import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as apigatewayv2 from 'aws-cdk-lib/aws-apigatewayv2';
import * as apigatewayv2_integrations from 'aws-cdk-lib/aws-apigatewayv2-integrations';
import * as lambda from 'aws-cdk-lib/aws-lambda';
import * as dynamodb from 'aws-cdk-lib/aws-dynamodb';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as logs from 'aws-cdk-lib/aws-logs';
import * as iot from 'aws-cdk-lib/aws-iot';
import { NodejsFunction } from 'aws-cdk-lib/aws-lambda-nodejs';

export class WebSocketApiStack extends cdk.Stack {
    public readonly webSocketApi: apigatewayv2.WebSocketApi;
    public readonly webSocketStage: apigatewayv2.WebSocketStage;

    constructor(scope: Construct, id: string, props?: cdk.StackProps) {
        super(scope, id, props);

        // DynamoDB table for storing WebSocket connection IDs
        // Tracks which connectionId is subscribed to which device
        const connectionsTable = new dynamodb.Table(this, 'WebSocketConnections', {
            tableName: 'BootBootsWebSocketConnections',
            partitionKey: { name: 'connectionId', type: dynamodb.AttributeType.STRING },
            billingMode: dynamodb.BillingMode.PAY_PER_REQUEST,
            removalPolicy: cdk.RemovalPolicy.DESTROY,
            timeToLiveAttribute: 'ttl',
        });

        // GSI to look up connections by device
        connectionsTable.addGlobalSecondaryIndex({
            indexName: 'deviceIndex',
            partitionKey: { name: 'deviceId', type: dynamodb.AttributeType.STRING },
            projectionType: dynamodb.ProjectionType.ALL,
        });

        // Log group for WebSocket Lambdas
        const wsLogGroup = new logs.LogGroup(this, 'WebSocketLambdaLogGroup', {
            logGroupName: '/aws/lambda/BootBootsWebSocket',
            retention: logs.RetentionDays.ONE_MONTH,
            removalPolicy: cdk.RemovalPolicy.DESTROY,
        });

        // Lambda for $connect route
        const connectHandler = new NodejsFunction(this, 'ConnectHandler', {
            functionName: 'BootBootsWsConnect',
            runtime: lambda.Runtime.NODEJS_22_X,
            entry: `${__dirname}/../lambda/websocket/src/connect.ts`,
            logGroup: wsLogGroup,
            timeout: cdk.Duration.seconds(10),
            memorySize: 128,
            environment: {
                CONNECTIONS_TABLE: connectionsTable.tableName,
            },
            bundling: {
                minify: true,
                sourceMap: false,
                target: 'node22',
            },
        });
        connectionsTable.grantWriteData(connectHandler);

        // Lambda for $disconnect route
        const disconnectHandler = new NodejsFunction(this, 'DisconnectHandler', {
            functionName: 'BootBootsWsDisconnect',
            runtime: lambda.Runtime.NODEJS_22_X,
            entry: `${__dirname}/../lambda/websocket/src/disconnect.ts`,
            logGroup: wsLogGroup,
            timeout: cdk.Duration.seconds(10),
            memorySize: 128,
            environment: {
                CONNECTIONS_TABLE: connectionsTable.tableName,
            },
            bundling: {
                minify: true,
                sourceMap: false,
                target: 'node22',
            },
        });
        connectionsTable.grantWriteData(disconnectHandler);

        // Lambda for sendCommand route - publishes to IoT MQTT
        const sendCommandHandler = new NodejsFunction(this, 'SendCommandHandler', {
            functionName: 'BootBootsWsSendCommand',
            runtime: lambda.Runtime.NODEJS_22_X,
            entry: `${__dirname}/../lambda/websocket/src/sendCommand.ts`,
            logGroup: wsLogGroup,
            timeout: cdk.Duration.seconds(15),
            memorySize: 256,
            environment: {
                CONNECTIONS_TABLE: connectionsTable.tableName,
                IOT_ENDPOINT: '', // Will be set after deployment or via parameter
            },
            bundling: {
                minify: true,
                sourceMap: false,
                target: 'node22',
            },
        });
        connectionsTable.grantReadWriteData(sendCommandHandler);

        // Grant IoT publish permissions
        sendCommandHandler.addToRolePolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['iot:Publish'],
            resources: [`arn:aws:iot:${this.region}:${this.account}:topic/catcam/*`],
        }));

        // Grant permission to describe IoT endpoint
        sendCommandHandler.addToRolePolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['iot:DescribeEndpoint'],
            resources: ['*'],
        }));

        // Create WebSocket API
        this.webSocketApi = new apigatewayv2.WebSocketApi(this, 'BootBootsWebSocketApi', {
            apiName: 'BootBootsWebSocket',
            description: 'WebSocket API for BootBoots device communication',
            connectRouteOptions: {
                integration: new apigatewayv2_integrations.WebSocketLambdaIntegration(
                    'ConnectIntegration',
                    connectHandler
                ),
            },
            disconnectRouteOptions: {
                integration: new apigatewayv2_integrations.WebSocketLambdaIntegration(
                    'DisconnectIntegration',
                    disconnectHandler
                ),
            },
        });

        // Add sendCommand route
        this.webSocketApi.addRoute('sendCommand', {
            integration: new apigatewayv2_integrations.WebSocketLambdaIntegration(
                'SendCommandIntegration',
                sendCommandHandler
            ),
        });

        // Create WebSocket stage
        this.webSocketStage = new apigatewayv2.WebSocketStage(this, 'BootBootsWebSocketStage', {
            webSocketApi: this.webSocketApi,
            stageName: 'prod',
            autoDeploy: true,
        });

        // Lambda for routing IoT responses back to WebSocket clients
        const responseRouterHandler = new NodejsFunction(this, 'ResponseRouterHandler', {
            functionName: 'BootBootsWsResponseRouter',
            runtime: lambda.Runtime.NODEJS_22_X,
            entry: `${__dirname}/../lambda/websocket/src/responseRouter.ts`,
            logGroup: wsLogGroup,
            timeout: cdk.Duration.seconds(15),
            memorySize: 256,
            environment: {
                CONNECTIONS_TABLE: connectionsTable.tableName,
                WEBSOCKET_ENDPOINT: this.webSocketStage.callbackUrl,
            },
            bundling: {
                minify: true,
                sourceMap: false,
                target: 'node22',
            },
        });
        connectionsTable.grantReadData(responseRouterHandler);

        // Grant permission to post to WebSocket connections
        responseRouterHandler.addToRolePolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['execute-api:ManageConnections'],
            resources: [
                `arn:aws:execute-api:${this.region}:${this.account}:${this.webSocketApi.apiId}/${this.webSocketStage.stageName}/POST/@connections/*`,
            ],
        }));

        // IoT Rule to trigger Lambda when device publishes responses
        const iotRule = new iot.CfnTopicRule(this, 'DeviceResponseRule', {
            ruleName: 'BootBootsDeviceResponseRule',
            topicRulePayload: {
                sql: "SELECT *, topic(2) as deviceId FROM 'catcam/+/responses'",
                actions: [
                    {
                        lambda: {
                            functionArn: responseRouterHandler.functionArn,
                        },
                    },
                ],
                ruleDisabled: false,
            },
        });

        // Grant IoT permission to invoke the Lambda
        responseRouterHandler.addPermission('AllowIoTInvoke', {
            principal: new iam.ServicePrincipal('iot.amazonaws.com'),
            sourceArn: `arn:aws:iot:${this.region}:${this.account}:rule/${iotRule.ruleName}`,
        });

        // Outputs
        new cdk.CfnOutput(this, 'WebSocketApiEndpoint', {
            value: this.webSocketStage.url,
            description: 'WebSocket API endpoint URL',
        });

        new cdk.CfnOutput(this, 'WebSocketApiId', {
            value: this.webSocketApi.apiId,
            description: 'WebSocket API ID',
        });

        new cdk.CfnOutput(this, 'ConnectionsTableName', {
            value: connectionsTable.tableName,
            description: 'DynamoDB table for WebSocket connections',
        });
    }
}
