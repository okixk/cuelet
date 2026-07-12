import SwiftUI

struct ToolbarMenuButton<MenuContent: View>: View {
    let title: String
    let systemImage: String
    @ViewBuilder var menuContent: () -> MenuContent

    var body: some View {
        Menu {
            menuContent()
        } label: {
            Label(title, systemImage: systemImage)
        }
        .menuStyle(.button)
    }
}
