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
        settings.categoryColorHexes[SoundCategory.effects.id] = "#4F8A8B"
        settings.keepsRunningAfterWindowClose = true
        settings.showsMenuBarItem = true
        settings.soundboardVolume = 0.42

        store.save(settings)

        XCTAssertEqual(store.load().viewMode, .list)
        XCTAssertEqual(store.load().categoryColorHexes[SoundCategory.effects.id], "#4F8A8B")
        XCTAssertTrue(store.load().keepsRunningAfterWindowClose)
        XCTAssertTrue(store.load().showsMenuBarItem)
        XCTAssertEqual(store.load().soundboardVolume, 0.42)
    }
}
