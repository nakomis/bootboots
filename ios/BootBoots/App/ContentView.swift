import AVFoundation
import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var auth: AuthService
    @StateObject private var monitor = SprayMonitor()

    var body: some View {
        NavigationStack {
            VStack(spacing: 16) {
                CameraPreview(session: monitor.recorder.session)
                    .frame(maxWidth: .infinity)
                    .frame(height: 220)
                    .background(Color.black)
                    .clipShape(RoundedRectangle(cornerRadius: 12))
                    .overlay(alignment: .topLeading) { SandboxBadge().padding(8) }

                HStack(spacing: 8) {
                    Circle()
                        .fill(monitor.running ? Color.green : Color.gray)
                        .frame(width: 10, height: 10)
                    Text(monitor.status)
                        .font(.headline)
                    Spacer()
                }

                VStack(alignment: .leading, spacing: 4) {
                    LabeledContent("Last trigger") {
                        Text(monitor.lastTrigger.map { $0.formatted(date: .omitted, time: .standard) } ?? "—")
                    }
                    LabeledContent("Last upload") {
                        Text(monitor.lastUpload ?? "—").lineLimit(1).truncationMode(.head)
                    }
                }
                .font(.subheadline)

                HStack {
                    if monitor.running {
                        Button("Stop", role: .destructive) { monitor.stop() }
                            .buttonStyle(.bordered)
                    } else {
                        Button("Start Monitoring") {
                            Task {
                                if let token = await auth.idToken() {
                                    await monitor.start(idToken: token)
                                }
                            }
                        }
                        .buttonStyle(.borderedProminent)
                    }
                    Button("Test Trigger") { monitor.fireTestTrigger() }
                        .buttonStyle(.bordered)
                        .disabled(!monitor.running)
                }

                logView

                Spacer()
            }
            .padding()
            .navigationTitle("BootBoots")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Sign Out") { auth.signOut() }
                }
            }
        }
    }

    private var logView: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 2) {
                ForEach(monitor.log, id: \.self) { line in
                    Text(line)
                        .font(.caption.monospaced())
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
        }
        .frame(maxHeight: 200)
        .padding(8)
        .background(Color(.secondarySystemBackground), in: RoundedRectangle(cornerRadius: 8))
    }
}

/// Live camera preview backed by AVCaptureVideoPreviewLayer.
struct CameraPreview: UIViewRepresentable {
    let session: AVCaptureSession

    func makeUIView(context: Context) -> PreviewView {
        let view = PreviewView()
        view.videoPreviewLayer.session = session
        view.videoPreviewLayer.videoGravity = .resizeAspectFill
        return view
    }

    func updateUIView(_ uiView: PreviewView, context: Context) {}

    final class PreviewView: UIView {
        override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }
        var videoPreviewLayer: AVCaptureVideoPreviewLayer { layer as! AVCaptureVideoPreviewLayer }
    }
}
