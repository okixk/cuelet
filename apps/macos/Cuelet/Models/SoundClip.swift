import Foundation

enum SoundStorageMode: String, Codable, Hashable, CaseIterable {
    case managed
    case linked
}

struct SoundFileIdentity: Codable, Hashable {
    var resourceIdentifier: String?
    var volumeIdentifier: String?
    var fileSize: Int64?
    var contentModificationDate: Date?
}

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

    // Schema-v2 library fields. fileURL is a resolved runtime value; these fields
    // are the durable source of truth.
    var storageMode: SoundStorageMode
    var managedRelativePath: String?
    var externalSourcePath: String?
    var originalSourcePath: String?
    var securityScopedBookmark: Data?
    var isBookmarkStale: Bool
    var isMissing: Bool
    var originalFilename: String
    var notes: String
    var aliases: [String]
    var fileIdentity: SoundFileIdentity?

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
        fileURL: URL? = nil,
        storageMode: SoundStorageMode = .managed,
        managedRelativePath: String? = nil,
        externalSourcePath: String? = nil,
        originalSourcePath: String? = nil,
        securityScopedBookmark: Data? = nil,
        isBookmarkStale: Bool = false,
        isMissing: Bool = false,
        originalFilename: String? = nil,
        notes: String = "",
        aliases: [String] = [],
        fileIdentity: SoundFileIdentity? = nil
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
        self.storageMode = storageMode
        self.managedRelativePath = managedRelativePath
        self.externalSourcePath = externalSourcePath
        self.originalSourcePath = originalSourcePath
        self.securityScopedBookmark = securityScopedBookmark
        self.isBookmarkStale = isBookmarkStale
        self.isMissing = isMissing
        self.originalFilename = originalFilename ?? filename
        self.notes = notes
        self.aliases = aliases
        self.fileIdentity = fileIdentity
    }

    enum CodingKeys: String, CodingKey {
        case id, name, filename, category, duration, shortcut, waveform
        case isFavorite, addedAt, lastPlayedAt, fileURL
        case storageMode, managedRelativePath, externalSourcePath, originalSourcePath
        case securityScopedBookmark, isBookmarkStale, isMissing
        case originalFilename, notes, aliases, fileIdentity
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decodeIfPresent(UUID.self, forKey: .id) ?? UUID()
        name = try container.decodeIfPresent(String.self, forKey: .name) ?? ""
        filename = try container.decodeIfPresent(String.self, forKey: .filename) ?? ""
        category = try container.decodeIfPresent(SoundCategory.self, forKey: .category) ?? .uncategorized
        duration = try container.decodeIfPresent(TimeInterval.self, forKey: .duration) ?? 0
        shortcut = try? container.decodeIfPresent(SoundShortcut.self, forKey: .shortcut)
        waveform = try container.decodeIfPresent([Double].self, forKey: .waveform) ?? []
        isFavorite = try container.decodeIfPresent(Bool.self, forKey: .isFavorite) ?? false
        addedAt = try container.decodeIfPresent(Date.self, forKey: .addedAt) ?? .distantPast
        lastPlayedAt = try container.decodeIfPresent(Date.self, forKey: .lastPlayedAt)
        fileURL = try container.decodeIfPresent(URL.self, forKey: .fileURL)
        storageMode = try container.decodeIfPresent(SoundStorageMode.self, forKey: .storageMode) ?? .managed
        managedRelativePath = try container.decodeIfPresent(String.self, forKey: .managedRelativePath)
        externalSourcePath = try container.decodeIfPresent(String.self, forKey: .externalSourcePath)
        originalSourcePath = try container.decodeIfPresent(String.self, forKey: .originalSourcePath)
        securityScopedBookmark = try container.decodeIfPresent(Data.self, forKey: .securityScopedBookmark)
        isBookmarkStale = try container.decodeIfPresent(Bool.self, forKey: .isBookmarkStale) ?? false
        isMissing = try container.decodeIfPresent(Bool.self, forKey: .isMissing) ?? false
        originalFilename = try container.decodeIfPresent(String.self, forKey: .originalFilename) ?? filename
        notes = try container.decodeIfPresent(String.self, forKey: .notes) ?? ""
        aliases = try container.decodeIfPresent([String].self, forKey: .aliases) ?? []
        fileIdentity = try container.decodeIfPresent(SoundFileIdentity.self, forKey: .fileIdentity)
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

    var isPlayable: Bool {
        !isMissing && fileURL != nil
    }

    var storageLabel: String {
        if isMissing { return "Missing" }
        return storageMode == .linked ? "Linked" : "Managed"
    }
}
