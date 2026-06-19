import Foundation

/// Ties the pieces together: fetch credentials, connect to the trigger topic,
/// and on each trigger record a clip and upload it to S3.
@MainActor
final class SprayMonitor: ObservableObject {
    @Published var status = "Not started"
    @Published var lastTrigger: Date?
    @Published var lastUpload: String?
    @Published var log: [String] = []
    @Published private(set) var running = false

    let recorder = VideoRecorder()
    private var trigger: TriggerClient?
    private var creds: AWSCredentials?
    private var busy = false
    private let clipDuration: TimeInterval = 5

    func start(idToken: String) async {
        guard !running else { return }
        running = true
        status = "Starting…"

        await recorder.configure()
        append(recorder.isReady
               ? "Camera ready"
               : "No camera (simulator?) — triggers still received, no clip recorded")

        do {
            let session = try await CognitoCredentials.fetch(idToken: idToken)
            let creds = session.credentials
            self.creds = creds
            append("Obtained AWS credentials")

            // AWS IoT authorises MQTT per Cognito identity — attach the policy first.
            try await IoTPolicyAttacher.attach(
                policyName: AppConfig.iotPolicyName,
                identityId: session.identityId,
                creds: creds
            )
            append("IoT policy attached to identity")

            let trigger = TriggerClient(
                onStatus: { [weak self] s in self?.status = s; self?.append(s) },
                onTrigger: { [weak self] payload in self?.handleTrigger(payload) }
            )
            try trigger.connect(using: creds)
            self.trigger = trigger
        } catch {
            append("Start failed: \(error)")
            status = "Error"
            running = false
        }
    }

    func stop() {
        trigger?.disconnect()
        trigger = nil
        running = false
        status = "Stopped"
        append("Stopped")
    }

    /// Bench helper — simulates a trigger without a real MQTT message.
    func fireTestTrigger() {
        handleTrigger("{\"reason\":\"local test\"}")
    }

    private func handleTrigger(_ payload: String) {
        lastTrigger = Date()
        append("Trigger received: \(payload)")
        guard !busy else { append("Busy — ignoring trigger"); return }
        guard let creds else { append("No credentials yet"); return }
        guard recorder.isReady else { append("No camera — skipping recording"); return }

        busy = true
        Task {
            defer { busy = false }
            do {
                status = "Recording"
                let url = try await recorder.record(duration: clipDuration)
                status = "Uploading"
                let key = try await ClipUploader.upload(fileURL: url, creds: creds)
                lastUpload = key
                append("Uploaded \(key)")
                try? FileManager.default.removeItem(at: url)
                status = "Listening on \(AppConfig.triggerTopic)"
            } catch {
                append("Record/upload failed: \(error)")
                status = "Listening on \(AppConfig.triggerTopic)"
            }
        }
    }

    private func append(_ line: String) {
        let ts = DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium)
        log.insert("[\(ts)] \(line)", at: 0)
        if log.count > 50 { log.removeLast() }
    }
}
