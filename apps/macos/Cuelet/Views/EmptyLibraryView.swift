import SwiftUI

struct EmptyLibraryView: View {
    @EnvironmentObject private var appState: AppState
    let filter: LibraryFilter

    var body: some View {
        VStack(spacing: 18) {
            Image(systemName: filter.emptySystemImage)
                .font(.system(size: 48, weight: .regular))
                .foregroundStyle(.secondary)

            VStack(spacing: 6) {
                Text(filter.emptyTitle)
                    .font(.title3.weight(.semibold))
                Text(filter.emptyMessage)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 360)
            }

            if filter == .all {
                HStack(spacing: 10) {
                    Button("Choose Library…") {
                        appState.chooseLibrary()
                    }
                    .buttonStyle(.borderedProminent)

                    Button("Show Demo Library") {
                        appState.loadDemoLibrary()
                    }
                    .buttonStyle(.bordered)
                }
            }
        }
        .padding(32)
    }
}
