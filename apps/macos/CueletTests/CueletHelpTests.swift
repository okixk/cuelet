import XCTest
@testable import Cuelet

final class CueletHelpTests: XCTestCase {
    func testEssentialHelpSectionsAndGettingStartedTerminology() {
        XCTAssertEqual(
            CueletHelpContent.sectionTitles,
            [
                "Getting Started",
                "Cuelet Virtual Microphone",
                "Global Shortcuts",
                "Troubleshooting",
                "Support & Online"
            ]
        )
        XCTAssertEqual(CueletHelpContent.gettingStarted.count, 5)
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Choose Library…"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Import Sounds…"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Assign Category"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Set Shortcut…"))
    }

    func testVirtualMicrophoneAndTroubleshootingCopyMatchesSupportedBehavior() {
        XCTAssertTrue(CueletHelpContent.offlineText.contains("restart your Mac"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Use as Cuelet Output"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Restart required"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Relink…"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Locate Replacement…"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Refresh Outputs"))
    }

    func testGlobalShortcutsDoNotClaimAnAccessibilityRequirement() {
        XCTAssertTrue(CueletHelpContent.offlineText.contains("Scope to Global"))
        XCTAssertTrue(CueletHelpContent.offlineText.contains("do not require Accessibility permission"))
    }

    func testSupportLinksAreCanonicalAndUnsupportedCLIGuidanceIsAbsent() {
        XCTAssertEqual(CueletHelpContent.projectURL.absoluteString, "https://github.com/okixk/cuelet")
        XCTAssertEqual(CueletHelpContent.issueTrackerURL.absoluteString, "https://github.com/okixk/cuelet/issues")
        XCTAssertFalse(CueletHelpContent.offlineText.contains("cuelet --help"))
    }
}
