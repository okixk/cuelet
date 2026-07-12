import SwiftUI

struct SidebarView: View {
    @EnvironmentObject private var appState: AppState
    @Binding var selection: AppState.SidebarItem
    let categories: [SoundCategory]

    @State private var isCategoriesExpanded = true

    var body: some View {
        List(selection: $selection) {
            Section("Library") {
                Label("Library", systemImage: "rectangle.grid.2x2")
                    .tag(AppState.SidebarItem.library)
                Label("Favorites", systemImage: "star")
                    .tag(AppState.SidebarItem.favorites)
                Label("Recent", systemImage: "clock")
                    .tag(AppState.SidebarItem.recent)
            }

            DisclosureGroup("Categories", isExpanded: $isCategoriesExpanded) {
                Label("All Categories", systemImage: "square.grid.2x2")
                    .tag(AppState.SidebarItem.allCategories)
                    .contextMenu {
                        Button("New Category…") {
                            appState.newCategory()
                        }
                    }

                ForEach(categories) { category in
                    CategorySidebarRow(category: category)
                        .tag(AppState.SidebarItem.category(category))
                }
            }
        }
        .listStyle(.sidebar)
        .navigationSplitViewColumnWidth(min: 200, ideal: 230, max: 300)
    }
}

private struct CategorySidebarRow: View {
    @EnvironmentObject private var appState: AppState
    let category: SoundCategory

    var body: some View {
        HStack(spacing: 8) {
            Label(appState.name(for: category), systemImage: appState.systemImage(for: category))
            Spacer(minLength: 8)
            Circle()
                .fill(appState.color(for: category))
                .frame(width: 9, height: 9)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .contentShape(Rectangle())
        .contextMenu {
            categoryMenuItems
        }
    }

    @ViewBuilder
    private var categoryMenuItems: some View {
        if appState.canEditCategory(category) {
            Button("Edit Category…") {
                appState.renameCategory(category)
            }

            Button("Rename Category") {
                appState.renameCategory(category)
            }

            Menu("Change Color") {
                ForEach(SoundCategory.palette, id: \.hex) { paletteColor in
                    Button {
                        appState.changeColor(for: category, to: paletteColor.hex)
                    } label: {
                        HStack {
                            Circle()
                                .fill(Color(hex: paletteColor.hex))
                                .frame(width: 10, height: 10)
                            Text(paletteColor.name)
                            if appState.categoryColorHex(for: category) == paletteColor.hex {
                                Image(systemName: "checkmark")
                            }
                        }
                    }
                }
            }

            Menu("Change Icon") {
                ForEach(SoundCategory.iconChoices, id: \.id) { icon in
                    Button {
                        appState.changeIcon(for: category, to: icon.id)
                    } label: {
                        HStack {
                            Label(icon.name, systemImage: icon.systemImage)
                            if appState.iconID(for: category) == icon.id {
                                Image(systemName: "checkmark")
                            }
                        }
                    }
                }
            }

            Button("Delete Category", role: .destructive) {
                appState.confirmDeleteCategory(category)
            }

            Divider()
        }

        Button("New Category…") {
            appState.newCategory()
        }
    }
}
