import AppKit
import SwiftUI

struct SoundListView: View {
    @EnvironmentObject private var appState: AppState
    let clips: [SoundClip]

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(Array(clips.enumerated()), id: \.element.id) { index, clip in
                        SoundListRow(clip: clip, isAlternate: index.isMultiple(of: 2))
                    }
                }
            }
            .background {
                Color.clear
                    .contentShape(Rectangle())
                    .onTapGesture {
                        appState.clearSelection()
                    }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color(nsColor: .textBackgroundColor))
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .strokeBorder(Color.secondary.opacity(0.16))
        }
    }

    private var header: some View {
        HStack(spacing: 0) {
            Text("Name").frame(maxWidth: .infinity, alignment: .leading)
            Text("Category").frame(width: 132, alignment: .leading)
            Text("Shortcut").frame(width: 92, alignment: .leading)
            Text("Duration").frame(width: 82, alignment: .leading)
            Text("Favorite").frame(width: 76, alignment: .center)
            Text("Play").frame(width: 76, alignment: .center)
        }
        .font(.caption.weight(.semibold))
        .foregroundStyle(.secondary)
        .padding(.horizontal, 12)
        .frame(height: 30)
        .background(Color(nsColor: .controlBackgroundColor))
    }
}

private struct SoundListRow: View {
    @EnvironmentObject private var appState: AppState
    let clip: SoundClip
    let isAlternate: Bool

    @State private var isHovering = false

    private var isSelected: Bool { appState.isSelected(clip) }
    private var isFocused: Bool { appState.isFocused(clip) }
    private var isPlaying: Bool { appState.playbackState.playingClipIDs.contains(clip.id) }

    var body: some View {
        HStack(spacing: 0) {
            HStack(spacing: 8) {
                Button {
                    appState.performPrimaryPlaybackAction(for: clip)
                } label: {
                    Image(systemName: isPlaying ? "speaker.slash.fill" : "play.fill")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(isPlaying ? Color(hex: "#2E8B57") : .secondary)
                        .frame(width: 22, height: 22)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(isPlaying ? "Stop \(clip.displayName)" : "Play \(clip.displayName)")

                Image(systemName: appState.systemImage(for: clip.category))
                    .foregroundStyle(appState.color(for: clip.category))
                    .frame(width: 18)
                Text(clip.displayName)
                    .lineLimit(1)
                    .truncationMode(.tail)
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            CategoryChip(
                category: clip.category,
                color: appState.color(for: clip.category),
                title: appState.name(for: clip.category)
            )
                .frame(width: 132, alignment: .leading)

            Group {
                if let shortcut = clip.shortcut {
                    ShortcutBadge(shortcut: shortcut)
                } else {
                    Text("—")
                        .foregroundStyle(.secondary)
                }
            }
                .frame(width: 92, alignment: .leading)

            Text(clip.duration > 0 ? clip.durationLabel : "—")
                .foregroundStyle(clip.duration > 0 ? .primary : .secondary)
                .frame(width: 82, alignment: .leading)

            Button {
                appState.toggleFavorite(clip)
            } label: {
                Image(systemName: clip.isFavorite ? "star.fill" : "star")
                    .foregroundStyle(clip.isFavorite ? Color(hex: "#D9822B") : .secondary)
                    .frame(width: 76)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(clip.isFavorite ? "Remove \(clip.displayName) from Favorites" : "Add \(clip.displayName) to Favorites")

            Button {
                appState.performPrimaryPlaybackAction(for: clip)
            } label: {
                Image(systemName: isPlaying ? "speaker.slash.fill" : "speaker.wave.2")
                    .foregroundStyle(isPlaying ? Color(hex: "#2E8B57") : .secondary)
                    .frame(width: 76)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(isPlaying ? "Stop \(clip.displayName)" : "Play \(clip.displayName)")
        }
        .font(.callout)
        .padding(.horizontal, 12)
        .frame(height: 34)
        .background(rowBackground)
        .overlay(alignment: .leading) {
            if isSelected {
                Rectangle()
                    .fill(Color.accentColor.opacity(isFocused ? 0.9 : 0.55))
                    .frame(width: 3)
            }
        }
        .contentShape(Rectangle())
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
    }

    private var rowBackground: Color {
        if isSelected { return Color.accentColor.opacity(isFocused ? 0.16 : 0.11) }
        if isHovering { return Color(nsColor: .controlBackgroundColor).opacity(0.72) }
        if isAlternate { return Color(nsColor: .controlBackgroundColor).opacity(0.45) }
        return Color.clear
    }

    private var currentSelectionModifiers: NSEvent.ModifierFlags {
        NSApp.currentEvent?.modifierFlags ?? NSEvent.modifierFlags
    }
}
