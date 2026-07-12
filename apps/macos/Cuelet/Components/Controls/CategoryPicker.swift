import SwiftUI

struct CategoryPicker: View {
    let categories: [SoundCategory]
    @Binding var selection: SoundCategory?

    var body: some View {
        Picker("Category", selection: $selection) {
            Text("All Categories").tag(nil as SoundCategory?)
            ForEach(categories) { category in
                Text(category.rawValue).tag(category as SoundCategory?)
            }
        }
        .pickerStyle(.menu)
        .frame(width: 160)
    }
}
