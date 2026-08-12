import SwiftUI

struct SoundLibraryView: View {
    @EnvironmentObject private var appState: AppState
    @FocusState private var isSearchFocused: Bool

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()

            if !appState.persistenceStatusMessage.isEmpty {
                Label(appState.persistenceStatusMessage, systemImage: "externaldrive.badge.exclamationmark")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 24)
                    .padding(.vertical, 10)
                    .background(Color.orange.opacity(0.1))
                    .accessibilityLabel("Library status: \(appState.persistenceStatusMessage)")
                Divider()
            }

            if appState.visibleClips.isEmpty {
                EmptyLibraryView(filter: appState.activeLibraryFilter)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                libraryContent
                    .padding(24)
            }

            if appState.playbackState.isPlaying {
                Divider()
                NowPlayingMiniPlayerView()
                    .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .animation(.easeInOut(duration: 0.16), value: appState.playbackState.isPlaying)
        .contextMenu {
            Button("Choose Library…") { appState.chooseLibrary() }
            Button("Import Sounds…") { appState.importSounds() }
            Button("Rescan Library") { appState.rescanLibrary() }
            Divider()
            Button("New Category…") { appState.newCategory() }
        }
        .onChange(of: appState.searchFocusRequestID) { _, _ in
            isSearchFocused = true
        }
    }

    @ViewBuilder
    private var libraryContent: some View {
        switch appState.viewMode {
        case .grid:
            SoundPadGridView(clips: appState.visibleClips)
        case .list:
            SoundListView(clips: appState.visibleClips)
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(appState.navigationTitle)
                        .font(.title2.weight(.semibold))
                    Text(appState.librarySubtitle)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }

                Spacer()

                Text(appState.visibleSoundCountText)
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }

            HStack(spacing: 12) {
                SearchField(
                    text: $appState.searchText,
                    isFocused: $isSearchFocused,
                    onSubmit: {
                        _ = appState.playRecommendedSearchResult()
                    },
                    onEscape: {
                        appState.clearSearchOrStopAll()
                    }
                )
                    .frame(maxWidth: 360)
                Spacer()
            }
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 18)
    }
}
