import AWSS3
import AWSSDKIdentity
import Foundation

/// Uploads a recorded clip to the images bucket under the `iOS/` prefix using
/// the identity-pool IAM credentials.
enum ClipUploader {
    static func upload(fileURL: URL, creds: AWSCredentials) async throws -> String {
        let resolver = StaticAWSCredentialIdentityResolver(
            AWSCredentialIdentity(
                accessKey: creds.accessKey,
                secret: creds.secret,
                sessionToken: creds.sessionToken
            )
        )
        let config = try await S3Client.S3ClientConfig(
            awsCredentialIdentityResolver: resolver,
            region: AppConfig.awsRegion
        )
        let s3 = S3Client(config: config)

        let data = try Data(contentsOf: fileURL)
        let key = "iOS/\(timestamp()).mov"
        _ = try await s3.putObject(input: PutObjectInput(
            body: .data(data),
            bucket: AppConfig.imagesBucket,
            contentType: "video/quicktime",
            key: key
        ))
        return key
    }

    private static func timestamp() -> String {
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime]
        return f.string(from: Date()).replacingOccurrences(of: ":", with: "-")
    }
}
