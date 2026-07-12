import SwiftUI

struct NowPlayingMiniPlayerView: View {
    @EnvironmentObject private var appState: AppState

    var body: some View {
        TimelineView(.periodic(from: .now, by: 0.25)) { timeline in
            if let currentClip = appState.mostRecentPlayingClip {
                miniPlayer(for: currentClip)
            }
        }
    }

    private func miniPlayer(for currentClip: SoundClip) -> some View {
        let playingCount = appState.playingClips.count
        let progress = appState.playbackProgress(for: currentClip)

        return HStack(spacing: 12) {
            Image(systemName: "speaker.wave.2.fill")
                .font(.system(size: 14, weight: .semibold))
                .foregroundStyle(Color(hex: "#2E8B57"))
                .frame(width: 26, height: 26)
                .background(Color(hex: "#2E8B57").opacity(0.14), in: Circle())

            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 8) {
                    Text(currentClip.displayName)
                        .font(.callout.weight(.semibold))
                        .lineLimit(1)
                        .truncationMode(.tail)

                    CategoryChip(
                        category: currentClip.category,
                        color: appState.color(for: currentClip.category),
                        title: appState.name(for: currentClip.category)
                    )
                    .fixedSize()

                    if playingCount > 1 {
                        Text("+\(playingCount - 1)")
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(.secondary)
                            .padding(.horizontal, 7)
                            .padding(.vertical, 3)
                            .background(Color.secondary.opacity(0.12), in: Capsule())
                    }
                }

                HStack(spacing: 8) {
                    ProgressView(value: progress?.fraction ?? 0)
                        .progressViewStyle(.linear)
                        .controlSize(.small)

                    Text(timeLabel(for: progress))
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                        .frame(minWidth: 72, alignment: .trailing)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            if playingCount > 1 {
                Menu {
                    ForEach(appState.playingClips) { clip in
                        Button("Stop \(clip.displayName)") {
                            appState.stop(clip)
                        }
                    }
                    Divider()
                    Button("Stop All") {
                        appState.stopAllPlayback()
                    }
                } label: {
                    Image(systemName: "list.bullet")
                        .frame(width: 26, height: 26)
                }
                .menuStyle(.button)
                .buttonStyle(.borderless)
                .help("Currently playing")
            }

            Button {
                appState.stop(currentClip)
            } label: {
                Label("Stop", systemImage: "stop.fill")
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .help("Stop current sound")

            if playingCount > 1 {
                Button {
                    appState.stopAllPlayback()
                } label: {
                    Label("Stop All", systemImage: "stop.circle")
                }
                .buttonStyle(.bordered)
                .controlSize(.small)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 9)
        .frame(minHeight: 56)
        .background(.regularMaterial)
        .id(currentClip.id)
        .transition(.move(edge: .bottom).combined(with: .opacity))
    }

    private func timeLabel(for progress: PlaybackService.Progress?) -> String {
        guard let progress else { return "0:00 / --:--" }
        return "\(format(progress.position)) / \(format(progress.duration))"
    }

    private func format(_ seconds: TimeInterval) -> String {
        guard seconds.isFinite, seconds >= 0 else { return "--:--" }
        let totalSeconds = Int(seconds.rounded(.down))
        return String(format: "%d:%02d", totalSeconds / 60, totalSeconds % 60)
    }
}
