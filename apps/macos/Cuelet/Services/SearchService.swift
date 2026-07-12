import Foundation

struct SearchService {
    func filter(clips: [SoundClip], searchText: String, filter: LibraryFilter) -> [SoundClip] {
        let scopedClips = clipsForFilter(clips: clips, filter: filter)
        let trimmedSearch = searchText.trimmingCharacters(in: .whitespacesAndNewlines)

        let searchedClips = scopedClips.filter { clip in
            trimmedSearch.isEmpty
                || clip.displayName.localizedCaseInsensitiveContains(trimmedSearch)
                || clip.filename.localizedCaseInsensitiveContains(trimmedSearch)
                || clip.category.rawValue.localizedCaseInsensitiveContains(trimmedSearch)
        }

        if case .recent = filter {
            return searchedClips.sorted { lhs, rhs in
                (lhs.lastPlayedAt ?? .distantPast) > (rhs.lastPlayedAt ?? .distantPast)
            }
        }

        guard !trimmedSearch.isEmpty else { return searchedClips }

        return searchedClips.sorted { lhs, rhs in
            let lhsScore = rankingScore(for: lhs, query: trimmedSearch)
            let rhsScore = rankingScore(for: rhs, query: trimmedSearch)
            if lhsScore != rhsScore { return lhsScore > rhsScore }
            return lhs.displayName.localizedStandardCompare(rhs.displayName) == .orderedAscending
        }
    }

    private func rankingScore(for clip: SoundClip, query: String) -> Int {
        let normalizedQuery = query.normalizedForSearch
        let name = clip.displayName.normalizedForSearch
        let filename = clip.filename.normalizedForSearch
        let category = clip.category.rawValue.normalizedForSearch

        if name == normalizedQuery { return 1_000 }
        if name.hasPrefix(normalizedQuery) { return 900 }
        if filename == normalizedQuery { return 800 }
        if filename.hasPrefix(normalizedQuery) { return 700 }

        let queryTokens = normalizedQuery.split(separator: " ")
        if !queryTokens.isEmpty, queryTokens.allSatisfy({ name.contains($0) }) { return 600 }
        if name.contains(normalizedQuery) { return 500 }
        if filename.contains(normalizedQuery) { return 400 }
        if category.contains(normalizedQuery) { return 300 }
        return 0
    }

    private func clipsForFilter(clips: [SoundClip], filter: LibraryFilter) -> [SoundClip] {
        switch filter {
        case .all, .allCategories:
            clips
        case .favorites:
            clips.filter(\.isFavorite)
        case .recent:
            clips.filter { $0.lastPlayedAt != nil }
        case .category(let category):
            clips.filter { $0.category.id == category.id }
        }
    }
}

private extension String {
    var normalizedForSearch: String {
        folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
            .replacingOccurrences(of: "-", with: " ")
            .replacingOccurrences(of: "_", with: " ")
            .split(whereSeparator: \.isWhitespace)
            .joined(separator: " ")
    }
}

enum LibraryFilter: Hashable {
    case all
    case favorites
    case recent
    case allCategories
    case category(SoundCategory)

    var title: String {
        switch self {
        case .all: "Library"
        case .favorites: "Favorites"
        case .recent: "Recent"
        case .allCategories: "All Categories"
        case .category(let category): category.rawValue
        }
    }

    var emptyTitle: String {
        switch self {
        case .all: "No Sound Library"
        case .favorites: "No favorites yet"
        case .recent: "No recently played sounds yet"
        case .allCategories: "No categorized sounds yet"
        case .category: "No sounds in this category yet"
        }
    }

    var emptyMessage: String {
        switch self {
        case .all:
            "Choose a folder of audio files to start building pads."
        case .favorites:
            "Mark sounds with a star to find them here."
        case .recent:
            "Play a sound and it will appear here."
        case .allCategories:
            "Assign categories from a sound's context menu to organize the library."
        case .category:
            "Drag sounds here or assign a category from a sound's context menu."
        }
    }

    var emptySystemImage: String {
        switch self {
        case .all: "folder.badge.plus"
        case .favorites: "star"
        case .recent: "clock.arrow.circlepath"
        case .allCategories: "square.grid.2x2"
        case .category: "tray"
        }
    }
}
