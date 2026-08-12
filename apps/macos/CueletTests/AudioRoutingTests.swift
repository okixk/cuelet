import CoreAudio
import XCTest
@testable import Cuelet

@MainActor
final class AudioRoutingTests: XCTestCase {
    func testSettingsPersistStableUIDAndFallbackPolicy() throws {
        let url = temporarySettingsURL()
        let store = SettingsStore(url: url)
        var settings = CueletSettings()
        settings.outputDeviceID = AudioDevice.persistentID(forCoreAudioUID: "stable-device-uid")
        settings.outputDeviceName = "Studio Output"
        settings.outputFallbackPolicy = .systemOutput

        XCTAssertTrue(store.save(settings))

        let loaded = store.load()
        XCTAssertEqual(loaded.schemaVersion, CueletSettings.currentSchemaVersion)
        XCTAssertEqual(loaded.outputDeviceID, "coreaudio:stable-device-uid")
        XCTAssertEqual(loaded.outputDeviceName, "Studio Output")
        XCTAssertEqual(loaded.outputFallbackPolicy, .systemOutput)
    }

    func testPreviousTemporaryNumericRoutingSettingMigratesToSystemOutput() throws {
        let data = Data(#"{"schemaVersion":2,"outputDeviceID":"coreaudio-91","outputDeviceName":"Temporary ID"}"#.utf8)
        let settings = try JSONDecoder().decode(CueletSettings.self, from: data)

        XCTAssertEqual(settings.schemaVersion, CueletSettings.currentSchemaVersion)
        XCTAssertEqual(settings.outputDeviceID, AudioDevice.systemOutput.id)
        XCTAssertEqual(settings.outputDeviceName, AudioDevice.systemOutput.name)
        XCTAssertEqual(settings.outputFallbackPolicy, .stopAndWait)
    }

    func testStableUIDFromPreviousSettingsFormatIsPreserved() throws {
        let data = Data(#"{"schemaVersion":2,"outputDeviceID":"coreaudio:persistent-uid","outputDeviceName":"Renamed Device"}"#.utf8)
        let settings = try JSONDecoder().decode(CueletSettings.self, from: data)

        XCTAssertEqual(settings.outputDeviceID, "coreaudio:persistent-uid")
        XCTAssertEqual(settings.outputDeviceName, "Renamed Device")
    }

    func testOutputNormalizationExcludesInputDeadAndDuplicateUIDs() {
        let first = liveDevice(uid: "same", name: "Output A", numericID: 14)
        let duplicate = liveDevice(uid: "same", name: "Output Alias", numericID: 15)
        let input = LiveAudioOutputDevice(
            device: AudioDevice(
                id: AudioDevice.persistentID(forCoreAudioUID: "input"),
                name: "Input Only",
                kind: .input,
                isDefault: false,
                isVirtual: false
            ),
            audioDeviceID: 16
        )
        var dead = liveDevice(uid: "dead", name: "Dead", numericID: 17)
        dead.device.isAlive = false

        let normalized = AudioDeviceService.normalizedOutputDevices([duplicate, input, dead, first])

        XCTAssertEqual(normalized.count, 1)
        XCTAssertEqual(normalized.first?.device.coreAudioUID, "same")
    }

    func testVirtualTransportDetectionUsesCoreAudioTransportProperty() {
        XCTAssertTrue(AudioDeviceService.isVirtualTransport(kAudioDeviceTransportTypeVirtual))
        XCTAssertFalse(AudioDeviceService.isVirtualTransport(kAudioDeviceTransportTypeBuiltIn))
        XCTAssertFalse(AudioDeviceService.isVirtualTransport(nil))
    }

    func testSystemOutputSelectionUsesNilPlayerUIDAndConfirmsOnlyAfterPlayback() async throws {
        let provider = FakeAudioDeviceProvider(system: liveDevice(uid: "system-live", name: "Built-in Output", numericID: 1))
        let factory = FakeAudioPlayerFactory()
        let appState = makeAppState(provider: provider, factory: factory)

        XCTAssertEqual(appState.settings.outputDeviceID, AudioDevice.systemOutput.id)
        XCTAssertEqual(appState.audioRouteStatus.kind, .ready)
        XCTAssertFalse(appState.audioRouteStatus.isConfirmedActive)

        let clip = try playableClip(name: "System")
        appState.clips = [clip]
        appState.play(clip)

        XCTAssertFalse(appState.audioRouteStatus.isConfirmedActive)
        await Task.yield()

        XCTAssertEqual(factory.players.first?.currentOutputDeviceUID, nil)
        XCTAssertEqual(appState.audioRouteStatus.kind, .systemOutput)
        XCTAssertEqual(appState.audioRouteStatus.activeName, "Built-in Output")
    }

    func testExplicitSelectionRoutesByUIDPersistsAndRestoresAfterRestart() async throws {
        let selected = liveDevice(uid: "usb-stable", name: "USB Output", numericID: 44, transport: "USB")
        let provider = FakeAudioDeviceProvider(outputs: [selected])
        let factory = FakeAudioPlayerFactory()
        let settingsURL = temporarySettingsURL()
        let appState = makeAppState(provider: provider, factory: factory, settingsURL: settingsURL)

        XCTAssertTrue(appState.selectOutputDevice(id: selected.device.id))
        XCTAssertEqual(appState.settings.outputDeviceID, "coreaudio:usb-stable")
        XCTAssertEqual(appState.playbackService.configuredOutputDeviceUID, "usb-stable")
        XCTAssertEqual(appState.audioRouteStatus.kind, .ready)

        let clip = try playableClip(name: "Explicit")
        appState.clips = [clip]
        appState.play(clip)
        await Task.yield()

        XCTAssertEqual(factory.players.first?.currentOutputDeviceUID, "usb-stable")
        XCTAssertEqual(appState.audioRouteStatus.kind, .explicitDevice)
        appState.prepareForTermination()

        let restartedPlayback = PlaybackService(playerFactory: FakeAudioPlayerFactory())
        let restarted = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            playbackService: restartedPlayback,
            audioDeviceService: provider,
            globalShortcutService: FakeRoutingShortcutService(),
            installKeyboardShortcuts: false
        )
        XCTAssertEqual(restarted.settings.outputDeviceID, selected.device.id)
        XCTAssertEqual(restartedPlayback.configuredOutputDeviceUID, "usb-stable")
        XCTAssertEqual(restarted.audioRouteStatus.kind, .ready)
        restarted.prepareForTermination()
    }

    func testUnavailableDeviceStopsPlaybackAndKeepsStableSelectionByDefault() async throws {
        let selected = liveDevice(uid: "disconnecting", name: "Disposable Output", numericID: 51)
        let provider = FakeAudioDeviceProvider(outputs: [selected])
        let factory = FakeAudioPlayerFactory()
        let appState = makeAppState(provider: provider, factory: factory)
        XCTAssertTrue(appState.selectOutputDevice(id: selected.device.id))

        let clip = try playableClip(name: "Disconnect")
        appState.clips = [clip]
        appState.play(clip)
        await Task.yield()
        provider.outputs = []
        provider.notifyChange()

        XCTAssertFalse(appState.playbackState.isPlaying)
        XCTAssertEqual(appState.audioRouteStatus.kind, .unavailable)
        XCTAssertEqual(appState.settings.outputDeviceID, selected.device.id)
        XCTAssertEqual(appState.playbackService.configuredOutputDeviceUID, "disconnecting")
        XCTAssertFalse(appState.audioRouteStatus.allowsPlayback)
        XCTAssertEqual(factory.players.first?.stopCount, 1)
    }

    func testUnavailableDeviceCanTemporarilyFallbackWithoutChangingSavedUID() async throws {
        let selected = liveDevice(uid: "virtual-stable", name: "Virtual Output", numericID: 71, isVirtual: true)
        let provider = FakeAudioDeviceProvider(outputs: [selected])
        let factory = FakeAudioPlayerFactory()
        let appState = makeAppState(provider: provider, factory: factory)
        XCTAssertTrue(appState.selectOutputDevice(id: selected.device.id))
        appState.setOutputFallbackPolicy(.systemOutput)

        provider.outputs = []
        provider.notifyChange()

        XCTAssertEqual(appState.settings.outputDeviceID, selected.device.id)
        XCTAssertEqual(appState.audioRouteStatus.kind, .fallbackSystemOutput)
        XCTAssertNil(appState.audioRouteStatus.activeDeviceID)
        XCTAssertNil(appState.playbackService.configuredOutputDeviceUID)

        let clip = try playableClip(name: "Fallback")
        appState.clips = [clip]
        appState.play(clip)
        await Task.yield()

        XCTAssertEqual(appState.audioRouteStatus.kind, .fallbackSystemOutput)
        XCTAssertEqual(appState.audioRouteStatus.activeDeviceID, AudioDevice.systemOutput.id)
        XCTAssertNil(factory.players.first?.currentOutputDeviceUID)

        let routeCallCount = factory.players[0].routeCalls.count
        appState.refreshAudioRouting()
        XCTAssertTrue(factory.players[0].isPlaying)
        XCTAssertEqual(factory.players[0].routeCalls.count, routeCallCount)
        XCTAssertEqual(appState.audioRouteStatus.activeDeviceID, AudioDevice.systemOutput.id)

        XCTAssertTrue(appState.selectOutputDevice(id: AudioDevice.systemOutput.id))
        XCTAssertTrue(factory.players[0].isPlaying)
        XCTAssertEqual(factory.players[0].routeCalls.count, routeCallCount)
        XCTAssertEqual(appState.settings.outputDeviceID, AudioDevice.systemOutput.id)
        XCTAssertEqual(appState.audioRouteStatus.kind, .systemOutput)
    }

    func testChangingActiveFallbackToStopAndWaitStopsPlayback() async throws {
        let selected = liveDevice(uid: "loss-policy", name: "Loss Policy Output", numericID: 79)
        let provider = FakeAudioDeviceProvider(outputs: [selected])
        let factory = FakeAudioPlayerFactory()
        let appState = makeAppState(provider: provider, factory: factory)
        XCTAssertTrue(appState.selectOutputDevice(id: selected.device.id))
        appState.setOutputFallbackPolicy(.systemOutput)
        provider.outputs = []
        provider.notifyChange()
        let clip = try playableClip(name: "Fallback Policy")
        appState.clips = [clip]
        appState.play(clip)
        await Task.yield()
        XCTAssertTrue(appState.playbackState.isPlaying)

        appState.setOutputFallbackPolicy(.stopAndWait)

        XCTAssertFalse(appState.playbackState.isPlaying)
        XCTAssertEqual(factory.players[0].stopCount, 1)
        XCTAssertEqual(appState.audioRouteStatus.kind, .unavailable)
        XCTAssertFalse(appState.audioRouteStatus.allowsPlayback)
    }

    func testExactUIDReconnectsWhenNumericIDAndNameChange() async throws {
        let initial = liveDevice(uid: "returning", name: "Old Name", numericID: 100)
        let provider = FakeAudioDeviceProvider(outputs: [initial])
        let appState = makeAppState(provider: provider, factory: FakeAudioPlayerFactory())
        XCTAssertTrue(appState.selectOutputDevice(id: initial.device.id))
        provider.outputs = []
        provider.notifyChange()
        XCTAssertEqual(appState.audioRouteStatus.kind, .unavailable)

        provider.outputs = [liveDevice(uid: "returning", name: "New Name", numericID: 777)]
        provider.notifyChange()
        await Task.yield()

        XCTAssertEqual(provider.outputDevice(forPersistentID: initial.device.id)?.audioDeviceID, 777)
        XCTAssertEqual(appState.settings.outputDeviceID, initial.device.id)
        XCTAssertEqual(appState.settings.outputDeviceName, "New Name")
        XCTAssertEqual(appState.playbackService.configuredOutputDeviceUID, "returning")
        XCTAssertEqual(appState.audioRouteStatus.kind, .ready)
    }

    func testRouteChangeFailureRollsBackAllUpdatedPlayersAndKeepsSetting() throws {
        let firstDevice = liveDevice(uid: "route-one", name: "Route One", numericID: 81)
        let secondDevice = liveDevice(uid: "route-two", name: "Route Two", numericID: 82)
        let provider = FakeAudioDeviceProvider(outputs: [firstDevice, secondDevice])
        let factory = FakeAudioPlayerFactory()
        let appState = makeAppState(provider: provider, factory: factory)
        let clips = try [playableClip(name: "First"), playableClip(name: "Second")]
        appState.clips = clips
        appState.play(clips)
        factory.players[1].rejectedUIDs.insert("route-two")

        XCTAssertFalse(appState.selectOutputDevice(id: secondDevice.device.id))

        XCTAssertEqual(appState.settings.outputDeviceID, AudioDevice.systemOutput.id)
        XCTAssertEqual(appState.audioRouteStatus.kind, .failed)
        XCTAssertTrue(factory.players.allSatisfy { $0.currentOutputDeviceUID == nil })
    }

    func testMultiplePlayersPauseResumeAndRouteChangesRemainIndependent() throws {
        let firstDevice = liveDevice(uid: "route-a", name: "Route A", numericID: 91)
        let secondDevice = liveDevice(uid: "route-b", name: "Route B", numericID: 92)
        let provider = FakeAudioDeviceProvider(outputs: [firstDevice, secondDevice])
        let factory = FakeAudioPlayerFactory()
        let appState = makeAppState(provider: provider, factory: factory)
        let first = try playableClip(name: "First")
        let second = try playableClip(name: "Second")
        appState.clips = [first, second]
        XCTAssertTrue(appState.selectOutputDevice(id: firstDevice.device.id))

        appState.play([first, second])
        XCTAssertEqual(factory.players[0].volume, 0.5, accuracy: 0.001)
        XCTAssertEqual(factory.players[1].volume, 0.5, accuracy: 0.001)
        appState.pause(first)
        XCTAssertTrue(appState.isPaused(first))
        XCTAssertTrue(appState.selectOutputDevice(id: secondDevice.device.id))

        XCTAssertEqual(factory.players.count, 2)
        XCTAssertTrue(factory.players.allSatisfy { $0.currentOutputDeviceUID == "route-b" })
        XCTAssertFalse(factory.players[0].isPlaying)
        XCTAssertTrue(factory.players[1].isPlaying)

        appState.resume(first)
        XCTAssertTrue(factory.players[0].isPlaying)
        appState.stop(second)
        XCTAssertEqual(factory.players[1].stopCount, 1)
        XCTAssertTrue(factory.players[0].isPlaying)
        XCTAssertEqual(factory.players[0].volume, 1, accuracy: 0.001)
    }

    func testDeviceRefreshDoesNotReapplyUnchangedRouteToActivePlayer() async throws {
        let selected = liveDevice(uid: "unchanged", name: "Stable Output", numericID: 98)
        let provider = FakeAudioDeviceProvider(outputs: [selected])
        let factory = FakeAudioPlayerFactory()
        let appState = makeAppState(provider: provider, factory: factory)
        XCTAssertTrue(appState.selectOutputDevice(id: selected.device.id))
        let clip = try playableClip(name: "Refresh")
        appState.clips = [clip]
        appState.play(clip)
        await Task.yield()
        let routeCallCount = factory.players[0].routeCalls.count

        appState.refreshAudioRouting()

        XCTAssertEqual(factory.players[0].routeCalls.count, routeCallCount)
        XCTAssertTrue(factory.players[0].isPlaying)
        XCTAssertEqual(appState.audioRouteStatus.kind, .explicitDevice)
    }

    func testPlaybackStartAndOutputRouteFailuresDoNotEnterPlayingState() throws {
        let clip = try playableClip(name: "Failure")
        var state = PlaybackState()
        let startFailureFactory = FakeAudioPlayerFactory()
        startFailureFactory.nextPlayerConfiguration = { $0.playSucceeds = false }
        let startFailureService = PlaybackService(playerFactory: startFailureFactory)

        let startResult = startFailureService.play(clip: clip, settings: CueletSettings(), playbackState: &state)
        XCTAssertFalse(startResult.didStart)
        XCTAssertFalse(state.isPlaying)

        let routeFailureFactory = FakeAudioPlayerFactory()
        routeFailureFactory.nextPlayerConfiguration = { $0.rejectedUIDs.insert("rejected") }
        let routeFailureService = PlaybackService(playerFactory: routeFailureFactory)
        _ = routeFailureService.applyOutputDeviceUID("rejected")

        let routeResult = routeFailureService.play(clip: clip, settings: CueletSettings(), playbackState: &state)
        XCTAssertFalse(routeResult.didStart)
        guard case .outputRoutingFailed = routeResult.error else {
            return XCTFail("Expected output routing failure")
        }
        XCTAssertFalse(state.isPlaying)
    }

    func testStopAllAndShutdownReleaseEveryPlayer() throws {
        let factory = FakeAudioPlayerFactory()
        let service = PlaybackService(playerFactory: factory)
        var state = PlaybackState()
        let clips = try [playableClip(name: "One"), playableClip(name: "Two")]
        for clip in clips {
            XCTAssertTrue(service.play(clip: clip, settings: CueletSettings(), playbackState: &state).didStart)
        }

        service.stopAll(playbackState: &state)

        XCTAssertFalse(state.isPlaying)
        XCTAssertTrue(factory.players.allSatisfy { $0.stopCount == 1 && $0.didFinish == nil })
    }

    func testStaleFinishCallbackCannotStopRapidReplayReplacement() async throws {
        let factory = FakeAudioPlayerFactory()
        let service = PlaybackService(playerFactory: factory)
        var state = PlaybackState()
        let clip = try playableClip(name: "Rapid")
        service.playbackDidFinish = { clipID in state.stop(clipID) }

        XCTAssertTrue(service.play(clip: clip, settings: CueletSettings(), playbackState: &state).didStart)
        let staleCallback = factory.players[0].didFinish
        XCTAssertTrue(service.play(clip: clip, settings: CueletSettings(), playbackState: &state).didStart)
        staleCallback?(nil)
        await Task.yield()

        XCTAssertTrue(state.playingClipIDs.contains(clip.id))
        XCTAssertTrue(factory.players[1].isPlaying)
    }

    private func makeAppState(
        provider: FakeAudioDeviceProvider,
        factory: FakeAudioPlayerFactory,
        settingsURL: URL? = nil
    ) -> AppState {
        AppState(
            settingsStore: SettingsStore(url: settingsURL ?? temporarySettingsURL()),
            playbackService: PlaybackService(playerFactory: factory),
            audioDeviceService: provider,
            globalShortcutService: FakeRoutingShortcutService(),
            installKeyboardShortcuts: false
        )
    }

    private func playableClip(name: String) throws -> SoundClip {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("\(UUID().uuidString)-\(name).wav")
        try Data([0]).write(to: url)
        return SoundClip(
            name: name,
            filename: url.lastPathComponent,
            category: .uncategorized,
            duration: 1,
            waveform: [],
            fileURL: url
        )
    }

    private func temporarySettingsURL() -> URL {
        FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString)
            .appendingPathExtension("json")
    }

    private func liveDevice(
        uid: String,
        name: String,
        numericID: AudioDeviceID,
        isVirtual: Bool = false,
        transport: String? = nil
    ) -> LiveAudioOutputDevice {
        LiveAudioOutputDevice(
            device: AudioDevice(
                id: AudioDevice.persistentID(forCoreAudioUID: uid),
                name: name,
                kind: .output,
                isDefault: false,
                isVirtual: isVirtual,
                transportName: transport
            ),
            audioDeviceID: numericID
        )
    }
}

@MainActor
private final class FakeAudioDeviceProvider: AudioDeviceProviding {
    var outputs: [LiveAudioOutputDevice]
    var inputs: [AudioDevice]
    var system: LiveAudioOutputDevice?
    private var changeHandler: (() -> Void)?

    init(
        outputs: [LiveAudioOutputDevice] = [],
        inputs: [AudioDevice] = [],
        system: LiveAudioOutputDevice? = nil
    ) {
        self.outputs = outputs
        self.inputs = inputs
        self.system = system
    }

    func outputDeviceSnapshots() -> [LiveAudioOutputDevice] { outputs }
    func inputDevices() -> [AudioDevice] { inputs }
    func systemOutputDevice() -> LiveAudioOutputDevice? { system }
    func outputDevice(forPersistentID persistentID: String) -> LiveAudioOutputDevice? {
        outputs.first { $0.device.id == persistentID }
    }
    func startObserving(_ handler: @escaping () -> Void) { changeHandler = handler }
    func stopObserving() { changeHandler = nil }
    func notifyChange() { changeHandler?() }
}

private enum FakeAudioError: LocalizedError {
    case rejected(String)
    case creation

    var errorDescription: String? {
        switch self {
        case .rejected(let uid): "Fake player rejected \(uid)."
        case .creation: "Fake player creation failed."
        }
    }
}

private final class FakeAudioPlayer: AudioPlayerBackend {
    var volume: Float = 1
    var currentTime: TimeInterval = 0
    var duration: TimeInterval = 1
    private(set) var isPlaying = false
    private(set) var currentOutputDeviceUID: String?
    var didFinish: ((Error?) -> Void)?
    var rejectedUIDs: Set<String> = []
    var playSucceeds = true
    private(set) var routeCalls: [String?] = []
    private(set) var stopCount = 0

    func setOutputDeviceUID(_ uid: String?) throws {
        routeCalls.append(uid)
        if let uid, rejectedUIDs.contains(uid) {
            throw FakeAudioError.rejected(uid)
        }
        currentOutputDeviceUID = uid
    }

    func prepareToPlay() -> Bool { true }

    func play() -> Bool {
        guard playSucceeds else { return false }
        isPlaying = true
        return true
    }

    func pause() { isPlaying = false }

    func stop() {
        isPlaying = false
        stopCount += 1
    }
}

private final class FakeAudioPlayerFactory: AudioPlayerBackendCreating {
    var players: [FakeAudioPlayer] = []
    var shouldFailCreation = false
    var nextPlayerConfiguration: ((FakeAudioPlayer) -> Void)?

    func makePlayer(contentsOf url: URL) throws -> AudioPlayerBackend {
        if shouldFailCreation { throw FakeAudioError.creation }
        let player = FakeAudioPlayer()
        nextPlayerConfiguration?(player)
        players.append(player)
        return player
    }
}

private final class FakeRoutingShortcutService: GlobalShortcutRegistering {
    var registeredClipIDs: Set<SoundClip.ID> = []
    var lastErrorMessage: String?
    func setHandler(_ handler: @escaping (SoundClip.ID) -> Void) {}
    func tryUpdate(_ assignments: [GlobalShortcutAssignment]) -> Result<Void, GlobalShortcutRegistrationError> {
        .success(())
    }
    func unregisterAll() { registeredClipIDs.removeAll() }
}
