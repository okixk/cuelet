import SwiftUI

struct RootView: View {
    @EnvironmentObject private var appState: AppState
    @State private var columnVisibility: NavigationSplitViewVisibility = .all

    var body: some View {
        NavigationSplitView(columnVisibility: $columnVisibility) {
            SidebarView(selection: $appState.selectedSidebarItem, categories: appState.categories)
        } detail: {
            detailView
                .toolbar {
                    CueletToolbar(appState: appState)
                }
                .navigationTitle(appState.navigationTitle)
        }
        .background(MainWindowTrackerView().frame(width: 0, height: 0))
        .sheet(item: $appState.shortcutCaptureRequest) { request in
            ShortcutCaptureSheet(request: request)
                .environmentObject(appState)
        }
        .sheet(item: $appState.categoryEditorRequest) { request in
            CategoryEditorSheet(request: request, category: appState.categoryForEditing(request))
                .environmentObject(appState)
        }
    }

    @ViewBuilder
    private var detailView: some View {
        SoundLibraryView()
    }
}
