import AVFoundation
import AppKit
import CoreAudio
import Foundation

final class PlaybackService: NSObject, AVAudioPlayerDelegate {
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
            }
        }
    }

    private var players: [SoundClip.ID: AVAudioPlayer] = [:]
    private var playerClipIDs: [ObjectIdentifier: SoundClip.ID] = [:]
    var playbackDidFinish: (@MainActor (SoundClip.ID) -> Void)?

    @discardableResult
    func play(clip: SoundClip, settings: CueletSettings, playbackState: inout PlaybackState) -> PlaybackResult {
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

        let player: AVAudioPlayer
        do {
            player = try AVAudioPlayer(contentsOf: fileURL)
            player.delegate = self
            player.volume = Float(min(max(settings.soundboardVolume, 0), 1))
            player.prepareToPlay()
        } catch {
            let playbackError = PlaybackError.playerCreationFailed(fileURL, error)
            NSLog("%@", playbackError.localizedDescription)
            NSSound.beep()
            return .failed(playbackError)
        }

        let startedAt = Date()
        guard player.play() else {
            let error = PlaybackError.playerCouldNotStart(fileURL)
            NSLog("%@", error.localizedDescription)
            NSSound.beep()
            return .failed(error)
        }

        if !settings.allowsSimultaneousPlayback {
            stopAllPlayers()
            playbackState.stopAll()
        } else if let currentPlayer = players[clip.id] {
            playerClipIDs[ObjectIdentifier(currentPlayer)] = nil
            currentPlayer.stop()
            players[clip.id] = nil
            playbackState.stop(clip.id)
        }

        players[clip.id] = player
        playerClipIDs[ObjectIdentifier(player)] = clip.id
        playbackState.markPlaying(clip.id, startedAt: startedAt)
        return .started()
    }

    func stop(clip: SoundClip, playbackState: inout PlaybackState) {
        if let player = players[clip.id] {
            playerClipIDs[ObjectIdentifier(player)] = nil
            player.stop()
        }
        players[clip.id] = nil
        playbackState.stop(clip.id)
    }

    func stopAll(playbackState: inout PlaybackState) {
        stopAllPlayers()
        playbackState.stopAll()
    }

    func progress(for clipID: SoundClip.ID) -> Progress? {
        guard let player = players[clipID], player.isPlaying else { return nil }
        return Progress(position: player.currentTime, duration: player.duration)
    }

    func setVolume(_ volume: Double) {
        let normalizedVolume = Float(min(max(volume, 0), 1))
        players.values.forEach { $0.volume = normalizedVolume }
    }

    func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
        finish(player)
    }

    func audioPlayerDecodeErrorDidOccur(_ player: AVAudioPlayer, error: Error?) {
        finish(player)
    }

    private func finish(_ player: AVAudioPlayer) {
        let objectID = ObjectIdentifier(player)
        guard let clipID = playerClipIDs[objectID] else { return }
        playerClipIDs[objectID] = nil
        players[clipID] = nil

        Task { @MainActor [playbackDidFinish] in
            playbackDidFinish?(clipID)
        }
    }

    private func stopAllPlayers() {
        players.values.forEach { $0.stop() }
        playerClipIDs.removeAll()
        players.removeAll()
    }
}

struct AudioDeviceService {
    func outputDevices() -> [AudioDevice] {
        [AudioDevice.systemOutput] + coreAudioDevices(scope: kAudioDevicePropertyScopeOutput)
    }

    func inputDevices() -> [AudioDevice] {
        let captureDevices = AVCaptureDevice.DiscoverySession(
            deviceTypes: [.microphone],
            mediaType: .audio,
            position: .unspecified
        ).devices.map { device in
            AudioDevice(
                id: device.uniqueID,
                name: device.localizedName,
                kind: .input,
                isDefault: false,
                isVirtual: isLikelyVirtualDevice(named: device.localizedName)
            )
        }

        if captureDevices.isEmpty {
            return coreAudioDevices(scope: kAudioDevicePropertyScopeInput)
        }

        return captureDevices
    }

    private func coreAudioDevices(scope: AudioObjectPropertyScope) -> [AudioDevice] {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var dataSize: UInt32 = 0

        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &propertyAddress, 0, nil, &dataSize) == noErr else {
            return []
        }

        let deviceCount = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var deviceIDs = Array(repeating: AudioDeviceID(), count: deviceCount)

        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &propertyAddress, 0, nil, &dataSize, &deviceIDs) == noErr else {
            return []
        }

        return deviceIDs.compactMap { deviceID in
            guard deviceHasStreams(deviceID, scope: scope), let name = deviceName(deviceID) else { return nil }
            return AudioDevice(
                id: "coreaudio-\(deviceID)",
                name: name,
                kind: scope == kAudioDevicePropertyScopeInput ? .input : .output,
                isDefault: false,
                isVirtual: isLikelyVirtualDevice(named: name)
            )
        }
    }

    private func deviceHasStreams(_ deviceID: AudioDeviceID, scope: AudioObjectPropertyScope) -> Bool {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreams,
            mScope: scope,
            mElement: kAudioObjectPropertyElementMain
        )
        var dataSize: UInt32 = 0
        return AudioObjectGetPropertyDataSize(deviceID, &propertyAddress, 0, nil, &dataSize) == noErr && dataSize > 0
    }

    private func deviceName(_ deviceID: AudioDeviceID) -> String? {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioObjectPropertyName,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var name: CFString = "" as CFString
        var dataSize = UInt32(MemoryLayout<CFString>.size)
        let status = withUnsafeMutablePointer(to: &name) { namePointer in
            AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nil, &dataSize, namePointer)
        }

        guard status == noErr else { return nil }
        return name as String
    }

    private func isLikelyVirtualDevice(named name: String) -> Bool {
        let lowercasedName = name.lowercased()
        return lowercasedName.contains("blackhole")
            || lowercasedName.contains("loopback")
            || lowercasedName.contains("soundflower")
            || lowercasedName.contains("virtual")
            || lowercasedName.contains("aggregate")
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
