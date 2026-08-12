import XCTest
@testable import Cuelet

final class SettingsStoreTests: XCTestCase {
    func testPersistsViewModeAndCategoryColors() {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString)
            .appendingPathExtension("json")
        let store = SettingsStore(url: url)
        var settings = CueletSettings()
        settings.viewMode = .list
        let effectsID = SoundCategory.makeUserCategory(named: "Effects").id
        settings.categoryColorHexes[effectsID] = "#4F8A8B"
        settings.keepsRunningAfterWindowClose = true
        settings.showsMenuBarItem = true
        settings.soundboardVolume = 0.42

        store.save(settings)

        XCTAssertEqual(store.load().viewMode, .list)
        XCTAssertEqual(store.load().categoryColorHexes[effectsID], "#4F8A8B")
        XCTAssertTrue(store.load().keepsRunningAfterWindowClose)
        XCTAssertTrue(store.load().showsMenuBarItem)
        XCTAssertEqual(store.load().soundboardVolume, 0.42)
    }

    func testCorruptSettingsAreNotSilentlyOverwritten() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let url = directory.appendingPathComponent("settings.json")
        let corrupt = Data("{not settings".utf8)
        try corrupt.write(to: url)
        let store = SettingsStore(url: url)

        guard case .failure(let message) = store.loadResult() else {
            return XCTFail("Expected explicit settings failure")
        }
        XCTAssertTrue(message.contains("left untouched"))
        XCTAssertEqual(try Data(contentsOf: url), corrupt)
    }

    func testSettingsRecoveryCopyIsUsedAfterCorruption() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let store = SettingsStore(url: directory.appendingPathComponent("settings.json"))
        var first = CueletSettings()
        first.viewMode = .list
        var second = first
        second.viewMode = .grid
        XCTAssertTrue(store.save(first))
        XCTAssertTrue(store.save(second))
        try Data("broken".utf8).write(to: store.url)

        guard case .recovered(let recovered, let primaryError) = store.loadResult() else {
            return XCTFail("Expected settings recovery")
        }
        XCTAssertFalse(primaryError.isEmpty)
        XCTAssertEqual(recovered.viewMode, .list)
    }
}
