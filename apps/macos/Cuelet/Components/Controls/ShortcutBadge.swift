import SwiftUI

struct ShortcutBadge: View {
    let shortcut: SoundShortcut?

    var body: some View {
        if let shortcut {
            Text(shortcut.displayLabel)
                .font(.caption.weight(.medium))
                .monospacedDigit()
                .foregroundStyle(.secondary)
                .padding(.horizontal, 7)
                .padding(.vertical, 3)
                .background(.quaternary, in: Capsule())
        }
    }
}
