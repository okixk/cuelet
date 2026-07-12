import SwiftUI

struct SoundPadGridView: View {
    @EnvironmentObject private var appState: AppState
    let clips: [SoundClip]

    private let columns = [
        GridItem(.adaptive(minimum: 190, maximum: 240), spacing: 16, alignment: .top)
    ]

    var body: some View {
        ScrollView {
            LazyVGrid(columns: columns, alignment: .leading, spacing: 16) {
                ForEach(clips) { clip in
                    SoundPadView(clip: clip)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background {
            Color.clear
                .contentShape(Rectangle())
                .onTapGesture {
                    appState.clearSelection()
                }
        }
    }
}
