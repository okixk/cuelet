import SwiftUI

struct ShortcutRecorderView: View {
    @ObservedObject var captureService: ShortcutCaptureService
    var statusMessage: String?
    var fallbackLabel = "Press a key combination"

    var body: some View {
        VStack(spacing: 10) {
            Text(captureService.livePreviewLabel ?? fallbackLabel)
                .font(.system(size: 26, weight: .semibold))
                .monospaced()
                .foregroundStyle(.primary)
                .frame(maxWidth: .infinity)
                .frame(height: 58)
                .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
                .overlay {
                    RoundedRectangle(cornerRadius: 8, style: .continuous)
                        .strokeBorder(Color.secondary.opacity(0.18))
                }

            Text(statusMessage ?? captureService.statusMessage)
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(minHeight: 20)
        }
    }
}
