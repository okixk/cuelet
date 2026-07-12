import SwiftUI

struct WaveformPreview: View {
    let samples: [Double]
    let isPlaying: Bool

    var body: some View {
        GeometryReader { geometry in
            let barWidth = max(3, geometry.size.width / CGFloat(max(samples.count, 1)) * 0.42)
            HStack(alignment: .center, spacing: 4) {
                ForEach(Array(samples.enumerated()), id: \.offset) { _, sample in
                    Capsule(style: .continuous)
                        .fill(isPlaying ? Color.accentColor : Color.secondary.opacity(0.42))
                        .frame(width: barWidth, height: max(8, geometry.size.height * CGFloat(sample)))
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(height: 46)
        .accessibilityHidden(true)
    }
}
