import SwiftUI

@main
struct BootBootsApp: App {
    @StateObject private var auth = AuthService()

    var body: some Scene {
        WindowGroup {
            Group {
                if auth.isSignedIn {
                    ContentView()
                } else {
                    SignInView()
                }
            }
            .environmentObject(auth)
            // Sandbox-only build: green tint throughout as a visible environment marker.
            .tint(.green)
            .task { await auth.restore() }
        }
    }
}

/// A small "SANDBOX" badge reused across screens.
struct SandboxBadge: View {
    var body: some View {
        Text("SANDBOX")
            .font(.caption2.weight(.bold))
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(.green.opacity(0.9), in: Capsule())
            .foregroundStyle(.black)
    }
}
