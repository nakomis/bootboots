import AuthenticationServices
import CryptoKit
import Foundation

/// Cognito hosted-UI auth via PKCE authorization-code flow (mirrors Recipator).
/// Exposes the id token so it can be exchanged for IAM credentials via the
/// Cognito identity pool (see `AWSCredentials`).
@MainActor
final class AuthService: NSObject, ObservableObject {
    @Published private(set) var isSignedIn = false
    @Published private(set) var displayName: String?
    @Published private(set) var lastError: String?

    private var tokens: StoredTokens?

    // MARK: - Lifecycle

    func restore() async {
        guard let stored = try? TokenStore.load() else { return }
        if stored.expiresAt > Date() {
            apply(stored)
        } else if let refreshToken = stored.refreshToken {
            await refresh(using: refreshToken)
        }
    }

    /// A valid id token, refreshing silently if needed. Used as the login for
    /// the Cognito identity pool exchange.
    func idToken() async -> String? {
        guard let t = tokens else { return nil }
        if t.expiresAt > Date().addingTimeInterval(60) { return t.idToken }
        guard let rt = t.refreshToken else { return nil }
        await refresh(using: rt)
        return tokens?.idToken
    }

    func signIn() async {
        lastError = nil
        let verifier = pkceVerifier()
        let challenge = pkceChallenge(for: verifier)

        var comps = URLComponents(string: "https://\(AppConfig.cognitoLoginDomain)/oauth2/authorize")!
        comps.queryItems = [
            URLQueryItem(name: "response_type",         value: "code"),
            URLQueryItem(name: "client_id",             value: AppConfig.cognitoClientID),
            URLQueryItem(name: "redirect_uri",          value: AppConfig.cognitoRedirectURI),
            URLQueryItem(name: "scope",                 value: "openid email profile"),
            URLQueryItem(name: "code_challenge",        value: challenge),
            URLQueryItem(name: "code_challenge_method", value: "S256"),
        ]

        do {
            let callback = try await beginSession(url: comps.url!)
            guard let code = URLComponents(url: callback, resolvingAgainstBaseURL: false)?
                    .queryItems?.first(where: { $0.name == "code" })?.value
            else { lastError = "No auth code in callback"; return }
            try await exchangeCode(code, verifier: verifier)
        } catch {
            lastError = error.localizedDescription
        }
    }

    func signOut() {
        TokenStore.clear()
        tokens = nil
        isSignedIn = false
        displayName = nil
    }

    // MARK: - Private

    private func beginSession(url: URL) async throws -> URL {
        let scheme = URL(string: AppConfig.cognitoRedirectURI)!.scheme!
        return try await withCheckedThrowingContinuation { cont in
            let session = ASWebAuthenticationSession(url: url, callbackURLScheme: scheme) { url, error in
                if let error { cont.resume(throwing: error); return }
                cont.resume(returning: url!)
            }
            session.presentationContextProvider = self
            session.prefersEphemeralWebBrowserSession = true
            session.start()
        }
    }

    private func exchangeCode(_ code: String, verifier: String) async throws {
        var req = URLRequest(url: URL(string: "https://\(AppConfig.cognitoLoginDomain)/oauth2/token")!)
        req.httpMethod = "POST"
        req.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        req.httpBody = [
            "grant_type":    "authorization_code",
            "client_id":     AppConfig.cognitoClientID,
            "code":          code,
            "redirect_uri":  AppConfig.cognitoRedirectURI,
            "code_verifier": verifier,
        ].formEncoded()
        let (data, _) = try await URLSession.shared.data(for: req)
        try applyTokenResponse(data)
    }

    private func refresh(using refreshToken: String) async {
        do {
            var req = URLRequest(url: URL(string: "https://\(AppConfig.cognitoLoginDomain)/oauth2/token")!)
            req.httpMethod = "POST"
            req.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
            req.httpBody = [
                "grant_type":    "refresh_token",
                "client_id":     AppConfig.cognitoClientID,
                "refresh_token": refreshToken,
            ].formEncoded()
            let (data, _) = try await URLSession.shared.data(for: req)
            try applyTokenResponse(data, existingRefreshToken: refreshToken)
        } catch {
            signOut()
        }
    }

    private func applyTokenResponse(_ data: Data, existingRefreshToken: String? = nil) throws {
        struct Response: Decodable {
            let access_token: String
            let id_token: String
            let refresh_token: String?
            let expires_in: Int
        }
        let r = try JSONDecoder().decode(Response.self, from: data)
        let stored = StoredTokens(
            accessToken:  r.access_token,
            idToken:      r.id_token,
            refreshToken: r.refresh_token ?? existingRefreshToken,
            expiresAt:    Date().addingTimeInterval(TimeInterval(r.expires_in))
        )
        try TokenStore.save(stored)
        apply(stored)
    }

    private func apply(_ stored: StoredTokens) {
        tokens = stored
        isSignedIn = true
        displayName = claims(from: stored.idToken)?["cognito:username"] as? String
    }

    // MARK: - PKCE helpers

    private func pkceVerifier() -> String {
        var bytes = [UInt8](repeating: 0, count: 64)
        _ = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        return Data(bytes).base64URLEncoded()
    }

    private func pkceChallenge(for verifier: String) -> String {
        Data(SHA256.hash(data: Data(verifier.utf8))).base64URLEncoded()
    }

    private func claims(from jwt: String) -> [String: Any]? {
        let parts = jwt.split(separator: ".")
        guard parts.count >= 2 else { return nil }
        var b64 = String(parts[1])
            .replacingOccurrences(of: "-", with: "+")
            .replacingOccurrences(of: "_", with: "/")
        while b64.count % 4 != 0 { b64 += "=" }
        guard let data = Data(base64Encoded: b64) else { return nil }
        return try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    }
}

extension AuthService: ASWebAuthenticationPresentationContextProviding {
    nonisolated func presentationAnchor(for session: ASWebAuthenticationSession) -> ASPresentationAnchor {
        MainActor.assumeIsolated { ASPresentationAnchor() }
    }
}

private extension Data {
    func base64URLEncoded() -> String {
        base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }
}

private extension [String: String] {
    func formEncoded() -> Data? {
        map { "\($0.key)=\($0.value.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? "")" }
            .joined(separator: "&")
            .data(using: .utf8)
    }
}
