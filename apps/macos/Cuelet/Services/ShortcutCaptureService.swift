import AppKit
import Combine

final class ShortcutCaptureService: ObservableObject {
    enum ValidationResult: Equatable {
        case valid
        case invalid(String)

        var isValid: Bool {
            self == .valid
        }
    }

    @Published private(set) var livePreviewLabel: String?
    @Published private(set) var statusMessage = "Press a key combination"
    @Published private(set) var capturedShortcut: SoundShortcut?

    private var monitor: Any?
    private var onValidShortcut: ((SoundShortcut) -> Void)?
    private var onCancel: (() -> Void)?
    private var onClear: (() -> Void)?

    deinit {
        stop()
    }

    func start(
        onValidShortcut: @escaping (SoundShortcut) -> Void,
        onCancel: @escaping () -> Void = {},
        onClear: @escaping () -> Void = {}
    ) {
        stop()
        self.onValidShortcut = onValidShortcut
        self.onCancel = onCancel
        self.onClear = onClear
        livePreviewLabel = nil
        capturedShortcut = nil
        statusMessage = "Press a key combination"

        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged]) { [weak self] event in
            self?.handle(event) ?? event
        }
    }

    func stop() {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
        monitor = nil
        onValidShortcut = nil
        onCancel = nil
        onClear = nil
    }

    @discardableResult
    func handle(_ event: NSEvent) -> NSEvent? {
        switch event.type {
        case .flagsChanged:
            let modifiers = ShortcutModifiers(modifierFlags: event.modifierFlags)
            livePreviewLabel = modifiers.displayLabel.isEmpty ? nil : modifiers.displayLabel
            return nil

        case .keyDown:
            guard !event.isARepeat else { return nil }
            if event.keyCode == 53 {
                onCancel?()
                return nil
            }
            if event.keyCode == 51 || event.keyCode == 117 {
                livePreviewLabel = nil
                capturedShortcut = nil
                statusMessage = "Shortcut cleared"
                onClear?()
                return nil
            }
            guard let shortcut = SoundShortcut(event: event) else { return nil }
            livePreviewLabel = shortcut.displayLabel

            switch Self.validationResult(for: shortcut) {
            case .valid:
                capturedShortcut = shortcut
                statusMessage = "Shortcut set: \(shortcut.displayLabel)"
                onValidShortcut?(shortcut)
            case .invalid(let message):
                capturedShortcut = nil
                statusMessage = message
                NSSound.beep()
            }
            return nil

        default:
            return event
        }
    }

    static func validationResult(for shortcut: SoundShortcut) -> ValidationResult {
        if shortcut.isModifierOnlyKey {
            return .invalid("Press a normal key with your modifiers.")
        }

        if shortcut.modifiers.isEmpty {
            return shortcut.isFunctionKeyF13ThroughF20
                ? .valid
                : .invalid("Use at least one modifier key.")
        }

        if isReservedTerminalKey(shortcut.keyCode) || isReservedSystemShortcut(shortcut) {
            return .invalid("That shortcut is reserved by macOS. Try another one.")
        }

        return .valid
    }

    private static func isReservedTerminalKey(_ keyCode: UInt16) -> Bool {
        switch keyCode {
        case 36, 49, 53:
            return true
        default:
            return false
        }
    }

    static func isReservedSystemShortcut(_ shortcut: SoundShortcut) -> Bool {
        let modifiers = shortcut.modifiers
        if modifiers == [.command] {
            return [4, 12, 13, 46, 48, 49].contains(shortcut.keyCode)
        }
        if modifiers == [.control, .command], shortcut.keyCode == 12 {
            return true
        }
        if modifiers == [.option, .command], shortcut.keyCode == 53 {
            return true
        }
        if modifiers == [.control], (123...126).contains(shortcut.keyCode) {
            return true
        }
        if modifiers == [.command, .shift], [20, 21, 23].contains(shortcut.keyCode) {
            return true
        }
        return false
    }
}
