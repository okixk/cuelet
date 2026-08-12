import XCTest
@testable import Cuelet

@MainActor
final class GlobalShortcutTransactionTests: XCTestCase {
    func testFailedGlobalRegistrationRetainsPreviousShortcutAndRegistration() throws {
        let registry = FakeGlobalShortcutService()
        let appState = makeAppState(registry: registry)
        let clips = try makeClips()
        appState.clips = clips
        let oldShortcut = SoundShortcut(
            keyCode: 7,
            characters: "x",
            modifiers: [.option],
            scope: .global
        )
        XCTAssertEqual(appState.assignShortcut(oldShortcut, to: clips[0], replacingConflicts: false), .assigned)
        registry.failingKeyCode = 8

        let unavailable = SoundShortcut(
            keyCode: 8,
            characters: "c",
            modifiers: [.option],
            scope: .global
        )
        let result = appState.assignShortcut(unavailable, to: clips[0], replacingConflicts: false)

        guard case .registrationFailed = result else {
            return XCTFail("Expected registration failure, received \(result)")
        }
        XCTAssertEqual(appState.clips[0].shortcut, oldShortcut)
        XCTAssertEqual(registry.registeredClipIDs, [clips[0].id])
    }

    func testCrossScopeConflictRequiresReplacementAndMovesAssignmentAtomically() throws {
        let registry = FakeGlobalShortcutService()
        let appState = makeAppState(registry: registry)
        let clips = try makeClips()
        appState.clips = clips
        let local = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.option], scope: .local)
        let global = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.option], scope: .global)
        XCTAssertEqual(appState.assignShortcut(local, to: clips[0], replacingConflicts: false), .assigned)

        guard case .conflict(let conflict) = appState.assignShortcut(global, to: clips[1], replacingConflicts: false) else {
            return XCTFail("Expected a cross-scope conflict")
        }
        XCTAssertEqual(conflict.id, clips[0].id)
        XCTAssertEqual(appState.assignShortcut(global, to: clips[1], replacingConflicts: true), .assigned)

        XCTAssertNil(appState.clips.first { $0.id == clips[0].id }?.shortcut)
        XCTAssertEqual(appState.clips.first { $0.id == clips[1].id }?.shortcut, global)
        XCTAssertEqual(registry.registeredClipIDs, [clips[1].id])
    }

    func testDisabledAssignmentsDoNotConflictOrRegister() throws {
        let registry = FakeGlobalShortcutService()
        let appState = makeAppState(registry: registry)
        let clips = try makeClips()
        appState.clips = clips
        let disabled = SoundShortcut(
            keyCode: 7,
            characters: "x",
            modifiers: [.option],
            scope: .global,
            isEnabled: false
        )
        let local = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.option], scope: .local)

        XCTAssertEqual(appState.assignShortcut(disabled, to: clips[0], replacingConflicts: false), .assigned)
        XCTAssertEqual(appState.assignShortcut(local, to: clips[1], replacingConflicts: false), .assigned)
        XCTAssertTrue(registry.registeredClipIDs.isEmpty)
    }

    func testPersistenceFailureRollsBackProposedGlobalRegistration() throws {
        let registry = FakeGlobalShortcutService()
        let unwritableSettingsURL = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: unwritableSettingsURL, withIntermediateDirectories: true)
        let appState = AppState(
            settingsStore: SettingsStore(url: unwritableSettingsURL),
            globalShortcutService: registry,
            installKeyboardShortcuts: false
        )
        let clip = try XCTUnwrap(makeClips().first)
        appState.clips = [clip]
        let shortcut = SoundShortcut(
            keyCode: 7,
            characters: "x",
            modifiers: [.option],
            scope: .global
        )

        let result = appState.assignShortcut(shortcut, to: clip, replacingConflicts: false)

        guard case .persistenceFailed = result else {
            return XCTFail("Expected persistence failure, received \(result)")
        }
        XCTAssertNil(appState.clips.first?.shortcut)
        XCTAssertTrue(registry.registeredClipIDs.isEmpty)
    }

    func testShortcutCaptureTemporarilySuspendsAndRestoresGlobalRegistrations() throws {
        let registry = FakeGlobalShortcutService()
        let appState = makeAppState(registry: registry)
        let clip = try XCTUnwrap(makeClips().first)
        appState.clips = [clip]
        let shortcut = SoundShortcut(keyCode: 105, characters: nil, modifiers: [], scope: .global)
        XCTAssertEqual(appState.assignShortcut(shortcut, to: clip, replacingConflicts: false), .assigned)
        XCTAssertEqual(registry.registeredClipIDs, [clip.id])

        appState.beginShortcutCapture(for: clip)

        XCTAssertTrue(registry.registeredClipIDs.isEmpty)
        XCTAssertEqual(registry.unregisterAllCallCount, 1)
        XCTAssertEqual(appState.globalShortcutStatusMessage, "Global shortcuts paused while editing")

        appState.dismissShortcutCapture()

        XCTAssertEqual(registry.registeredClipIDs, [clip.id])
        XCTAssertEqual(appState.globalShortcutStatusMessage, "1 global shortcut registered")
    }

    private func makeAppState(registry: FakeGlobalShortcutService) -> AppState {
        AppState(
            settingsStore: SettingsStore(url: FileManager.default.temporaryDirectory
                .appendingPathComponent(UUID().uuidString)
                .appendingPathExtension("json")),
            globalShortcutService: registry,
            installKeyboardShortcuts: false
        )
    }

    private func makeClips() throws -> [SoundClip] {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        return ["one.wav", "two.wav"].map { filename in
            SoundClip(
                name: filename,
                filename: filename,
                category: .uncategorized,
                duration: 1,
                waveform: [],
                fileURL: root.appendingPathComponent(filename)
            )
        }
    }
}

private final class FakeGlobalShortcutService: GlobalShortcutRegistering {
    var registeredClipIDs: Set<SoundClip.ID> = []
    var lastErrorMessage: String?
    var failingKeyCode: UInt16?
    var unregisterAllCallCount = 0
    private var handler: ((SoundClip.ID) -> Void)?

    func setHandler(_ handler: @escaping (SoundClip.ID) -> Void) {
        self.handler = handler
    }

    func tryUpdate(_ assignments: [GlobalShortcutAssignment]) -> Result<Void, GlobalShortcutRegistrationError> {
        if let failingKeyCode, assignments.contains(where: { $0.shortcut.keyCode == failingKeyCode }) {
            let error = GlobalShortcutRegistrationError.unavailable(-1)
            lastErrorMessage = error.localizedDescription
            return .failure(error)
        }
        registeredClipIDs = Set(assignments.map(\.clipID))
        lastErrorMessage = nil
        return .success(())
    }

    func unregisterAll() {
        unregisterAllCallCount += 1
        registeredClipIDs.removeAll()
        lastErrorMessage = nil
    }
}
