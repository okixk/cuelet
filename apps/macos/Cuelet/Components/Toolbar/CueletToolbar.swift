import SwiftUI

struct CueletToolbar: ToolbarContent {
    @ObservedObject var appState: AppState

    var body: some ToolbarContent {
        ToolbarItemGroup(placement: .primaryAction) {
            ToolbarMenuButton(title: "Library", systemImage: "folder") {
                Button("Choose Library…") { appState.chooseLibrary() }
                Button("Import Sounds…") { appState.importSounds() }
                Divider()
                Button("Rescan") { appState.rescanLibrary() }
            }

            Button {
                appState.stopAllPlayback()
            } label: {
                Label("Stop All", systemImage: "stop.fill")
            }
            .disabled(!appState.playbackState.isPlaying)
            .help("Stop All")

            Menu {
                Picker("Sort By", selection: $appState.sortOption) {
                    ForEach(SoundSortOption.allCases) { sortOption in
                        Text(sortOption.title).tag(sortOption)
                    }
                }
            } label: {
                Label("Sort", systemImage: "arrow.up.arrow.down")
            }
            .menuStyle(.button)
            .help("Sort")

            Picker("View", selection: $appState.viewMode) {
                ForEach(ViewMode.allCases) { viewMode in
                    Label(viewMode.title, systemImage: viewMode.systemImage).tag(viewMode)
                }
            }
            .pickerStyle(.segmented)
            .frame(width: 88)
        }
    }
}
