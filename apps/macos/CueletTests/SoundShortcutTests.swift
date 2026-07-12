import AppKit
import XCTest
@testable import Cuelet

final class SoundShortcutTests: XCTestCase {
    func testDisplayLabelUsesMacModifierSymbolsAndUppercaseCharacters() {
        XCTAssertEqual(
            SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift]).displayLabel,
            "⌘⇧X"
        )
        XCTAssertEqual(
            SoundShortcut(keyCode: 18, characters: "1", modifiers: [.option]).displayLabel,
            "⌥1"
        )
        XCTAssertEqual(
            SoundShortcut(keyCode: 1, characters: "s", modifiers: [.control, .option]).displayLabel,
            "⌃⌥S"
        )
    }

    func testDisplayLabelUsesSpecialKeyNames() {
        XCTAssertEqual(SoundShortcut(keyCode: 36, characters: nil, modifiers: [.command]).displayLabel, "⌘↩")
        XCTAssertEqual(SoundShortcut(keyCode: 53, characters: nil, modifiers: [.option]).displayLabel, "⌥⎋")
        XCTAssertEqual(SoundShortcut(keyCode: 49, characters: " ", modifiers: [.option]).displayLabel, "⌥Space")
        XCTAssertEqual(SoundShortcut(keyCode: 126, characters: nil, modifiers: [.control]).displayLabel, "⌃↑")
        XCTAssertEqual(SoundShortcut(keyCode: 105, characters: nil, modifiers: []).displayLabel, "F13")
    }

    func testValidationRejectsPlainCharactersAndReservedShortcuts() {
        XCTAssertEqual(
            ShortcutCaptureService.validationResult(
                for: SoundShortcut(keyCode: 7, characters: "x", modifiers: [])
            ),
            .invalid("Use at least one modifier key.")
        )
        XCTAssertEqual(
            ShortcutCaptureService.validationResult(
                for: SoundShortcut(keyCode: 49, characters: " ", modifiers: [])
            ),
            .invalid("Use at least one modifier key.")
        )
        XCTAssertEqual(
            ShortcutCaptureService.validationResult(
                for: SoundShortcut(keyCode: 12, characters: "q", modifiers: [.command])
            ),
            .invalid("That shortcut is reserved by macOS. Try another one.")
        )
        XCTAssertEqual(
            ShortcutCaptureService.validationResult(
                for: SoundShortcut(keyCode: 48, characters: nil, modifiers: [.command])
            ),
            .invalid("That shortcut is reserved by macOS. Try another one.")
        )
    }

    func testValidationAllowsPreferredShortcutShapes() {
        XCTAssertEqual(
            ShortcutCaptureService.validationResult(
                for: SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift])
            ),
            .valid
        )
        XCTAssertEqual(
            ShortcutCaptureService.validationResult(
                for: SoundShortcut(keyCode: 18, characters: "1", modifiers: [.option])
            ),
            .valid
        )
        XCTAssertEqual(
            ShortcutCaptureService.validationResult(
                for: SoundShortcut(keyCode: 105, characters: nil, modifiers: [])
            ),
            .valid
        )
    }

    func testShortcutMatchingNormalizesOnlyRelevantModifiers() {
        let shortcut = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift])

        XCTAssertTrue(shortcut.matches(keyCode: 7, modifierFlags: [.command, .shift, .capsLock]))
        XCTAssertFalse(shortcut.matches(keyCode: 7, modifierFlags: [.command]))
        XCTAssertFalse(shortcut.matches(keyCode: 8, modifierFlags: [.command, .shift]))
    }

    func testKeyCombinationComparisonIgnoresDisplayCharacters() {
        let storedShortcut = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift])
        let incomingEventShortcut = SoundShortcut(keyCode: 7, characters: "X", modifiers: [.command, .shift])

        XCTAssertTrue(storedShortcut.hasSameKeyCombination(as: incomingEventShortcut))
    }

    func testScopeAndEnabledStateRoundTripAndLegacyValuesRemainLocal() throws {
        let shortcut = SoundShortcut(
            keyCode: 105,
            characters: nil,
            modifiers: [],
            scope: .global,
            isEnabled: false
        )
        let decoded = try JSONDecoder().decode(SoundShortcut.self, from: JSONEncoder().encode(shortcut))
        XCTAssertEqual(decoded, shortcut)

        let legacyJSON = #"{"keyCode":7,"characters":"x","modifiers":3}"#.data(using: .utf8)!
        let legacy = try JSONDecoder().decode(SoundShortcut.self, from: legacyJSON)
        XCTAssertEqual(legacy.scope, .local)
        XCTAssertTrue(legacy.isEnabled)
    }

    func testKnownSystemShortcutsAreReservedWithoutRejectingCommandShiftS() {
        let reserved = [
            SoundShortcut(keyCode: 12, characters: "q", modifiers: [.command]),
            SoundShortcut(keyCode: 49, characters: " ", modifiers: [.command]),
            SoundShortcut(keyCode: 12, characters: "q", modifiers: [.control, .command]),
            SoundShortcut(keyCode: 123, characters: nil, modifiers: [.control]),
            SoundShortcut(keyCode: 20, characters: "3", modifiers: [.command, .shift])
        ]
        XCTAssertTrue(reserved.allSatisfy(ShortcutCaptureService.isReservedSystemShortcut))
        XCTAssertFalse(ShortcutCaptureService.isReservedSystemShortcut(
            SoundShortcut(keyCode: 1, characters: "s", modifiers: [.command, .shift])
        ))
    }
}
