import AVFoundation
import Foundation

/// Wraps an AVCaptureSession to record short clips on demand.
/// The camera only exists on a real device — on the simulator `configure()`
/// returns false and the rest of the pipeline can still be exercised.
@MainActor
final class VideoRecorder: NSObject, ObservableObject {
    let session = AVCaptureSession()
    private let movieOutput = AVCaptureMovieFileOutput()
    private let sessionQueue = DispatchQueue(label: "com.nakomis.bootboots.camera")
    private var finishContinuation: CheckedContinuation<URL, Error>?

    @Published private(set) var isReady = false

    func configure() async {
        let camGranted = await AVCaptureDevice.requestAccess(for: .video)
        let micGranted = await AVCaptureDevice.requestAccess(for: .audio)
        guard camGranted else { isReady = false; return }

        session.beginConfiguration()
        session.sessionPreset = .high

        if let videoDevice = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back),
           let videoInput = try? AVCaptureDeviceInput(device: videoDevice),
           session.canAddInput(videoInput) {
            session.addInput(videoInput)
        }
        if micGranted,
           let audioDevice = AVCaptureDevice.default(for: .audio),
           let audioInput = try? AVCaptureDeviceInput(device: audioDevice),
           session.canAddInput(audioInput) {
            session.addInput(audioInput)
        }
        if session.canAddOutput(movieOutput) {
            session.addOutput(movieOutput)
        }
        session.commitConfiguration()

        isReady = movieOutput.connection(with: .video) != nil
        if isReady { startRunning() }
    }

    private func startRunning() {
        sessionQueue.async { [session] in
            if !session.isRunning { session.startRunning() }
        }
    }

    /// Records a clip of the given duration and returns the file URL.
    func record(duration: TimeInterval) async throws -> URL {
        guard isReady else { throw RecorderError.noCamera }
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("bootboots-\(UUID().uuidString).mov")
        movieOutput.maxRecordedDuration = CMTime(seconds: duration, preferredTimescale: 600)
        return try await withCheckedThrowingContinuation { cont in
            finishContinuation = cont
            movieOutput.startRecording(to: url, recordingDelegate: self)
        }
    }

    enum RecorderError: Error { case noCamera }
}

extension VideoRecorder: AVCaptureFileOutputRecordingDelegate {
    nonisolated func fileOutput(
        _ output: AVCaptureFileOutput,
        didFinishRecordingTo outputFileURL: URL,
        from connections: [AVCaptureConnection],
        error: Error?
    ) {
        Task { @MainActor in
            // A maxRecordedDuration stop reports a non-fatal error but still produces a
            // valid file, so treat the file as success unless it's genuinely missing.
            if FileManager.default.fileExists(atPath: outputFileURL.path) {
                finishContinuation?.resume(returning: outputFileURL)
            } else if let error {
                finishContinuation?.resume(throwing: error)
            } else {
                finishContinuation?.resume(throwing: RecorderError.noCamera)
            }
            finishContinuation = nil
        }
    }
}
