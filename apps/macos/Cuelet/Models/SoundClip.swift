import Foundation

struct SoundClip: Identifiable, Hashable, Codable {
    let id: UUID
    var name: String
    var filename: String
    var category: SoundCategory
    var duration: TimeInterval
    var shortcut: SoundShortcut?
    var waveform: [Double]
    var isFavorite: Bool
    var addedAt: Date
    var lastPlayedAt: Date?
    var fileURL: URL?

    init(
        id: UUID = UUID(),
        name: String,
        filename: String,
        category: SoundCategory,
        duration: TimeInterval,
        shortcut: SoundShortcut? = nil,
        waveform: [Double],
        isFavorite: Bool = false,
        addedAt: Date = .distantPast,
        lastPlayedAt: Date? = nil,
        fileURL: URL? = nil
    ) {
        self.id = id
        self.name = name
        self.filename = filename
        self.category = category
        self.duration = duration
        self.shortcut = shortcut
        self.waveform = waveform
        self.isFavorite = isFavorite
        self.addedAt = addedAt
        self.lastPlayedAt = lastPlayedAt
        self.fileURL = fileURL
    }

    enum CodingKeys: String, CodingKey {
        case id
        case name
        case filename
        case category
        case duration
        case shortcut
        case waveform
        case isFavorite
        case addedAt
        case lastPlayedAt
        case fileURL
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        name = try container.decode(String.self, forKey: .name)
        filename = try container.decode(String.self, forKey: .filename)
        category = try container.decode(SoundCategory.self, forKey: .category)
        duration = try container.decode(TimeInterval.self, forKey: .duration)
        shortcut = try? container.decodeIfPresent(SoundShortcut.self, forKey: .shortcut)
        waveform = try container.decode([Double].self, forKey: .waveform)
        isFavorite = try container.decode(Bool.self, forKey: .isFavorite)
        addedAt = try container.decodeIfPresent(Date.self, forKey: .addedAt) ?? .distantPast
        lastPlayedAt = try container.decodeIfPresent(Date.self, forKey: .lastPlayedAt)
        fileURL = try container.decodeIfPresent(URL.self, forKey: .fileURL)
    }

    var durationLabel: String {
        let totalSeconds = Int(duration.rounded())
        return String(format: "%d:%02d", totalSeconds / 60, totalSeconds % 60)
    }

    var displayName: String {
        if !name.isEmpty { return name }
        if let fileURL {
            return fileURL.deletingPathExtension().lastPathComponent
        }
        return filename.isEmpty ? "Untitled Sound" : URL(fileURLWithPath: filename).deletingPathExtension().lastPathComponent
    }
}
