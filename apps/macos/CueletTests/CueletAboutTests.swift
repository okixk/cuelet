import XCTest
@testable import Cuelet

final class CueletAboutTests: XCTestCase {
    func testVersionTextUsesBundleMetadata() {
        let metadata = CueletAboutMetadata(
            infoDictionary: [
                "CFBundleDisplayName": "Cuelet",
                "CFBundleShortVersionString": "9.8.7",
                "CFBundleVersion": "654"
            ]
        )

        XCTAssertEqual(metadata.applicationName, "Cuelet")
        XCTAssertEqual(metadata.versionText, "Version 9.8.7")
        XCTAssertEqual(metadata.buildVersion, "654")
    }

    func testAboutLegalCopyAndLinksAreCanonical() {
        XCTAssertEqual(CueletAboutContent.contributors, "Cuelet contributors")
        XCTAssertEqual(
            CueletAboutContent.licenseStatement,
            "Cuelet is free and open-source software licensed under the GNU Affero General Public License version 3 only."
        )
        XCTAssertEqual(CueletAboutContent.projectURL.absoluteString, "https://github.com/okixk/cuelet")
        XCTAssertEqual(CueletAboutContent.issueTrackerURL.absoluteString, "https://github.com/okixk/cuelet/issues")
    }
}
