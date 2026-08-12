import Foundation
import XCTest
@testable import Cuelet

final class LibraryPerformanceTests: XCTestCase {
    func testLargeLibraryScanReloadAndSearch() throws {
        guard ProcessInfo.processInfo.environment["CUELET_RUN_PERFORMANCE_TESTS"] == "1" else {
            throw XCTSkip("Set CUELET_RUN_PERFORMANCE_TESTS=1 to run the 250/1,000-file benchmark.")
        }

        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("cuelet-performance-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let fixture = silentWAVData(duration: 0.01)
        try writeFixtures(in: root, range: 0..<250, data: fixture)

        let service = LibraryService()
        let scan250Start = ContinuousClock.now
        let clips250 = try service.scanLibrary(at: root, scansSubfolders: true)
        let scan250 = scan250Start.duration(to: .now)
        XCTAssertEqual(clips250.count, 250)

        try writeFixtures(in: root, range: 250..<1_000, data: fixture)
        let scan1000Start = ContinuousClock.now
        let clips1000 = try service.scanLibrary(at: root, scansSubfolders: true)
        let scan1000 = scan1000Start.duration(to: .now)
        XCTAssertEqual(clips1000.count, 1_000)

        let store = LibraryMetadataStore(libraryURL: root)
        try store.save(store.document(from: clips1000, categories: []))
        let document = try XCTUnwrap(loadedDocument(from: store.load()))
        let reloadStart = ContinuousClock.now
        let reloaded = try service.loadLibrary(at: root, scansSubfolders: true, metadata: document)
        let reload1000 = reloadStart.duration(to: .now)
        XCTAssertEqual(reloaded.count, 1_000)

        let searchStart = ContinuousClock.now
        let result = SearchService().filter(clips: reloaded, searchText: "fixture 0999", filter: .all)
        let search1000 = searchStart.duration(to: .now)
        XCTAssertEqual(result.map(\.displayName), ["Fixture 0999"])

        print(
            "CUELET_PERF scan_250_ms=\(milliseconds(scan250)) "
                + "scan_1000_ms=\(milliseconds(scan1000)) "
                + "metadata_reload_1000_ms=\(milliseconds(reload1000)) "
                + "search_1000_ms=\(milliseconds(search1000))"
        )

        XCTAssertLessThan(milliseconds(scan250), 15_000)
        XCTAssertLessThan(milliseconds(scan1000), 30_000)
        XCTAssertLessThan(milliseconds(reload1000), 30_000)
        XCTAssertLessThan(milliseconds(search1000), 1_000)
    }

    private func writeFixtures(in root: URL, range: Range<Int>, data: Data) throws {
        for index in range {
            let name = String(format: "Fixture %04d.wav", index)
            try data.write(to: root.appendingPathComponent(name))
        }
    }

    private func loadedDocument(from result: LibraryMetadataStore.LoadResult) -> LibraryMetadataDocument? {
        switch result {
        case .loaded(let document, _), .recovered(let document, _):
            document
        case .missing, .failure:
            nil
        }
    }

    private func milliseconds(_ duration: Duration) -> Double {
        let components = duration.components
        return Double(components.seconds) * 1_000
            + Double(components.attoseconds) / 1_000_000_000_000_000
    }

    private func silentWAVData(duration: TimeInterval) -> Data {
        let sampleRate: UInt32 = 44_100
        let bitsPerSample: UInt16 = 16
        let channelCount: UInt16 = 1
        let sampleCount = UInt32(Double(sampleRate) * duration)
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
