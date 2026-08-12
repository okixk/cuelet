import SwiftUI

struct SoundContextMenu: View {
    @EnvironmentObject private var appState: AppState
    let clip: SoundClip

    var body: some View {
        let targetClips = appState.contextMenuTargetClips(for: clip)
        let isMultiSelection = targetClips.count > 1
        let isPlaying = appState.playbackState.playingClipIDs.contains(clip.id)
        let hasPlayingSelection = targetClips.contains { appState.playbackState.playingClipIDs.contains($0.id) }
        let policy = SoundActionPolicy(clip: clip)

        if isMultiSelection {
            Button("Play Selected") {
                appState.play(targetClips.filter(\.isPlayable))
            }
            .disabled(!targetClips.contains(where: \.isPlayable))

            if hasPlayingSelection {
                Button("Stop Selected") {
                    appState.stop(targetClips)
                }
            }

            Divider()

            Button("Add to Favorites") {
                appState.setFavorite(targetClips, isFavorite: true)
            }

            Button("Remove from Favorites") {
                appState.setFavorite(targetClips, isFavorite: false)
            }

            Menu("Assign Category") {
                ForEach(appState.assignableCategories) { category in
                    Button(appState.name(for: category)) {
                        appState.assign(targetClips, to: category)
                    }
                }
                Divider()
                Button("New Category…") {
                    appState.createCategoryAndAssign(to: targetClips)
                }
            }

            Button("Reveal Selected in Finder") {
                appState.revealInFinder(targetClips)
            }
            .disabled(!targetClips.contains(where: \.isPlayable))

            Divider()

            Button("Remove from Library…", role: .destructive) {
                appState.removeFromLibrary(targetClips)
            }
        } else {
            Button("Play") {
                appState.play(clip)
            }
            .disabled(!policy.canPlay)

            if isPlaying {
                Button("Stop") {
                    appState.stop(clip)
                }
            }

            Divider()

            Button(clip.isFavorite ? "Remove from Favorites" : "Add to Favorites") {
                appState.toggleFavorite(clip)
            }

            Menu("Assign Category") {
                ForEach(appState.assignableCategories) { category in
                    Button(appState.name(for: category)) {
                        appState.assign(clip, to: category)
                    }
                }
                Divider()
                Button("New Category…") {
                    appState.createCategoryAndAssign(to: clip)
                }
            }

            Divider()

            Button("Rename…") {
                appState.rename(clip)
            }

            Button("Edit Notes & Aliases…") {
                appState.editNotesAndAliases(clip)
            }

            if let shortcut = clip.shortcut {
                Button("Change Shortcut… \(shortcut.displayLabel)") {
                    appState.updateShortcut(for: clip)
                }

                Button("Clear Shortcut") {
                    appState.clearShortcut(for: clip)
                }
            } else {
                Button("Set Shortcut…") {
                    appState.updateShortcut(for: clip)
                }
            }

            Button("Reveal in Finder") {
                appState.revealInFinder(clip)
            }
            .disabled(!policy.canReveal)

            if policy.canLocateOrRelink {
                Button(clip.storageMode == .linked ? "Relink…" : "Locate Replacement…") {
                    appState.locateOrRelink(clip)
                }
            }

            Button("Remove from Library…", role: .destructive) {
                appState.removeFromLibrary(clip)
            }

            if policy.canDeleteManagedFile {
                Button("Delete Managed File…", role: .destructive) {
                    appState.deleteManagedFile(clip)
                }
            }
        }
    }
}
