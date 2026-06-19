import AWSCognitoIdentity
import Foundation

/// Temporary IAM credentials obtained from the Cognito identity pool.
struct AWSCredentials {
    let accessKey: String
    let secret: String
    let sessionToken: String
    let expiration: Date?
}

/// The result of a Cognito identity-pool exchange: IAM credentials plus the
/// identity id (needed to attach the IoT policy to this identity).
struct CognitoSession {
    let credentials: AWSCredentials
    let identityId: String
}

/// Exchanges a Cognito user-pool id token for temporary IAM credentials via the
/// identity pool (authenticated flow). Those credentials sign the IoT WebSocket
/// connection and the S3 upload.
enum CognitoCredentials {
    static func fetch(idToken: String) async throws -> CognitoSession {
        let client = try CognitoIdentityClient(region: AppConfig.awsRegion)
        let logins = [AppConfig.loginProviderKey: idToken]

        let idOut = try await client.getId(input: GetIdInput(
            identityPoolId: AppConfig.identityPoolID,
            logins: logins
        ))
        guard let identityId = idOut.identityId else {
            throw CredentialsError.noIdentityId
        }

        let credsOut = try await client.getCredentialsForIdentity(input: GetCredentialsForIdentityInput(
            identityId: identityId,
            logins: logins
        ))
        guard let c = credsOut.credentials,
              let accessKey = c.accessKeyId,
              let secret = c.secretKey,
              let sessionToken = c.sessionToken
        else {
            throw CredentialsError.missingCredentials
        }

        return CognitoSession(
            credentials: AWSCredentials(
                accessKey: accessKey,
                secret: secret,
                sessionToken: sessionToken,
                expiration: c.expiration
            ),
            identityId: identityId
        )
    }

    enum CredentialsError: Error {
        case noIdentityId
        case missingCredentials
    }
}
