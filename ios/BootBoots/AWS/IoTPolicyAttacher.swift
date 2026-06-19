import AWSIoT
import AWSSDKIdentity
import Foundation

/// AWS IoT authorises MQTT for Cognito identities via an IoT policy attached to
/// the *identity* (not the IAM role). The app attaches the policy to its own
/// identity on launch — idempotent, so it's safe to call every time and it
/// covers fresh logins / reinstalls (which get a new identity id).
enum IoTPolicyAttacher {
    static func attach(policyName: String, identityId: String, creds: AWSCredentials) async throws {
        let resolver = StaticAWSCredentialIdentityResolver(
            AWSCredentialIdentity(
                accessKey: creds.accessKey,
                secret: creds.secret,
                sessionToken: creds.sessionToken
            )
        )
        let config = try await IoTClient.IoTClientConfig(
            awsCredentialIdentityResolver: resolver,
            region: AppConfig.awsRegion
        )
        let iot = IoTClient(config: config)
        _ = try await iot.attachPolicy(input: AttachPolicyInput(
            policyName: policyName,
            target: identityId
        ))
    }
}
