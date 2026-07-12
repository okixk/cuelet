import XCTest
@testable import Cuelet

final class CategoryMetadataTests: XCTestCase {
    func testPortableIconIDsMapToSFSymbols() {
        XCTAssertEqual(SoundCategory.systemImage(for: "music-note"), "music.note")
        XCTAssertEqual(SoundCategory.systemImage(for: "audio-speakers"), "speaker.wave.2")
        XCTAssertEqual(SoundCategory.systemImage(for: "chat-message"), "bubble.left")
        XCTAssertEqual(SoundCategory.systemImage(for: "face-smile"), "face.smiling")
    }

    func testCategoryPersistsPortableIconIDAndMigratesSFSymbolAlias() throws {
        let category = SoundCategory(
            id: "weather",
            name: "Weather",
            defaultColorHex: "#009688",
            iconID: "weather-showers"
        )
        let data = try JSONEncoder().encode(category)
        let json = try XCTUnwrap(String(data: data, encoding: .utf8))
        XCTAssertTrue(json.contains("\"iconID\":\"weather-showers\""))
        XCTAssertFalse(json.contains("systemImage"))

        let legacy = "{\"id\":\"legacy\",\"name\":\"Music\",\"defaultColorHex\":\"#3478F6\",\"systemImage\":\"music.note\",\"isUserEditable\":true}"
        let decoded = try JSONDecoder().decode(SoundCategory.self, from: Data(legacy.utf8))
        XCTAssertEqual(decoded.iconID, "music-note")
        XCTAssertEqual(decoded.systemImage, "music.note")
    }
}
