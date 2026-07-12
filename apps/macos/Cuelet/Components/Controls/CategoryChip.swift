import SwiftUI

struct CategoryChip: View {
    let category: SoundCategory
    var color: Color? = nil
    var title: String? = nil

    private var resolvedColor: Color {
        color ?? category.color
    }

    var body: some View {
        HStack(spacing: 6) {
            Text(title ?? category.rawValue)
                .lineLimit(1)
            Circle()
                .fill(resolvedColor)
                .frame(width: 8, height: 8)
        }
        .font(.caption.weight(.medium))
        .foregroundStyle(.primary)
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .background(resolvedColor.opacity(0.12), in: Capsule())
    }
}
