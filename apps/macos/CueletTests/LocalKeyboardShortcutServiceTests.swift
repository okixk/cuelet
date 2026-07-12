import AppKit
import XCTest
@testable import Cuelet

final class LocalKeyboardShortcutServiceTests: XCTestCase {
    func testMapsExpectedKeyCodesToLocalActions() {
        XCTAssertEqual(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 49), .playSelected)
        XCTAssertEqual(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 36), .playSelected)
        XCTAssertEqual(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 53), .stopOrClearSelection)
        XCTAssertEqual(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 123), .moveSelection(.left, extendsSelection: false))
        XCTAssertEqual(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 124), .moveSelection(.right, extendsSelection: false))
        XCTAssertEqual(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 125), .moveSelection(.down, extendsSelection: false))
        XCTAssertEqual(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 126), .moveSelection(.up, extendsSelection: false))
        XCTAssertEqual(
            LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 124, modifierFlags: [.shift]),
            .moveSelection(.right, extendsSelection: true)
        )
        XCTAssertEqual(
            LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 0, modifierFlags: [.command]),
            .selectAll
        )
        XCTAssertNil(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 0, modifierFlags: [.command, .shift]))
        XCTAssertNil(LocalKeyboardShortcutService.Action.keyDownAction(forKeyCode: 0))
    }

    func testReservedModifiersBypassLocalSoundboardHandling() {
        XCTAssertTrue(LocalKeyboardShortcutService.hasReservedModifier(in: [.command]))
        XCTAssertTrue(LocalKeyboardShortcutService.hasReservedModifier(in: [.control]))
        XCTAssertTrue(LocalKeyboardShortcutService.hasReservedModifier(in: [.option]))
        XCTAssertFalse(LocalKeyboardShortcutService.hasReservedModifier(in: [.shift]))
        XCTAssertFalse(LocalKeyboardShortcutService.hasReservedModifier(in: [.numericPad]))
    }

    func testTextInputRespondersBypassLocalSoundboardHandling() {
        let textField = NSTextField()
        let searchField = NSSearchField()
        let textView = NSTextView()
        let button = NSButton()

        XCTAssertTrue(LocalKeyboardShortcutService.isTextInputOrEditableResponder(textField))
        XCTAssertTrue(LocalKeyboardShortcutService.isTextInputOrEditableResponder(searchField))
        XCTAssertTrue(LocalKeyboardShortcutService.isTextInputOrEditableResponder(textView))
        XCTAssertFalse(LocalKeyboardShortcutService.isTextInputOrEditableResponder(button))
        XCTAssertFalse(LocalKeyboardShortcutService.isTextInputOrEditableResponder(nil))
    }
}
