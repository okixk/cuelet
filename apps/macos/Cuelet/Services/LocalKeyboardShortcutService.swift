import AppKit

final class LocalKeyboardShortcutService {
    struct Handlers {
        let isSoundboardShortcutAvailable: @MainActor () -> Bool
        let playSelected: @MainActor () -> Bool
        let stopOrClearSelection: @MainActor () -> Bool
        let moveSelection: @MainActor (SelectionDirection, Bool) -> Bool
        let selectAll: @MainActor () -> Bool
        let playAssignedShortcut: @MainActor (SoundShortcut) -> Bool
    }

    enum SelectionDirection: Equatable {
        case left
        case right
        case down
        case up
    }

    enum Action: Equatable {
        case playSelected
        case stopOrClearSelection
        case moveSelection(SelectionDirection, extendsSelection: Bool)
        case selectAll

        static func keyDownAction(
            forKeyCode keyCode: UInt16,
            modifierFlags: NSEvent.ModifierFlags = []
        ) -> Action? {
            let usesCommandOnly = modifierFlags.contains(.command)
                && modifierFlags.intersection([.control, .option, .shift]).isEmpty
            if usesCommandOnly, keyCode == 0 {
                return .selectAll
            }

            guard modifierFlags.intersection([.command, .control, .option]).isEmpty else {
                return nil
            }

            let extendsSelection = modifierFlags.contains(.shift)
            switch keyCode {
            case 49:
                return .playSelected
            case 36:
                return .playSelected
            case 53:
                return .stopOrClearSelection
            case 123:
                return .moveSelection(.left, extendsSelection: extendsSelection)
            case 124:
                return .moveSelection(.right, extendsSelection: extendsSelection)
            case 125:
                return .moveSelection(.down, extendsSelection: extendsSelection)
            case 126:
                return .moveSelection(.up, extendsSelection: extendsSelection)
            default:
                return nil
            }
        }
    }

    private var handlers: Handlers?
    private var monitor: Any?

    deinit {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
    }

    func install(handlers: Handlers) {
        self.handlers = handlers

        guard monitor == nil else { return }

        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            self?.handle(event) ?? event
        }
    }

    private func handle(_ event: NSEvent) -> NSEvent? {
        guard NSApp.isActive else { return event }
        guard NSApp.keyWindow?.isKeyWindow == true else { return event }
        guard !Self.isTextInputOrEditableResponder(NSApp.keyWindow?.firstResponder) else { return event }
        if let action = Action.keyDownAction(forKeyCode: event.keyCode, modifierFlags: event.modifierFlags) {
            let wasConsumed = MainActor.assumeIsolated {
                consume(action)
            }

            if wasConsumed {
                return nil
            }
        }

        guard let shortcut = SoundShortcut(event: event),
              ShortcutCaptureService.validationResult(for: shortcut).isValid else {
            return event
        }

        let wasConsumed = MainActor.assumeIsolated {
            consume(shortcut)
        }
        return wasConsumed ? nil : event
    }

    @MainActor
    private func consume(_ action: Action) -> Bool {
        guard let handlers else { return false }

        switch action {
        case .playSelected:
            guard handlers.isSoundboardShortcutAvailable() else { return false }
            return handlers.playSelected()
        case .stopOrClearSelection:
            guard handlers.isSoundboardShortcutAvailable() else { return false }
            return handlers.stopOrClearSelection()
        case .moveSelection(let direction, let extendsSelection):
            guard handlers.isSoundboardShortcutAvailable() else { return false }
            return handlers.moveSelection(direction, extendsSelection)
        case .selectAll:
            guard handlers.isSoundboardShortcutAvailable() else { return false }
            return handlers.selectAll()
        }
    }

    @MainActor
    private func consume(_ shortcut: SoundShortcut) -> Bool {
        guard let handlers else { return false }
        guard handlers.isSoundboardShortcutAvailable() else { return false }
        return handlers.playAssignedShortcut(shortcut)
    }

    static func hasReservedModifier(in modifierFlags: NSEvent.ModifierFlags) -> Bool {
        !modifierFlags.intersection([.command, .control, .option]).isEmpty
    }

    static func isTextInputOrEditableResponder(_ responder: NSResponder?) -> Bool {
        guard let responder else { return false }

        if responder is NSTextView {
            return true
        }

        if let textField = responder as? NSTextField, textField.isEditable, textField.isEnabled {
            return true
        }

        if let comboBox = responder as? NSComboBox, comboBox.isEditable, comboBox.isEnabled {
            return true
        }

        return false
    }
}
