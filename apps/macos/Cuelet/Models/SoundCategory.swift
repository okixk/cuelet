import SwiftUI

struct SoundCategory: Identifiable, Hashable, Codable {
    let id: String
    var name: String
    var defaultColorHex: String
    var iconID: String
    var isUserEditable: Bool

    init(
        id: String,
        name: String,
        defaultColorHex: String = "#8E8E93",
        iconID: String = "tag",
        isUserEditable: Bool = true
    ) {
        self.id = id
        self.name = name
        self.defaultColorHex = defaultColorHex
        self.iconID = Self.canonicalIconID(iconID)
        self.isUserEditable = isUserEditable
    }

    init(
        id: String,
        name: String,
        defaultColorHex: String = "#8E8E93",
        systemImage: String,
        isUserEditable: Bool = true
    ) {
        self.init(
            id: id,
            name: name,
            defaultColorHex: defaultColorHex,
            iconID: systemImage,
            isUserEditable: isUserEditable
        )
    }

    var rawValue: String { name }

    var color: Color {
        Color(hex: defaultColorHex)
    }

    var systemImage: String {
        Self.systemImage(for: iconID)
    }

    static let uncategorized = SoundCategory(
        id: "uncategorized",
        name: "Uncategorized",
        defaultColorHex: "#8E8E93",
        iconID: "folder",
        isUserEditable: false
    )

    static let defaultColorHexes: [String: String] = [uncategorized].reduce(into: [:]) { colors, category in
        colors[category.id] = category.defaultColorHex
    }

    static let palette: [(name: String, hex: String)] = [
        ("Gray", "#8E8E93"),
        ("Blue", "#3478F6"),
        ("Teal", "#009688"),
        ("Green", "#2E8B57"),
        ("Yellow", "#B38B00"),
        ("Orange", "#D9822B"),
        ("Red", "#D64545"),
        ("Pink", "#D65780"),
        ("Purple", "#AF52DE")
    ]

    static let iconChoices: [(name: String, id: String, systemImage: String)] = [
        ("Tag", "tag", "tag"),
        ("Folder", "folder", "folder"),
        ("Music", "music-note", "music.note"),
        ("Speaker", "audio-speakers", "speaker.wave.2"),
        ("Waveform", "waveform", "waveform"),
        ("Bell", "bell", "bell"),
        ("Sparkles", "sparkles", "sparkles"),
        ("Weather", "weather-showers", "cloud.rain"),
        ("Game", "applications-games", "gamecontroller"),
        ("Microphone", "microphone", "mic"),
        ("Chat", "chat-message", "bubble.left"),
        ("Star", "star", "star"),
        ("Heart", "heart", "heart"),
        ("Bolt", "bolt", "bolt"),
        ("Flame", "flame", "flame"),
        ("Smile", "face-smile", "face.smiling")
    ]

    enum CodingKeys: String, CodingKey {
        case id
        case name
        case defaultColorHex
        case iconID
        case systemImage
        case isUserEditable
    }

    init(from decoder: Decoder) throws {
        if let container = try? decoder.singleValueContainer(),
           let rawValue = try? container.decode(String.self) {
            self = Self.category(forLegacyRawValue: rawValue)
            return
        }

        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(String.self, forKey: .id)
        name = try container.decode(String.self, forKey: .name)
        defaultColorHex = try container.decodeIfPresent(String.self, forKey: .defaultColorHex) ?? "#8E8E93"
        let storedIcon = try container.decodeIfPresent(String.self, forKey: .iconID)
            ?? container.decodeIfPresent(String.self, forKey: .systemImage)
            ?? "tag"
        iconID = Self.canonicalIconID(storedIcon)
        isUserEditable = try container.decodeIfPresent(Bool.self, forKey: .isUserEditable) ?? true
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(id, forKey: .id)
        try container.encode(name, forKey: .name)
        try container.encode(defaultColorHex, forKey: .defaultColorHex)
        try container.encode(iconID, forKey: .iconID)
        try container.encode(isUserEditable, forKey: .isUserEditable)
    }

    private static func category(forLegacyRawValue rawValue: String) -> SoundCategory {
        rawValue == uncategorized.name || rawValue == uncategorized.id
            ? uncategorized
            : SoundCategory(id: stableID(for: rawValue), name: rawValue)
    }

    static func makeUserCategory(named name: String, colorHex: String = "#3478F6") -> SoundCategory {
        SoundCategory(
            id: stableID(for: name),
            name: name,
            defaultColorHex: colorHex,
            iconID: "tag",
            isUserEditable: true
        )
    }

    static func systemImage(for iconID: String) -> String {
        let canonicalID = canonicalIconID(iconID)
        return iconChoices.first { $0.id == canonicalID }?.systemImage ?? "tag"
    }

    static func canonicalIconID(_ value: String) -> String {
        if iconChoices.contains(where: { $0.id == value }) {
            return value
        }

        let aliases: [String: String] = [
            "folder-symbolic": "folder", "tray": "folder",
            "music": "music-note", "music.note": "music-note", "audio-x-generic-symbolic": "music-note",
            "speaker": "audio-speakers", "speaker.wave.2": "audio-speakers", "audio-speakers-symbolic": "audio-speakers",
            "sound-wave-symbolic": "waveform",
            "weather": "weather-showers", "cloud.rain": "weather-showers", "weather-showers-symbolic": "weather-showers",
            "game": "applications-games", "gamecontroller": "applications-games", "applications-games-symbolic": "applications-games",
            "mic": "microphone", "audio-input-microphone-symbolic": "microphone",
            "chat": "chat-message", "message": "chat-message", "bubble.left": "chat-message", "chat-message-new-symbolic": "chat-message",
            "starred-symbolic": "star", "emblem-favorite-symbolic": "heart",
            "weather-storm-symbolic": "bolt", "bolt.fill": "bolt", "flame.fill": "flame",
            "face.smiling": "face-smile", "wand.and.stars": "sparkles", "quote.bubble": "chat-message"
        ]
        return aliases[value] ?? "tag"
    }

    private static func stableID(for name: String) -> String {
        let normalized = name
            .folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
            .lowercased()
        let slug = normalized
            .map { character -> Character in
                if character.isLetter || character.isNumber { return character }
                return "-"
            }
            .split(separator: "-")
            .joined(separator: "-")
        let slugText = String(slug)

        var hash: UInt64 = 14_695_981_039_346_656_037
        for byte in Array(name.utf8) {
            hash ^= UInt64(byte)
            hash &*= 1_099_511_628_211
        }

        return "user-\(slugText.isEmpty ? "category" : slugText)-\(String(format: "%06llx", hash & 0xFFFFFF))"
    }
}

extension Color {
    init(hex: String) {
        let normalized = hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        guard normalized.count == 6, let value = Int(normalized, radix: 16) else {
            self = .gray
            return
        }

        self = Color(
            red: Double((value >> 16) & 0xFF) / 255,
            green: Double((value >> 8) & 0xFF) / 255,
            blue: Double(value & 0xFF) / 255
        )
    }
}
