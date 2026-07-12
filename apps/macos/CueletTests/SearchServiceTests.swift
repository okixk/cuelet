import XCTest
@testable import Cuelet

final class SearchServiceTests: XCTestCase {
    func testSearchRankingPromotesExactPrefixBeforeOtherMatches() {
        let clips = [
            SoundClip(name: "Door Close", filename: "door-close.wav", category: .effects, duration: 1, waveform: []),
            SoundClip(name: "Soft Door Knock", filename: "soft-door-knock.wav", category: .effects, duration: 1, waveform: []),
            SoundClip(name: "Door Knock", filename: "door-knock.wav", category: .effects, duration: 1, waveform: [])
        ]

        let results = SearchService().filter(clips: clips, searchText: "door knock", filter: .all)

        XCTAssertEqual(results.map(\.name), ["Door Knock", "Soft Door Knock"])
    }

    @MainActor
    func testSearchEnterPlaysSelectedVisibleResultOrTopResult() throws {
        let root = try makeTemporaryDirectory()
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        appState.clips = [
            try makeAudioClip(named: "Rain", filename: "rain.wav", category: .ambience, in: root),
            try makeAudioClip(named: "Door Knock", filename: "door-knock.wav", category: .effects, in: root),
            try makeAudioClip(named: "Soft Door Knock", filename: "soft-door-knock.wav", category: .effects, in: root)
        ]
        appState.searchText = "door knock"

        XCTAssertTrue(appState.playRecommendedSearchResult())
        XCTAssertEqual(appState.playbackState.lastPlayedClipID, appState.visibleClips.first?.id)

        let selected = appState.visibleClips[1]
        appState.select(selected)

        XCTAssertTrue(appState.playRecommendedSearchResult())
        XCTAssertEqual(appState.playbackState.lastPlayedClipID, selected.id)
        appState.stopAllPlayback()
    }

    private func temporarySettingsURL() -> URL {
        FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString)
            .appendingPathExtension("json")
    }

    private func makeTemporaryDirectory() throws -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    private func makeAudioClip(named name: String, filename: String, category: SoundCategory, in directory: URL) throws -> SoundClip {
        let url = directory.appendingPathComponent(filename)
        try silentWAVData().write(to: url)
        return SoundClip(
            name: name,
            filename: filename,
            category: category,
            duration: 1,
            waveform: [],
            fileURL: url
        )
    }

    private func silentWAVData() -> Data {
        let sampleRate: UInt32 = 44_100
        let bitsPerSample: UInt16 = 16
        let channelCount: UInt16 = 1
        let sampleCount: UInt32 = sampleRate
        let blockAlign = channelCount * bitsPerSample / 8
        let byteRate = sampleRate * UInt32(blockAlign)
        let dataSize = sampleCount * UInt32(blockAlign)

        var data = Data()
        data.append(contentsOf: "RIFF".utf8)
        data.appendLittleEndian(36 + dataSize)
        data.append(contentsOf: "WAVE".utf8)
        data.append(contentsOf: "fmt ".utf8)
        data.appendLittleEndian(UInt32(16))
        data.appendLittleEndian(UInt16(1))
        data.appendLittleEndian(channelCount)
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
        var littleEndianValue = value.littleEndian
        Swift.withUnsafeBytes(of: &littleEndianValue) { bytes in
            append(contentsOf: bytes)
        }
    }
}
