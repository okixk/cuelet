import SwiftUI

struct CategoryEditorSheet: View {
    @EnvironmentObject private var appState: AppState

    let request: CategoryEditorRequest
    let category: SoundCategory?

    @State private var name: String
    @State private var colorHex: String
    @State private var iconID: String
    @State private var validationMessage: String?
    @FocusState private var isNameFocused: Bool

    init(request: CategoryEditorRequest, category: SoundCategory?) {
        self.request = request
        self.category = category
        _name = State(initialValue: category?.name ?? "")
        _colorHex = State(initialValue: category?.defaultColorHex ?? SoundCategory.palette[1].hex)
        _iconID = State(initialValue: category?.iconID ?? "tag")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text(category == nil ? "New Category" : "Edit Category")
                .font(.headline)

            TextField("Name", text: $name)
                .focused($isNameFocused)

            VStack(alignment: .leading, spacing: 10) {
                Text("Color")
                    .font(.callout.weight(.medium))
                HStack(spacing: 10) {
                    ForEach(SoundCategory.palette, id: \.hex) { choice in
                        Button {
                            colorHex = choice.hex
                        } label: {
                            Circle()
                                .fill(Color(hex: choice.hex))
                                .frame(width: 22, height: 22)
                                .overlay {
                                    Circle()
                                        .strokeBorder(.primary.opacity(colorHex == choice.hex ? 0.7 : 0), lineWidth: 2)
                                        .padding(-3)
                                }
                        }
                        .buttonStyle(.plain)
                        .help(choice.name)
                        .accessibilityLabel(choice.name)
                    }
                }
            }

            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Text("Icon")
                        .font(.callout.weight(.medium))
                    Spacer()
                    if let selected = SoundCategory.iconChoices.first(where: { $0.id == iconID }) {
                        Label(selected.name, systemImage: selected.systemImage)
                            .foregroundStyle(.secondary)
                    }
                }

                LazyVGrid(columns: [GridItem(.adaptive(minimum: 96), spacing: 8)], spacing: 8) {
                    ForEach(SoundCategory.iconChoices, id: \.id) { choice in
                        Button {
                            iconID = choice.id
                        } label: {
                            VStack(spacing: 5) {
                                Image(systemName: choice.systemImage)
                                    .font(.system(size: 18))
                                    .frame(height: 22)
                                Text(choice.name)
                                    .font(.caption)
                                    .lineLimit(1)
                            }
                            .frame(maxWidth: .infinity)
                            .frame(height: 48)
                            .background(
                                iconID == choice.id ? Color.accentColor.opacity(0.14) : Color.clear,
                                in: RoundedRectangle(cornerRadius: 6, style: .continuous)
                            )
                            .overlay {
                                RoundedRectangle(cornerRadius: 6, style: .continuous)
                                    .strokeBorder(iconID == choice.id ? Color.accentColor : Color.secondary.opacity(0.18))
                            }
                        }
                        .buttonStyle(.plain)
                    }
                }
            }

            if let validationMessage {
                Label(validationMessage, systemImage: "exclamationmark.triangle")
                    .font(.callout)
                    .foregroundStyle(.red)
            }

            HStack {
                Spacer()
                Button("Cancel") {
                    appState.dismissCategoryEditor()
                }
                .keyboardShortcut(.cancelAction)

                Button("Save") {
                    validationMessage = appState.saveCategoryEditor(
                        request: request,
                        name: name,
                        colorHex: colorHex,
                        iconID: iconID
                    )
                }
                .keyboardShortcut(.defaultAction)
                .disabled(name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }
        }
        .padding(22)
        .frame(width: 440)
        .onAppear { isNameFocused = true }
    }
}
