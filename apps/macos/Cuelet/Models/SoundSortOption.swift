import Foundation

enum SoundSortOption: String, CaseIterable, Identifiable, Codable {
    case nameAscending
    case nameDescending
    case latestAdded
    case oldestAdded
    case durationShortest
    case durationLongest
    case category

    var id: String { rawValue }

    var title: String {
        switch self {
        case .nameAscending: "Name A-Z"
        case .nameDescending: "Name Z-A"
        case .latestAdded: "Latest Added"
        case .oldestAdded: "Oldest Added"
        case .durationShortest: "Duration Shortest"
        case .durationLongest: "Duration Longest"
        case .category: "Category"
        }
    }
}
