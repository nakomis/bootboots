import SwiftUI

struct SignInView: View {
    @EnvironmentObject private var auth: AuthService
    @State private var signingIn = false

    var body: some View {
        VStack(spacing: 28) {
            Spacer()

            Image(systemName: "pawprint.circle.fill")
                .resizable()
                .frame(width: 96, height: 96)
                .foregroundStyle(.green)

            VStack(spacing: 6) {
                Text("BootBoots")
                    .font(.largeTitle.bold())
                SandboxBadge()
                Text("Records a clip when the deterrent fires.")
                    .foregroundStyle(.secondary)
                    .padding(.top, 4)
            }

            Spacer()

            if let error = auth.lastError {
                Text(error)
                    .font(.caption)
                    .foregroundStyle(.red)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal)
            }

            Button {
                signingIn = true
                Task {
                    await auth.signIn()
                    signingIn = false
                }
            } label: {
                Group {
                    if signingIn { ProgressView() } else { Text("Sign In") }
                }
                .frame(maxWidth: .infinity)
                .padding()
                .background(Color.green)
                .foregroundStyle(.white)
                .clipShape(RoundedRectangle(cornerRadius: 12))
            }
            .disabled(signingIn)
            .padding(.horizontal)
            .padding(.bottom, 32)
        }
    }
}
