import Foundation

/// Backend config baked in at build time via Info.plist (see project.yml).
enum AppConfig {
    static let cognitoClientID    = plist("BootBootsCognitoClientID")
    static let cognitoLoginDomain = plist("BootBootsCognitoLoginDomain")
    static let userPoolID         = plist("BootBootsUserPoolID")
    static let identityPoolID     = plist("BootBootsIdentityPoolID")
    static let awsRegion          = plist("BootBootsAWSRegion")
    static let iotEndpoint        = plist("BootBootsIoTEndpoint")
    static let triggerTopic       = plist("BootBootsTriggerTopic")
    static let imagesBucket       = plist("BootBootsImagesBucket")
    static let iotPolicyName      = plist("BootBootsIoTPolicyName")

    static let cognitoRedirectURI = "com.nakomis.bootboots://callback"

    /// Cognito user-pool provider key used in the identity-pool logins map.
    static var loginProviderKey: String {
        "cognito-idp.\(awsRegion).amazonaws.com/\(userPoolID)"
    }

    private static func plist(_ key: String) -> String {
        guard let value = Bundle.main.infoDictionary?[key] as? String, !value.isEmpty else {
            fatalError("Missing Info.plist key: \(key)")
        }
        return value
    }
}
