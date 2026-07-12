import SwiftUI

struct OverlayPreviewView: View {
    @EnvironmentObject private var appState: AppState

    private let columns = Array(repeating: GridItem(.fixed(104), spacing: 10), count: 3)

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Overlay Preview")
                    .font(.title2.weight(.semibold))
                Text("A small floating pad grid can be added after the main library UI is stable.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }

            VStack(spacing: 12) {
                HStack {
                    Text("Default Profile")
                        .font(.headline)
                    Spacer()
                    Button("Stop All") {
                        appState.stopAllPlayback()
                    }
                    .controlSize(.small)
                }

                LazyVGrid(columns: columns, spacing: 10) {
                    ForEach(appState.clips.prefix(9)) { clip in
                        Button {
                            appState.togglePlayback(for: clip)
                        } label: {
                            VStack(spacing: 8) {
                                Image(systemName: appState.playbackState.playingClipIDs.contains(clip.id) ? "speaker.wave.2.fill" : "play.fill")
                                    .font(.title3)
                                Text(clip.displayName)
                                    .font(.caption)
                                    .lineLimit(2)
                                    .multilineTextAlignment(.center)
                                    .frame(height: 32)
                            }
                            .frame(width: 104, height: 88)
                        }
                    }
                }
            }
            .padding(16)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            .frame(maxWidth: 380)

            Spacer()
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color(nsColor: .windowBackgroundColor))
    }
}
