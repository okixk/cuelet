import XCTest
@testable import Cuelet

@MainActor
final class AudioRoutingLiveIntegrationTests: XCTestCase {
    func testInstalledOutputsAcceptSystemAndExplicitUIDRoutingWithoutChangingDefault() throws {
        guard ProcessInfo.processInfo.environment["CUELET_RUN_LIVE_AUDIO_TESTS"] == "1" else {
            throw XCTSkip("Set CUELET_RUN_LIVE_AUDIO_TESTS=1 to exercise installed Core Audio outputs with a silent fixture.")
        }

        let provider = AudioDeviceService()
        let systemBefore = try XCTUnwrap(provider.systemOutputDevice())
        let outputs = provider.outputDeviceSnapshots()
        XCTAssertFalse(outputs.isEmpty)

        let fixtureURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("cuelet-live-routing-\(UUID().uuidString).wav")
        try silentWAVData(duration: 0.08).write(to: fixtureURL)
        defer { try? FileManager.default.removeItem(at: fixtureURL) }

        let systemPlayer = try AVAudioPlayerBackend(contentsOf: fixtureURL)
        try systemPlayer.setOutputDeviceUID(nil)
        XCTAssertNil(systemPlayer.currentOutputDeviceUID)
        XCTAssertTrue(systemPlayer.prepareToPlay())
        XCTAssertTrue(systemPlayer.play())
        systemPlayer.stop()

        for output in outputs {
            let uid = try XCTUnwrap(output.device.coreAudioUID)
            let resolved = try XCTUnwrap(provider.outputDevice(forPersistentID: output.device.id))
            XCTAssertEqual(resolved.device.coreAudioUID, uid)

            let player = try AVAudioPlayerBackend(contentsOf: fixtureURL)
            try player.setOutputDeviceUID(uid)
            XCTAssertEqual(player.currentOutputDeviceUID, uid)
            XCTAssertTrue(player.prepareToPlay())
            XCTAssertTrue(player.play())
            player.stop()
        }

        let systemAfter = try XCTUnwrap(provider.systemOutputDevice())
        XCTAssertEqual(systemAfter.audioDeviceID, systemBefore.audioDeviceID)
        XCTAssertEqual(systemAfter.device.coreAudioUID, systemBefore.device.coreAudioUID)
    }

    private func silentWAVData(duration: TimeInterval) -> Data {
        let sampleRate: UInt32 = 44_100
        let bitsPerSample: UInt16 = 16
        let channels: UInt16 = 1
        let sampleCount = UInt32(Double(sampleRate) * duration)
        let blockAlign = channels * bitsPerSample / 8
        let byteRate = sampleRate * UInt32(blockAlign)
        let dataSize = sampleCount * UInt32(blockAlign)

        var data = Data()
        data.append(contentsOf: "RIFF".utf8)
        data.appendLittleEndian(36 + dataSize)
        data.append(contentsOf: "WAVEfmt ".utf8)
        data.appendLittleEndian(UInt32(16))
        data.appendLittleEndian(UInt16(1))
        data.appendLittleEndian(channels)
        data.appendLittleEndian(sampleRate)
        data.appendLittleEndian(byteRate)
        data.appendLittleEndian(blockAlign)
        data.appendLittleEndian(bitsPerSample)
        data.append(contentsOf: "data".utf8)
        data.appendLittleEndian(dataSize)
        data.append(Data(repeating: 0, count: Int(dataSize)))
        return data
    }
}

private extension Data {
    mutating func appendLittleEndian<T: FixedWidthInteger>(_ value: T) {
        var littleEndian = value.littleEndian
        Swift.withUnsafeBytes(of: &littleEndian) { append(contentsOf: $0) }
    }
}
