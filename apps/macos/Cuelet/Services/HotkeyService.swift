import Carbon
import Foundation

enum HotkeyScope: String, Codable, Hashable, CaseIterable, Identifiable {
    case local
    case global

    var id: String { rawValue }

    var title: String {
        switch self {
        case .local: "Local"
        case .global: "Global"
        }
    }
}

struct GlobalShortcutAssignment: Equatable {
    let clipID: SoundClip.ID
    let shortcut: SoundShortcut
}

enum GlobalShortcutRegistrationError: LocalizedError, Equatable {
    case unavailable(OSStatus)

    var errorDescription: String? {
        switch self {
        case .unavailable:
            "This shortcut is unavailable or already used by macOS or another application."
        }
    }
}

protocol GlobalShortcutRegistering: AnyObject {
    var registeredClipIDs: Set<SoundClip.ID> { get }
    var lastErrorMessage: String? { get }

    func setHandler(_ handler: @escaping (SoundClip.ID) -> Void)
    func tryUpdate(_ assignments: [GlobalShortcutAssignment]) -> Result<Void, GlobalShortcutRegistrationError>
    func unregisterAll()
}

final class CarbonGlobalShortcutService: GlobalShortcutRegistering {
    private struct Combination: Hashable {
        let keyCode: UInt16
        let modifiers: ShortcutModifiers
    }

    private struct Registration {
        let clipID: SoundClip.ID
        let combination: Combination
        let nativeID: UInt32
        let reference: EventHotKeyRef
    }

    private static let signature: OSType = 0x4355454C // CUEL

    private var registrations: [Combination: Registration] = [:]
    private var eventHandlerReference: EventHandlerRef?
    private var handler: ((SoundClip.ID) -> Void)?
    private var nextNativeID: UInt32 = 1
    private(set) var lastErrorMessage: String?

    var registeredClipIDs: Set<SoundClip.ID> {
        Set(registrations.values.map(\.clipID))
    }

    init() {
        installEventHandler()
    }

    deinit {
        unregisterAll()
        if let eventHandlerReference {
            RemoveEventHandler(eventHandlerReference)
        }
    }

    func setHandler(_ handler: @escaping (SoundClip.ID) -> Void) {
        self.handler = handler
    }

    func tryUpdate(_ assignments: [GlobalShortcutAssignment]) -> Result<Void, GlobalShortcutRegistrationError> {
        let desired = assignments.filter { $0.shortcut.scope == .global && $0.shortcut.isEnabled }
        var proposed: [Combination: Registration] = [:]
        var newRegistrations: [Registration] = []

        for assignment in desired {
            let combination = Combination(
                keyCode: assignment.shortcut.keyCode,
                modifiers: assignment.shortcut.modifiers
            )

            if let existing = registrations[combination] {
                proposed[combination] = Registration(
                    clipID: assignment.clipID,
                    combination: combination,
                    nativeID: existing.nativeID,
                    reference: existing.reference
                )
                continue
            }

            var reference: EventHotKeyRef?
            let nativeID = nextNativeID
            nextNativeID &+= 1
            let hotKeyID = EventHotKeyID(signature: Self.signature, id: nativeID)
            let status = RegisterEventHotKey(
                UInt32(assignment.shortcut.keyCode),
                carbonModifiers(for: assignment.shortcut.modifiers),
                hotKeyID,
                GetApplicationEventTarget(),
                0,
                &reference
            )

            guard status == noErr, let reference else {
                newRegistrations.forEach { UnregisterEventHotKey($0.reference) }
                lastErrorMessage = GlobalShortcutRegistrationError.unavailable(status).localizedDescription
                return .failure(.unavailable(status))
            }

            let registration = Registration(
                clipID: assignment.clipID,
                combination: combination,
                nativeID: nativeID,
                reference: reference
            )
            proposed[combination] = registration
            newRegistrations.append(registration)
        }

        for (combination, registration) in registrations where proposed[combination] == nil {
            UnregisterEventHotKey(registration.reference)
        }
        registrations = proposed
        lastErrorMessage = nil
        return .success(())
    }

    func unregisterAll() {
        registrations.values.forEach { UnregisterEventHotKey($0.reference) }
        registrations.removeAll()
        lastErrorMessage = nil
    }

    private func installEventHandler() {
        var eventType = EventTypeSpec(
            eventClass: OSType(kEventClassKeyboard),
            eventKind: UInt32(kEventHotKeyPressed)
        )
        InstallEventHandler(
            GetApplicationEventTarget(),
            { _, event, userData in
                guard let event, let userData else { return OSStatus(eventNotHandledErr) }
                let service = Unmanaged<CarbonGlobalShortcutService>.fromOpaque(userData).takeUnretainedValue()
                return service.handle(event)
            },
            1,
            &eventType,
            Unmanaged.passUnretained(self).toOpaque(),
            &eventHandlerReference
        )
    }

    private func handle(_ event: EventRef) -> OSStatus {
        var hotKeyID = EventHotKeyID()
        let status = GetEventParameter(
            event,
            EventParamName(kEventParamDirectObject),
            EventParamType(typeEventHotKeyID),
            nil,
            MemoryLayout<EventHotKeyID>.size,
            nil,
            &hotKeyID
        )
        guard status == noErr,
              hotKeyID.signature == Self.signature,
              let clipID = registrations.values.first(where: { $0.nativeID == hotKeyID.id })?.clipID else {
            return OSStatus(eventNotHandledErr)
        }

        handler?(clipID)
        return noErr
    }

    private func carbonModifiers(for modifiers: ShortcutModifiers) -> UInt32 {
        var result: UInt32 = 0
        if modifiers.contains(.command) { result |= UInt32(cmdKey) }
        if modifiers.contains(.option) { result |= UInt32(optionKey) }
        if modifiers.contains(.control) { result |= UInt32(controlKey) }
        if modifiers.contains(.shift) { result |= UInt32(shiftKey) }
        return result
    }
}
