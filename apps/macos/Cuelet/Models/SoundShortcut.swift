import AppKit
import Foundation

struct ShortcutModifiers: OptionSet, Codable, Hashable {
    let rawValue: Int

    static let command = ShortcutModifiers(rawValue: 1 << 0)
    static let shift = ShortcutModifiers(rawValue: 1 << 1)
    static let option = ShortcutModifiers(rawValue: 1 << 2)
    static let control = ShortcutModifiers(rawValue: 1 << 3)

    init(rawValue: Int) {
        self.rawValue = rawValue
    }

    init(modifierFlags: NSEvent.ModifierFlags) {
        var modifiers: ShortcutModifiers = []
        if modifierFlags.contains(.command) { modifiers.insert(.command) }
        if modifierFlags.contains(.shift) { modifiers.insert(.shift) }
        if modifierFlags.contains(.option) { modifiers.insert(.option) }
        if modifierFlags.contains(.control) { modifiers.insert(.control) }
        self = modifiers
    }

    var displayLabel: String {
        var label = ""
        if contains(.command) { label += "⌘" }
        if contains(.control) { label += "⌃" }
        if contains(.option) { label += "⌥" }
        if contains(.shift) { label += "⇧" }
        return label
    }
}

struct SoundShortcut: Codable, Hashable {
    var keyCode: UInt16
    var characters: String?
    var modifiers: ShortcutModifiers
    var scope: HotkeyScope
    var isEnabled: Bool

    init(
        keyCode: UInt16,
        characters: String?,
        modifiers: ShortcutModifiers,
        scope: HotkeyScope = .local,
        isEnabled: Bool = true
    ) {
        self.keyCode = keyCode
        self.characters = Self.normalizedCharacters(characters)
        self.modifiers = modifiers
        self.scope = scope
        self.isEnabled = isEnabled
    }

    init?(event: NSEvent) {
        guard event.type == .keyDown else { return nil }
        self.init(
            keyCode: event.keyCode,
            characters: event.charactersIgnoringModifiers,
            modifiers: ShortcutModifiers(modifierFlags: event.modifierFlags)
        )
    }

    var displayLabel: String {
        modifiers.displayLabel + Self.keyLabel(forKeyCode: keyCode, characters: characters)
    }

    func matches(keyCode: UInt16, modifierFlags: NSEvent.ModifierFlags) -> Bool {
        self.keyCode == keyCode && modifiers == ShortcutModifiers(modifierFlags: modifierFlags)
    }

    func hasSameKeyCombination(as other: SoundShortcut) -> Bool {
        keyCode == other.keyCode && modifiers == other.modifiers
    }

    func normalized(scope: HotkeyScope? = nil) -> SoundShortcut {
        SoundShortcut(
            keyCode: keyCode,
            characters: characters,
            modifiers: modifiers,
            scope: scope ?? self.scope,
            isEnabled: isEnabled
        )
    }

    var isFunctionKeyF13ThroughF20: Bool {
        switch keyCode {
        case 105, 107, 113, 106, 64, 79, 80, 90:
            return true
        default:
            return false
        }
    }

    var isModifierOnlyKey: Bool {
        switch keyCode {
        case 54, 55, 56, 57, 58, 59, 60, 61, 62:
            return true
        default:
            return false
        }
    }

    private static func normalizedCharacters(_ characters: String?) -> String? {
        guard let characters, !characters.isEmpty else { return nil }
        if characters == " " { return characters }
        return String(characters.prefix(1))
    }

    private static func keyLabel(forKeyCode keyCode: UInt16, characters: String?) -> String {
        if let specialLabel = specialKeyLabels[keyCode] {
            return specialLabel
        }

        guard let characters, !characters.isEmpty else {
            return "Key \(keyCode)"
        }

        if characters == " " {
            return "Space"
        }

        return characters.uppercased()
    }

    private static let specialKeyLabels: [UInt16: String] = [
        36: "↩",
        48: "Tab",
        49: "Space",
        51: "⌫",
        53: "⎋",
        64: "F17",
        79: "F18",
        80: "F19",
        90: "F20",
        96: "F5",
        97: "F6",
        98: "F7",
        99: "F3",
        100: "F8",
        101: "F9",
        103: "F11",
        105: "F13",
        106: "F16",
        107: "F14",
        109: "F10",
        111: "F12",
        113: "F15",
        117: "⌦",
        118: "F4",
        120: "F2",
        122: "F1",
        123: "←",
        124: "→",
        125: "↓",
        126: "↑"
    ]

    enum CodingKeys: String, CodingKey {
        case keyCode
        case characters
        case modifiers
        case scope
        case isEnabled
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        keyCode = try container.decode(UInt16.self, forKey: .keyCode)
        characters = Self.normalizedCharacters(try container.decodeIfPresent(String.self, forKey: .characters))
        modifiers = try container.decode(ShortcutModifiers.self, forKey: .modifiers)
        scope = try container.decodeIfPresent(HotkeyScope.self, forKey: .scope) ?? .local
        isEnabled = try container.decodeIfPresent(Bool.self, forKey: .isEnabled) ?? true
    }
}
