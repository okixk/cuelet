import AppKit
import SwiftUI

struct SoundPadView: View {
    @EnvironmentObject private var appState: AppState
    let clip: SoundClip

    @State private var isHovering = false

    private var isSelected: Bool { appState.isSelected(clip) }
    private var isFocused: Bool { appState.isFocused(clip) }
    private var isPlaying: Bool { appState.playbackState.playingClipIDs.contains(clip.id) }
    private var actionPolicy: SoundActionPolicy { SoundActionPolicy(clip: clip) }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            ZStack(alignment: .topTrailing) {
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(previewBackground)
                    .frame(height: 78)
                    .overlay {
                        if clip.isMissing {
                            Label("Missing", systemImage: "exclamationmark.triangle.fill")
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(.secondary)
                        } else {
                            WaveformPreview(samples: clip.waveform, isPlaying: isPlaying)
                                .padding(.horizontal, 18)
                        }
                    }

                Button {
                    appState.toggleFavorite(clip)
                } label: {
                    Image(systemName: clip.isFavorite ? "star.fill" : "star")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(clip.isFavorite ? favoriteColor : .secondary)
                        .frame(width: 28, height: 28)
                        .contentShape(Circle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(clip.isFavorite ? "Remove \(clip.displayName) from Favorites" : "Add \(clip.displayName) to Favorites")
                .opacity(isHovering || clip.isFavorite ? 1 : 0)
                .animation(.easeInOut(duration: 0.12), value: isHovering || clip.isFavorite)
                .padding(4)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)

                Button {
                    appState.performPrimaryPlaybackAction(for: clip)
                } label: {
                    Image(systemName: isPlaying ? "speaker.slash.fill" : "play.fill")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(isPlaying ? playingColor : .secondary)
                        .frame(width: 30, height: 30)
                        .contentShape(Circle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(isPlaying ? "Stop \(clip.displayName)" : "Play \(clip.displayName)")
                .disabled(!actionPolicy.canPlay)
                .padding(4)
            }

            Text(clip.displayName)
                .font(.headline)
                .foregroundStyle(.primary)
                .lineLimit(1)
                .truncationMode(.tail)

            Label(
                clip.storageLabel,
                systemImage: clip.isMissing ? "exclamationmark.triangle" : (clip.storageMode == .linked ? "link" : "tray.full")
            )
            .font(.caption2.weight(.semibold))
            .foregroundStyle(clip.isMissing ? Color.orange : .secondary)
            .lineLimit(1)

            HStack(spacing: 8) {
                CategoryChip(
                    category: clip.category,
                    color: appState.color(for: clip.category),
                    title: appState.name(for: clip.category)
                )
                Spacer(minLength: 8)
                if clip.shortcut == nil {
                    Text(clip.duration > 0 ? clip.durationLabel : "—")
                        .font(.caption.weight(.medium))
                        .foregroundStyle(.secondary)
                } else {
                    ShortcutBadge(shortcut: clip.shortcut)
                }
            }
            .frame(height: 22)
        }
        .padding(14)
        .frame(height: 188)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(cardBackground, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .strokeBorder(borderColor, lineWidth: 1.5)
        }
        .shadow(color: shadowColor, radius: isHovering ? 8 : 2, y: isHovering ? 4 : 1)
        .contentShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
        .onTapGesture(count: 2) {
            appState.resignTextInputFocus()
            appState.play(clip)
        }
        .onTapGesture {
            appState.resignTextInputFocus()
            appState.select(clip, modifiers: currentSelectionModifiers)
        }
        .onHover { hovering in
            isHovering = hovering
        }
        .contextMenu {
            SoundContextMenu(clip: clip)
                .onAppear {
                    appState.prepareContextMenu(for: clip)
                }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(clip.displayName), \(clip.storageLabel)")
        .accessibilityValue(isPlaying ? "Playing" : (clip.isFavorite ? "Favorite" : "Not playing"))
    }

    private var previewBackground: Color {
        isPlaying ? playingColor.opacity(0.14) : Color.secondary.opacity(0.08)
    }

    private var cardBackground: Color {
        if isSelected { return Color.accentColor.opacity(isFocused ? 0.12 : 0.08) }
        if isHovering { return Color(nsColor: .controlBackgroundColor) }
        return Color(nsColor: .textBackgroundColor)
    }

    private var borderColor: Color {
        if isSelected { return .accentColor.opacity(isFocused ? 0.66 : 0.46) }
        return .secondary.opacity(isHovering ? 0.28 : 0.12)
    }

    private var shadowColor: Color {
        Color.black.opacity(isHovering ? 0.12 : 0.06)
    }

    private var playingColor: Color {
        Color(hex: "#2E8B57")
    }

    private var favoriteColor: Color {
        Color(hex: "#D9822B")
    }

    private var currentSelectionModifiers: NSEvent.ModifierFlags {
        NSApp.currentEvent?.modifierFlags ?? NSEvent.modifierFlags
    }
}
