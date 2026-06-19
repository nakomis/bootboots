import * as cdk from 'aws-cdk-lib';
import * as cognito from 'aws-cdk-lib/aws-cognito';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as iot from 'aws-cdk-lib/aws-iot';
import * as ssm from 'aws-cdk-lib/aws-ssm';
import { Construct } from 'constructs';

/**
 * iOS companion app infrastructure.
 *
 * An old iPhone records a short video clip whenever the spray deterrent fires.
 * The trigger reaches the phone as an MQTT-over-WebSocket message on
 * `bootboots/ios/trigger` (published by the infer Lambda — wired up separately).
 *
 * Auth: the app signs in via the shared Cognito user pool (PKCE, same pool as
 * Recipator) and exchanges the resulting JWT through a Cognito **identity pool**
 * for temporary IAM credentials. Those credentials are used to SigV4-sign the
 * IoT WebSocket connection and to PUT the recorded clip into the existing images
 * bucket under the `iOS/` prefix.
 */
export class IosCompanionStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props?: cdk.StackProps) {
        super(scope, id, props);

        const region = this.region;
        const account = this.account;

        // The bucket the ESP32 already uploads to. iOS clips go under iOS/.
        const imagesBucketName = `bootboots-images-${account}-${region}`;

        // MQTT topic the infer Lambda publishes a trigger to on Boots-detect.
        const triggerTopic = 'bootboots/ios/trigger';

        // ── Shared Cognito user pool (same one Recipator federates) ──────────────
        const userPoolId = ssm.StringParameter.valueForStringParameter(
            this, '/nakomis-infra/sandbox/cognito/user-pool-id',
        );
        const userPool = cognito.UserPool.fromUserPoolId(this, 'SharedPool', userPoolId);

        // App client. iOS uses PKCE (no client secret).
        const userPoolClient = new cognito.UserPoolClient(this, 'BootBootsIosClient', {
            userPoolClientName: 'bootboots-ios-sandbox',
            userPool,
            authFlows: { userSrp: true },
            generateSecret: false,
            oAuth: {
                flows: { authorizationCodeGrant: true },
                scopes: [cognito.OAuthScope.OPENID, cognito.OAuthScope.EMAIL, cognito.OAuthScope.PROFILE],
                callbackUrls: ['com.nakomis.bootboots://callback'],
                logoutUrls: ['com.nakomis.bootboots://logout'],
            },
        });

        // Managed Login v2 requires an explicit branding resource per client,
        // otherwise the hosted UI returns "Login pages unavailable".
        new cognito.CfnManagedLoginBranding(this, 'ManagedLoginBranding', {
            userPoolId: userPool.userPoolId,
            clientId: userPoolClient.userPoolClientId,
            useCognitoProvidedValues: true,
        });

        // ── Identity pool: federates the user pool → temporary IAM credentials ───
        const identityPool = new cognito.CfnIdentityPool(this, 'BootBootsIosIdentityPool', {
            identityPoolName: 'bootboots_ios_sandbox',
            allowUnauthenticatedIdentities: false,
            cognitoIdentityProviders: [{
                clientId: userPoolClient.userPoolClientId,
                providerName: `cognito-idp.${region}.amazonaws.com/${userPoolId}`,
                serverSideTokenCheck: true,
            }],
        });

        // Role assumed by authenticated identities from this pool.
        const authenticatedRole = new iam.Role(this, 'BootBootsIosAuthRole', {
            roleName: 'bootboots-ios-authenticated',
            assumedBy: new iam.FederatedPrincipal(
                'cognito-identity.amazonaws.com',
                {
                    StringEquals: { 'cognito-identity.amazonaws.com:aud': identityPool.ref },
                    'ForAnyValue:StringLike': { 'cognito-identity.amazonaws.com:amr': 'authenticated' },
                },
                'sts:AssumeRoleWithWebIdentity',
            ),
            description: 'Authenticated BootBoots iOS app: IoT trigger subscribe + S3 clip upload',
        });

        // AWS IoT authorises MQTT for Cognito identities via an *IoT policy attached to
        // the identity*, NOT the IAM role policy. So the connect/subscribe/receive
        // permissions live in this IoT policy; the app attaches it to its own identity
        // on launch (iot:AttachPolicy, granted below).
        const iotPolicyName = 'bootboots-ios-trigger';
        new iot.CfnPolicy(this, 'BootBootsIosIotPolicy', {
            policyName: iotPolicyName,
            policyDocument: {
                Version: '2012-10-17',
                Statement: [
                    {
                        Effect: 'Allow',
                        Action: 'iot:Connect',
                        Resource: `arn:aws:iot:${region}:${account}:client/bootboots-ios-*`,
                    },
                    {
                        Effect: 'Allow',
                        Action: 'iot:Subscribe',
                        Resource: `arn:aws:iot:${region}:${account}:topicfilter/${triggerTopic}`,
                    },
                    {
                        Effect: 'Allow',
                        Action: 'iot:Receive',
                        Resource: `arn:aws:iot:${region}:${account}:topic/${triggerTopic}`,
                    },
                ],
            },
        });

        // Let the app attach the IoT policy to its Cognito identity (idempotent, on launch).
        authenticatedRole.addToPolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['iot:AttachPolicy'],
            resources: [`arn:aws:iot:${region}:${account}:policy/${iotPolicyName}`],
        }));

        // Upload recorded clips to the iOS/ prefix only.
        authenticatedRole.addToPolicy(new iam.PolicyStatement({
            effect: iam.Effect.ALLOW,
            actions: ['s3:PutObject'],
            resources: [`arn:aws:s3:::${imagesBucketName}/iOS/*`],
        }));

        new cognito.CfnIdentityPoolRoleAttachment(this, 'BootBootsIosRoleAttachment', {
            identityPoolId: identityPool.ref,
            roles: { authenticated: authenticatedRole.roleArn },
        });

        // ── Outputs (baked into the iOS app's build config) ──────────────────────
        new cdk.CfnOutput(this, 'UserPoolClientId', {
            value: userPoolClient.userPoolClientId,
            description: 'Cognito app client id for the BootBoots iOS app',
        });
        new cdk.CfnOutput(this, 'IdentityPoolId', {
            value: identityPool.ref,
            description: 'Cognito identity pool id (JWT -> IAM credentials)',
        });
        new cdk.CfnOutput(this, 'TriggerTopic', {
            value: triggerTopic,
            description: 'MQTT topic the app subscribes to for spray triggers',
        });
        new cdk.CfnOutput(this, 'IotPolicyName', {
            value: iotPolicyName,
            description: 'IoT policy the app self-attaches to its Cognito identity',
        });
    }
}
