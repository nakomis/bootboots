import AwsIotDeviceSdkSwift
import Foundation

/// Subscribes to the spray-trigger MQTT topic over a SigV4-signed WebSocket.
/// Calls `onTrigger` with the message payload each time a trigger is published.
@MainActor
final class TriggerClient {
    private var client: Mqtt5Client?
    private let onStatus: (String) -> Void
    private let onTrigger: (String) -> Void

    init(onStatus: @escaping (String) -> Void, onTrigger: @escaping (String) -> Void) {
        self.onStatus = onStatus
        self.onTrigger = onTrigger
    }

    func connect(using creds: AWSCredentials) throws {
        IotDeviceSdk.initialize()

        let provider = try CredentialsProvider(source: .static(
            accessKey: creds.accessKey,
            secret: creds.secret,
            sessionToken: creds.sessionToken
        ))

        let builder = try Mqtt5ClientBuilder.websocketsWithDefaultAwsSigning(
            endpoint: AppConfig.iotEndpoint,
            region: AppConfig.awsRegion,
            credentialsProvider: provider
        )
        builder.withClientId("bootboots-ios-" + UUID().uuidString)
        builder.withCallbacks(
            onPublishReceived: { [weak self] data in
                let payload = data.publishPacket.payloadAsString() ?? "(empty)"
                Task { @MainActor in self?.onTrigger(payload) }
            },
            onLifecycleEventConnectionSuccess: { [weak self] _ in
                Task { @MainActor in
                    self?.onStatus("Connected")
                    await self?.subscribe()
                }
            },
            onLifecycleEventConnectionFailure: { [weak self] failure in
                Task { @MainActor in self?.onStatus("Connection failed: \(failure.crtError.message)") }
            },
            onLifecycleEventDisconnection: { [weak self] disconnect in
                Task { @MainActor in self?.onStatus("Disconnected: \(disconnect.crtError.message)") }
            },
            onLifecycleEventStopped: { [weak self] _ in
                Task { @MainActor in self?.onStatus("Stopped") }
            }
        )

        let client = try builder.build()
        self.client = client
        onStatus("Connecting…")
        try client.start()
    }

    func disconnect() {
        try? client?.stop()
        client = nil
    }

    private func subscribe() async {
        guard let client else { return }
        do {
            _ = try await client.subscribe(subscribePacket: SubscribePacket(
                subscription: Subscription(topicFilter: AppConfig.triggerTopic, qos: .atLeastOnce)
            ))
            onStatus("Listening on \(AppConfig.triggerTopic)")
        } catch {
            onStatus("Subscribe failed: \(error)")
        }
    }
}
