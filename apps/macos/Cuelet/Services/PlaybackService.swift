import AVFoundation
import AppKit
import Foundation

protocol AudioPlayerBackend: AnyObject {
    var volume: Float { get set }
    var currentTime: TimeInterval { get set }
    var duration: TimeInterval { get }
    var isPlaying: Bool { get }
    var currentOutputDeviceUID: String? { get }
    var didFinish: ((Error?) -> Void)? { get set }

    func setOutputDeviceUID(_ uid: String?) throws
    @discardableResult func prepareToPlay() -> Bool
    @discardableResult func play() -> Bool
    func pause()
    func stop()
}

protocol AudioPlayerBackendCreating {
    func makePlayer(contentsOf url: URL) throws -> AudioPlayerBackend
}

enum AudioPlayerBackendError: LocalizedError {
    case routeRejected(String)
    case playbackEndedUnsuccessfully

    var errorDescription: String? {
        switch self {
        case .routeRejected(let uid):
            "The audio player did not accept Core Audio device UID \(uid)."
        case .playbackEndedUnsuccessfully:
            "The audio player stopped before reaching the end of the file."
        }
    }
}

final class AVAudioPlayerBackend: NSObject, AudioPlayerBackend, AVAudioPlayerDelegate {
    private let player: AVAudioPlayer
    var didFinish: ((Error?) -> Void)?

    init(contentsOf url: URL) throws {
        player = try AVAudioPlayer(contentsOf: url)
        super.init()
        player.delegate = self
    }

    var volume: Float {
        get { player.volume }
        set { player.volume = newValue }
    }

    var currentTime: TimeInterval {
        get { player.currentTime }
        set { player.currentTime = newValue }
    }

    var duration: TimeInterval { player.duration }
    var isPlaying: Bool { player.isPlaying }
    var currentOutputDeviceUID: String? { player.currentDevice }

    func setOutputDeviceUID(_ uid: String?) throws {
        player.currentDevice = uid
        if let uid, player.currentDevice != uid {
            throw AudioPlayerBackendError.routeRejected(uid)
        }
    }

    func prepareToPlay() -> Bool { player.prepareToPlay() }
    func play() -> Bool { player.play() }
    func pause() { player.pause() }
    func stop() { player.stop() }

    func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
        didFinish?(flag ? nil : AudioPlayerBackendError.playbackEndedUnsuccessfully)
    }

    func audioPlayerDecodeErrorDidOccur(_ player: AVAudioPlayer, error: Error?) {
        didFinish?(error)
    }
}

struct AVAudioPlayerBackendFactory: AudioPlayerBackendCreating {
    func makePlayer(contentsOf url: URL) throws -> AudioPlayerBackend {
        try AVAudioPlayerBackend(contentsOf: url)
    }
}

final class PlaybackService {
    struct Progress: Equatable {
        let position: TimeInterval
        let duration: TimeInterval

        var fraction: Double {
            guard duration > 0 else { return 0 }
            return min(max(position / duration, 0), 1)
        }
    }
    struct PlaybackResult {
        let didStart: Bool
        let error: PlaybackError?

        static func started() -> PlaybackResult {
            PlaybackResult(didStart: true, error: nil)
        }

        static func failed(_ error: PlaybackError) -> PlaybackResult {
            PlaybackResult(didStart: false, error: error)
        }
    }

    enum PlaybackError: LocalizedError {
        case missingFileURL(SoundClip)
        case fileUnavailable(URL)
        case playerCreationFailed(URL, Error)
        case playerCouldNotStart(URL)
        case markedMissing(SoundClip)
        case outputRoutingFailed(String)

        var errorDescription: String? {
            switch self {
            case .missingFileURL(let clip):
                "Cuelet cannot play “\(clip.displayName)” because it is not linked to an audio file on disk."
            case .fileUnavailable(let url):
                "Cuelet could not find “\(url.lastPathComponent)” on disk. Rescan the library and try again."
            case .playerCreationFailed(let url, let error):
                "Cuelet could not open “\(url.lastPathComponent)”: \(error.localizedDescription)"
            case .playerCouldNotStart(let url):
                "Cuelet could not start playback for “\(url.lastPathComponent)”."
            case .markedMissing(let clip):
                "Cuelet cannot play “\(clip.displayName)” because its source is missing or changed. Locate or relink it first."
            case .outputRoutingFailed(let message):
                "Cuelet could not apply the selected output route: \(message)"
            }
        }
    }

    enum OutputRouteError: LocalizedError, Equatable {
        case playerRejected(String)

        var errorDescription: String? {
            switch self {
            case .playerRejected(let message): message
            }
        }
    }

    private let playerFactory: AudioPlayerBackendCreating
    private var players: [SoundClip.ID: AudioPlayerBackend] = [:]
    private var playerGenerations: [SoundClip.ID: UInt64] = [:]
    private var securityScopedURLs: [SoundClip.ID: URL] = [:]
    private var configuredVolume: Float = 1
    private(set) var configuredOutputDeviceUID: String?
    var playbackDidFinish: (@MainActor (SoundClip.ID) -> Void)?
    var outputRouteDidConfirm: (@MainActor (_ requestedUID: String?, _ actualUID: String?) -> Void)?

    init(playerFactory: AudioPlayerBackendCreating = AVAudioPlayerBackendFactory()) {
        self.playerFactory = playerFactory
    }

    deinit {
        stopAllPlayers()
    }

    @discardableResult
    func play(clip: SoundClip, settings: CueletSettings, playbackState: inout PlaybackState) -> PlaybackResult {
        guard !clip.isMissing else {
            let error = PlaybackError.markedMissing(clip)
            NSLog("%@", error.localizedDescription)
            NSSound.beep()
            return .failed(error)
        }
        guard let fileURL = clip.fileURL else {
            let error = PlaybackError.missingFileURL(clip)
            NSLog("%@", error.localizedDescription)
            NSSound.beep()
            return .failed(error)
        }

        guard FileManager.default.fileExists(atPath: fileURL.path) else {
            let error = PlaybackError.fileUnavailable(fileURL)
            NSLog("%@", error.localizedDescription)
            NSSound.beep()
            return .failed(error)
        }

        let didStartSecurityScope = clip.storageMode == .linked && fileURL.startAccessingSecurityScopedResource()
        let player: AudioPlayerBackend
        do {
            player = try playerFactory.makePlayer(contentsOf: fileURL)
        } catch {
            if didStartSecurityScope { fileURL.stopAccessingSecurityScopedResource() }
            let playbackError = PlaybackError.playerCreationFailed(fileURL, error)
            NSLog("%@", playbackError.localizedDescription)
            NSSound.beep()
            return .failed(playbackError)
        }

        do {
            try player.setOutputDeviceUID(configuredOutputDeviceUID)
            configuredVolume = Float(min(max(settings.soundboardVolume, 0), 1))
            let prospectivePlayerCount: Int
            if settings.allowsSimultaneousPlayback {
                prospectivePlayerCount = players[clip.id] == nil ? players.count + 1 : players.count
            } else {
                prospectivePlayerCount = 1
            }
            player.volume = configuredVolume / Float(max(prospectivePlayerCount, 1))
            _ = player.prepareToPlay()
        } catch {
            if didStartSecurityScope { fileURL.stopAccessingSecurityScopedResource() }
            let playbackError = PlaybackError.outputRoutingFailed(error.localizedDescription)
            NSLog("%@", playbackError.localizedDescription)
            NSSound.beep()
            return .failed(playbackError)
        }

        let startedAt = Date()
        guard player.play() else {
            if didStartSecurityScope { fileURL.stopAccessingSecurityScopedResource() }
            let error = PlaybackError.playerCouldNotStart(fileURL)
            NSLog("%@", error.localizedDescription)
            NSSound.beep()
            return .failed(error)
        }

        if !settings.allowsSimultaneousPlayback {
            stopAllPlayers()
            playbackState.stopAll()
        } else if let currentPlayer = players[clip.id] {
            currentPlayer.didFinish = nil
            currentPlayer.stop()
            players[clip.id] = nil
            stopSecurityScope(for: clip.id)
            playbackState.stop(clip.id)
        }

        let generation = (playerGenerations[clip.id] ?? 0) &+ 1
        playerGenerations[clip.id] = generation
        player.didFinish = { [weak self] _ in
            self?.finish(clipID: clip.id, generation: generation)
        }
        players[clip.id] = player
        if didStartSecurityScope { securityScopedURLs[clip.id] = fileURL }
        updatePlayerVolumes()
        playbackState.markPlaying(clip.id, startedAt: startedAt)
        confirmOutputRoute(using: player)
        return .started()
    }

    func stop(clip: SoundClip, playbackState: inout PlaybackState) {
        if let player = players[clip.id] {
            player.didFinish = nil
            player.stop()
        }
        players[clip.id] = nil
        stopSecurityScope(for: clip.id)
        updatePlayerVolumes()
        playbackState.stop(clip.id)
    }

    func pause(clip: SoundClip, playbackState: inout PlaybackState) {
        guard let player = players[clip.id], player.isPlaying else { return }
        player.pause()
        playbackState.pause(clip.id)
    }

    @discardableResult
    func resume(clip: SoundClip, playbackState: inout PlaybackState) -> Bool {
        guard let player = players[clip.id], playbackState.isPaused(clip.id) else { return false }
        guard player.play() else { return false }
        playbackState.resume(clip.id)
        return true
    }

    func stopAll(playbackState: inout PlaybackState) {
        stopAllPlayers()
        playbackState.stopAll()
    }

    func progress(for clipID: SoundClip.ID) -> Progress? {
        guard let player = players[clipID] else { return nil }
        return Progress(position: player.currentTime, duration: player.duration)
    }

    func setVolume(_ volume: Double) {
        configuredVolume = Float(min(max(volume, 0), 1))
        updatePlayerVolumes()
    }

    @discardableResult
    func applyOutputDeviceUID(_ uid: String?) -> Result<Void, OutputRouteError> {
        let previousUID = configuredOutputDeviceUID
        var updatedPlayers: [AudioPlayerBackend] = []
        do {
            for player in players.values {
                try player.setOutputDeviceUID(uid)
                updatedPlayers.append(player)
            }
        } catch {
            for player in updatedPlayers {
                try? player.setOutputDeviceUID(previousUID)
            }
            return .failure(.playerRejected(error.localizedDescription))
        }

        configuredOutputDeviceUID = uid
        if let player = players.values.first {
            confirmOutputRoute(using: player)
        }
        return .success(())
    }

    private func finish(clipID: SoundClip.ID, generation: UInt64) {
        guard playerGenerations[clipID] == generation else { return }
        players[clipID]?.didFinish = nil
        players[clipID] = nil
        stopSecurityScope(for: clipID)
        updatePlayerVolumes()

        Task { @MainActor [playbackDidFinish] in
            playbackDidFinish?(clipID)
        }
    }

    private func stopAllPlayers() {
        players.values.forEach { player in
            player.didFinish = nil
            player.stop()
        }
        players.removeAll()
        securityScopedURLs.values.forEach { $0.stopAccessingSecurityScopedResource() }
        securityScopedURLs.removeAll()
    }

    private func stopSecurityScope(for clipID: SoundClip.ID) {
        securityScopedURLs.removeValue(forKey: clipID)?.stopAccessingSecurityScopedResource()
    }

    private func updatePlayerVolumes() {
        let perPlayerVolume = configuredVolume / Float(max(players.count, 1))
        players.values.forEach { $0.volume = perPlayerVolume }
    }

    private func confirmOutputRoute(using player: AudioPlayerBackend) {
        let requestedUID = configuredOutputDeviceUID
        let actualUID = player.currentOutputDeviceUID
        Task { @MainActor [outputRouteDidConfirm] in
            outputRouteDidConfirm?(requestedUID, actualUID)
        }
    }
}

struct AudioPermissionService {
    var hasMicrophoneUsageDescription: Bool {
        Bundle.main.object(forInfoDictionaryKey: "NSMicrophoneUsageDescription") != nil
    }

    func microphonePermissionState() -> MicrophonePermissionState {
        guard hasMicrophoneUsageDescription else { return .missingUsageDescription }
        return MicrophonePermissionState(status: AVCaptureDevice.authorizationStatus(for: .audio))
    }

    func requestMicrophonePermission() async -> MicrophonePermissionState {
        guard hasMicrophoneUsageDescription else { return .missingUsageDescription }
        let granted = await AVCaptureDevice.requestAccess(for: .audio)
        return granted ? .authorized : MicrophonePermissionState(status: AVCaptureDevice.authorizationStatus(for: .audio))
    }
}

@MainActor
final class MicrophoneService {
    private let engine = AVAudioEngine()
    private var isMonitoring = false

    func startMonitoring(levelHandler: @escaping @MainActor (InputLevelState) -> Void) throws {
        guard !isMonitoring else { return }

        let inputNode = engine.inputNode
        let format = inputNode.outputFormat(forBus: 0)
        inputNode.installTap(onBus: 0, bufferSize: 512, format: format) { buffer, _ in
            let level = Self.levelState(from: buffer)
            Task { @MainActor in
                levelHandler(level)
            }
        }

        engine.prepare()
        try engine.start()
        isMonitoring = true
        levelHandler(InputLevelState(averagePower: 0, peakPower: 0, isMonitoring: true))
    }

    func stopMonitoring(levelHandler: @escaping @MainActor (InputLevelState) -> Void) {
        guard isMonitoring else { return }
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        isMonitoring = false
        levelHandler(.inactive)
    }

    private static func levelState(from buffer: AVAudioPCMBuffer) -> InputLevelState {
        guard let channelData = buffer.floatChannelData else { return .inactive }

        let frameLength = Int(buffer.frameLength)
        var peak: Float = 0
        var sum: Float = 0

        for channelIndex in 0..<Int(buffer.format.channelCount) {
            let samples = channelData[channelIndex]
            for frameIndex in 0..<frameLength {
                let sample = abs(samples[frameIndex])
                peak = max(peak, sample)
                sum += sample * sample
            }
        }

        let sampleCount = max(frameLength * Int(buffer.format.channelCount), 1)
        let rms = sqrt(sum / Float(sampleCount))
        return InputLevelState(
            averagePower: min(max(Double(rms), 0), 1),
            peakPower: min(max(Double(peak), 0), 1),
            isMonitoring: true
        )
    }
}
