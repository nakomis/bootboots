import * as fs from 'fs';
import * as path from 'path';
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
        // One Dark style, settings copied verbatim from nakostat's working branding
        // (Cognito rejects unknown keys with a 400 UnknownProperty), plus the
        // BootBoots logo above the sign-in form. The logo is embedded as base64 in
        // the template, so keep it small: CloudFormation templates are capped at 1 MB.
        const formLogo = fs.readFileSync(path.join(__dirname, '..', 'assets', 'bootboots-logo.png'));
        const branding = new cognito.CfnManagedLoginBranding(this, 'ManagedLoginBranding', {
            userPoolId: userPool.userPoolId,
            clientId: userPoolClient.userPoolClientId,
            useCognitoProvidedValues: false,
            settings: {
                components: {
                    pageBackground: {
                        image: { enabled: false },
                        darkMode: { color: '282c34ff' },
                    },
                    pageHeader: {
                        backgroundImage: { enabled: false },
                        logo: { location: 'START', enabled: false },
                        darkMode: { background: { color: '21252bff' }, borderColor: '3e4451ff' },
                    },
                    pageFooter: {
                        backgroundImage: { enabled: false },
                        logo: { location: 'START', enabled: false },
                        darkMode: { background: { color: '21252bff' }, borderColor: '3e4451ff' },
                    },
                    form: {
                        borderRadius: 8,
                        backgroundImage: { enabled: false },
                        logo: { location: 'CENTER', position: 'TOP', enabled: true, formInclusion: 'IN' },
                        darkMode: { backgroundColor: '2c313aff', borderColor: '3e4451ff' },
                    },
                    pageText: {
                        darkMode: {
                            bodyColor: 'abb2bfff',
                            headingColor: 'ffffffff',
                            descriptionColor: '5c6370ff',
                        },
                    },
                    primaryButton: {
                        darkMode: {
                            defaults: { backgroundColor: '2563ebff', textColor: 'ffffffff' },
                            hover: { backgroundColor: '1d4ed8ff', textColor: 'ffffffff' },
                            active: { backgroundColor: '1e40afff', textColor: 'ffffffff' },
                            disabled: { backgroundColor: '2c313aff', borderColor: '3e4451ff' },
                        },
                    },
                    secondaryButton: {
                        darkMode: {
                            defaults: {
                                backgroundColor: '2c313aff',
                                borderColor: '3e4451ff',
                                textColor: 'abb2bfff',
                            },
                            hover: {
                                backgroundColor: '353b45ff',
                                borderColor: '528bffff',
                                textColor: 'ffffffff',
                            },
                            active: {
                                backgroundColor: '21252bff',
                                borderColor: '3e4451ff',
                                textColor: 'ffffffff',
                            },
                        },
                    },
                    alert: {
                        borderRadius: 4,
                        darkMode: { error: { backgroundColor: '3a1515ff', borderColor: 'e06c75ff' } },
                    },
                    idpButton: {
                        standard: {
                            darkMode: {
                                defaults: {
                                    backgroundColor: '2c313aff',
                                    borderColor: '3e4451ff',
                                    textColor: 'abb2bfff',
                                },
                                hover: {
                                    backgroundColor: '353b45ff',
                                    borderColor: '528bffff',
                                    textColor: 'ffffffff',
                                },
                                active: {
                                    backgroundColor: '21252bff',
                                    borderColor: '3e4451ff',
                                    textColor: 'ffffffff',
                                },
                            },
                        },
                        custom: {},
                    },
                    phoneNumberSelector: { displayType: 'TEXT' },
                    favicon: { enabledTypes: ['ICO', 'SVG'] },
                },
                componentClasses: {
                    input: {
                        borderRadius: 6,
                        darkMode: {
                            defaults: { backgroundColor: '21252bff', borderColor: '3e4451ff' },
                            placeholderColor: '5c6370ff',
                        },
                    },
                    inputLabel: {
                        darkMode: { textColor: 'abb2bfff' },
                    },
                    inputDescription: {
                        darkMode: { textColor: '5c6370ff' },
                    },
                    link: {
                        darkMode: { defaults: { textColor: '61afefff' }, hover: { textColor: '528bffff' } },
                    },
                    optionControls: {
                        darkMode: {
                            defaults: { backgroundColor: '21252bff', borderColor: '3e4451ff' },
                            selected: { backgroundColor: '2563ebff', foregroundColor: 'ffffffff' },
                        },
                    },
                    focusState: {
                        darkMode: { borderColor: '528bffff' },
                    },
                },
                categories: {
                    global: {
                        colorSchemeMode: 'DARK',
                        pageFooter: { enabled: false },
                        pageHeader: { enabled: false },
                        spacingDensity: 'REGULAR',
                    },
                },
            },
            assets: [{
                category: 'FORM_LOGO',
                colorMode: 'DARK',
                extension: 'PNG',
                bytes: formLogo.toString('base64'),
            }],
        });
        branding.node.addDependency(userPoolClient);

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
