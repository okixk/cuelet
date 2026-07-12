import SwiftUI

struct SearchField: View {
    @Binding var text: String
    @FocusState.Binding var isFocused: Bool
    var onSubmit: () -> Void
    var onEscape: () -> Void

    var body: some View {
        TextField("Search sounds", text: $text)
            .textFieldStyle(.roundedBorder)
            .focused($isFocused)
            .onSubmit(onSubmit)
            .onExitCommand(perform: onEscape)
            .frame(width: 260)
    }
}
