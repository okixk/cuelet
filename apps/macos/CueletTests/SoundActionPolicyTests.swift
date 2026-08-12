import XCTest
@testable import Cuelet

final class SoundActionPolicyTests: XCTestCase {
    func testManagedLinkedAndMissingContextMenuPolicies() {
        let managed = SoundClip(
            name: "Managed",
            filename: "managed.wav",
            category: .uncategorized,
            duration: 1,
            waveform: [],
            fileURL: URL(fileURLWithPath: "/tmp/managed.wav"),
            storageMode: .managed,
            managedRelativePath: "Sounds/managed.wav"
        )
        let linked = SoundClip(
            name: "Linked",
            filename: "linked.wav",
            category: .uncategorized,
            duration: 1,
            waveform: [],
            fileURL: URL(fileURLWithPath: "/tmp/linked.wav"),
            storageMode: .linked,
            externalSourcePath: "/tmp/linked.wav"
        )
        var missing = linked
        missing.isMissing = true

        let managedPolicy = SoundActionPolicy(clip: managed, fileExists: true)
        XCTAssertTrue(managedPolicy.canPlay)
        XCTAssertTrue(managedPolicy.canDeleteManagedFile)
        XCTAssertFalse(managedPolicy.canLocateOrRelink)
        XCTAssertTrue(managedPolicy.renameChangesDisplayNameOnly)

        let linkedPolicy = SoundActionPolicy(clip: linked, fileExists: true)
        XCTAssertTrue(linkedPolicy.canPlay)
        XCTAssertFalse(linkedPolicy.canDeleteManagedFile)
        XCTAssertTrue(linkedPolicy.canRemoveFromLibrary)

        let missingPolicy = SoundActionPolicy(clip: missing, fileExists: false)
        XCTAssertFalse(missingPolicy.canPlay)
        XCTAssertFalse(missingPolicy.canReveal)
        XCTAssertFalse(missingPolicy.canDeleteManagedFile)
        XCTAssertTrue(missingPolicy.canLocateOrRelink)
        XCTAssertTrue(missingPolicy.canRemoveFromLibrary)
    }
}
