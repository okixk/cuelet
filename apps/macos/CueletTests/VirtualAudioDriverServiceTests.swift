import XCTest
@testable import Cuelet

@MainActor
final class VirtualAudioDriverServiceTests: XCTestCase {
    private let installedURL = URL(fileURLWithPath: "/test/installed.driver")
    private let preparedURL = URL(fileURLWithPath: "/test/prepared.driver")
    private let bootDate = Date(timeIntervalSince1970: 10_000)

    func testExpectedDriverCompatibilityIs018Build9() {
        XCTAssertEqual(CueletVirtualAudioDriverStatus.expectedVersion, "0.1.8")
        XCTAssertEqual(CueletVirtualAudioDriverStatus.expectedBuildVersion, "9")

        let provider = FakeDriverAudioDeviceProvider(
            outputs: [liveDriverOutput()],
            inputs: [driverInput()]
        )
        let service = makeService(
            provider: provider,
            inspector: installedInspector()
        )

        let status = service.status()
        XCTAssertEqual(status.kind, .ready)
        XCTAssertEqual(status.installedVersion, "0.1.8")
        XCTAssertEqual(status.installedBuildVersion, "9")
    }

    func testTransportCompatibleDriverBuildsPreserveThe018Contract() {
        for (version, build) in [
            ("0.1.9", "10"),
            ("0.1.10", "11"),
            ("0.1.11", "12"),
        ] {
            let inspector = FakeDriverBundleInspector()
            inspector.entries[installedURL] = .valid(
                version: version,
                buildVersion: build,
                modified: bootDate.addingTimeInterval(-10)
            )
            let provider = FakeDriverAudioDeviceProvider(
                outputs: [liveDriverOutput()],
                inputs: [driverInput()]
            )
            let service = makeService(provider: provider, inspector: inspector)

            let status = service.status()
            XCTAssertEqual(status.kind, .ready)
            XCTAssertEqual(status.installedVersion, version)
            XCTAssertEqual(status.installedBuildVersion, build)
            XCTAssertTrue(
                CueletVirtualAudioDriverStatus.isCompatible(
                    version: version,
                    buildVersion: build
                )
            )
            XCTAssertTrue(status.technicalDetails.contains("Expected version: 0.1.8"))
            XCTAssertTrue(status.technicalDetails.contains("Expected build: 9"))
        }
    }

    func testNotInstalledAndPreparedStatesAreDistinct() {
        let inspector = FakeDriverBundleInspector()
        let provider = FakeDriverAudioDeviceProvider()
        var service = makeService(provider: provider, inspector: inspector)

        XCTAssertEqual(service.status().kind, .notInstalled)

        inspector.entries[preparedURL] = .valid(
            version: CueletVirtualAudioDriverStatus.expectedVersion,
            modified: bootDate.addingTimeInterval(-100)
        )
        service = makeService(provider: provider, inspector: inspector)
        let prepared = service.status()
        XCTAssertEqual(prepared.kind, .preparedForInstallation)
        XCTAssertEqual(prepared.preparedVersion, "0.1.8")
        XCTAssertEqual(prepared.preparedBundlePath, preparedURL.path)
    }

    func testInstalledAfterBootWithoutDeviceRequiresRestart() {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = .valid(
            version: "0.1.8",
            modified: bootDate.addingTimeInterval(10)
        )
        let service = makeService(
            provider: FakeDriverAudioDeviceProvider(),
            inspector: inspector
        )

        let status = service.status()
        XCTAssertEqual(status.kind, .restartRequired)
        XCTAssertEqual(status.installedVersion, "0.1.8")
        XCTAssertFalse(status.isDeviceReady)
    }

    func testInstalledAfterBootWithPreviouslyLoadedDeviceStillRequiresRestart() {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = .valid(
            version: "0.1.11",
            buildVersion: "12",
            modified: bootDate.addingTimeInterval(10)
        )
        let provider = FakeDriverAudioDeviceProvider(
            outputs: [liveDriverOutput()],
            inputs: [driverInput()]
        )
        let service = makeService(provider: provider, inspector: inspector)

        let status = service.status()
        XCTAssertEqual(status.kind, .restartRequired)
        XCTAssertEqual(
            status.message,
            "Restart your Mac to finish installing the Cuelet audio driver."
        )
        XCTAssertFalse(status.isDeviceReady)
    }

    func testPublicDriverMessagesUseInstallerRatherThanTerminalCommands() {
        let statuses = [
            CueletVirtualAudioDriverStatus.notInstalled.message,
            CueletVirtualAudioDriverStatus(
                kind: .preparedForInstallation,
                installedVersion: nil,
                installedBuildVersion: nil,
                preparedVersion: "0.1.11",
                preparedBuildVersion: "12",
                preparedBundlePath: "/prepared.driver",
                inputStreamVisible: false,
                outputStreamVisible: false,
                technicalDetails: ""
            ).message,
        ]

        XCTAssertTrue(statuses.allSatisfy { $0.contains("Cuelet Installer") })
        XCTAssertTrue(statuses.allSatisfy { !$0.contains("Terminal") })
        XCTAssertTrue(statuses.allSatisfy { !$0.contains("sudo") })
    }

    func testInstalledBeforeBootWithoutDeviceIsUnavailable() {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = .valid(
            version: "0.1.8",
            modified: bootDate.addingTimeInterval(-10)
        )
        let service = makeService(
            provider: FakeDriverAudioDeviceProvider(),
            inspector: inspector
        )

        XCTAssertEqual(service.status().kind, .unavailable)
    }

    func testReadyRequiresBothCoreAudioStreamDirections() {
        let inspector = installedInspector()
        let provider = FakeDriverAudioDeviceProvider()
        provider.outputs = [liveDriverOutput()]
        let service = makeService(provider: provider, inspector: inspector)

        XCTAssertEqual(service.status().kind, .unavailable)
        provider.inputs = [driverInput()]
        let ready = service.status()
        XCTAssertEqual(ready.kind, .ready)
        XCTAssertTrue(ready.inputStreamVisible)
        XCTAssertTrue(ready.outputStreamVisible)
        XCTAssertTrue(ready.isDeviceReady)
    }

    func testReadyDeviceReportsSelectedAndRoutingError() {
        let provider = FakeDriverAudioDeviceProvider(
            outputs: [liveDriverOutput()],
            inputs: [driverInput()]
        )
        let service = makeService(
            provider: provider,
            inspector: installedInspector()
        )
        let selectedID = AudioDevice.persistentID(
            forCoreAudioUID: CueletVirtualAudioDriverStatus.deviceUID
        )

        XCTAssertEqual(
            service.status(selectedID: selectedID).kind,
            .selected
        )
        var route = AudioRouteStatus.applyingSystemOutput
        route.kind = .failed
        route.technicalDetails = "Injected route rejection"
        let failed = service.status(
            selectedOutputDeviceID: selectedID,
            routingStatus: route
        )
        XCTAssertEqual(failed.kind, .routingError)
        XCTAssertTrue(failed.technicalDetails.contains("Injected route rejection"))
    }

    func testOlderInstalledVersionReportsUpdateAvailable() {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = .valid(
            version: "0.1.3",
            modified: bootDate.addingTimeInterval(-10)
        )
        inspector.entries[preparedURL] = .valid(
            version: "0.1.8",
            modified: bootDate.addingTimeInterval(-10)
        )
        let service = makeService(
            provider: FakeDriverAudioDeviceProvider(),
            inspector: inspector
        )

        XCTAssertEqual(service.status().kind, .updateAvailable)
    }

    func testNewerOrUnexpectedVersionReportsMismatch() {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = .valid(
            version: "2.0.0",
            modified: bootDate.addingTimeInterval(-10)
        )
        let service = makeService(
            provider: FakeDriverAudioDeviceProvider(),
            inspector: inspector
        )

        XCTAssertEqual(service.status().kind, .versionMismatch)
    }

    func testExpectedVersionWithUnexpectedBuildReportsMismatch() {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = .valid(
            version: CueletVirtualAudioDriverStatus.expectedVersion,
            buildVersion: "4",
            modified: bootDate.addingTimeInterval(-10)
        )
        let service = makeService(
            provider: FakeDriverAudioDeviceProvider(),
            inspector: inspector
        )

        XCTAssertEqual(service.status().kind, .versionMismatch)
    }

    func testOlderBuildWithPreparedExpectedBuildReportsUpdateAvailable() {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = .valid(
            version: CueletVirtualAudioDriverStatus.expectedVersion,
            buildVersion: "4",
            modified: bootDate.addingTimeInterval(-10)
        )
        inspector.entries[preparedURL] = .valid(
            version: CueletVirtualAudioDriverStatus.expectedVersion,
            modified: bootDate.addingTimeInterval(-10)
        )
        let service = makeService(
            provider: FakeDriverAudioDeviceProvider(),
            inspector: inspector
        )

        XCTAssertEqual(service.status().kind, .updateAvailable)
    }

    func testUnexpectedBundleAtExactPathIsInstallationError() {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = FakeDriverBundleInspector.Entry(
            exists: true,
            metadata: CueletVirtualAudioBundleMetadata(
                bundleIdentifier: "com.example.unrelated",
                version: "1.0",
                buildVersion: "1",
                executable: "Unrelated"
            ),
            modified: bootDate.addingTimeInterval(-10)
        )
        let service = makeService(
            provider: FakeDriverAudioDeviceProvider(),
            inspector: inspector
        )

        XCTAssertEqual(service.status().kind, .installationError)
    }

    func testRecordedInstallationErrorTakesPriority() {
        let service = makeService(
            provider: FakeDriverAudioDeviceProvider(),
            inspector: FakeDriverBundleInspector()
        )
        service.recordInstallationError("copy failed")

        let status = service.status()
        XCTAssertEqual(status.kind, .installationError)
        XCTAssertTrue(status.technicalDetails.contains("copy failed"))
        service.recordInstallationError(nil)
        XCTAssertEqual(service.status().kind, .notInstalled)
    }

    private func makeService(
        provider: FakeDriverAudioDeviceProvider,
        inspector: FakeDriverBundleInspector
    ) -> CueletVirtualAudioDriverService {
        CueletVirtualAudioDriverService(
            deviceProvider: provider,
            bundleInspector: inspector,
            installedBundleURL: installedURL,
            preparedBundleURLs: [preparedURL],
            bootDate: bootDate
        )
    }

    private func installedInspector() -> FakeDriverBundleInspector {
        let inspector = FakeDriverBundleInspector()
        inspector.entries[installedURL] = .valid(
            version: "0.1.8",
            modified: bootDate.addingTimeInterval(-10)
        )
        return inspector
    }

    private func liveDriverOutput() -> LiveAudioOutputDevice {
        LiveAudioOutputDevice(
            device: AudioDevice(
                id: AudioDevice.persistentID(
                    forCoreAudioUID: CueletVirtualAudioDriverStatus.deviceUID
                ),
                name: CueletVirtualAudioDriverStatus.deviceName,
                kind: .output,
                isDefault: false,
                isVirtual: true,
                manufacturer: "Cuelet",
                transportName: "Virtual"
            ),
            audioDeviceID: 44
        )
    }

    private func driverInput() -> AudioDevice {
        AudioDevice(
            id: AudioDevice.persistentID(
                forCoreAudioUID: CueletVirtualAudioDriverStatus.deviceUID
            ),
            name: CueletVirtualAudioDriverStatus.deviceName,
            kind: .input,
            isDefault: false,
            isVirtual: true,
            manufacturer: "Cuelet",
            transportName: "Virtual"
        )
    }
}

private final class FakeDriverBundleInspector: CueletVirtualAudioBundleInspecting {
    struct Entry {
        var exists: Bool
        var metadata: CueletVirtualAudioBundleMetadata?
        var modified: Date?

        static func valid(
            version: String,
            buildVersion: String = CueletVirtualAudioDriverStatus.expectedBuildVersion,
            modified: Date
        ) -> Entry {
            Entry(
                exists: true,
                metadata: CueletVirtualAudioBundleMetadata(
                    bundleIdentifier: CueletVirtualAudioDriverStatus.bundleIdentifier,
                    version: version,
                    buildVersion: buildVersion,
                    executable: "CueletVirtualAudio"
                ),
                modified: modified
            )
        }
    }

    var entries: [URL: Entry] = [:]

    func metadata(at bundleURL: URL) -> CueletVirtualAudioBundleMetadata? {
        entries[bundleURL]?.metadata
    }

    func modificationDate(at bundleURL: URL) -> Date? {
        entries[bundleURL]?.modified
    }

    func fileExists(at bundleURL: URL) -> Bool {
        entries[bundleURL]?.exists == true
    }
}

@MainActor
private final class FakeDriverAudioDeviceProvider: AudioDeviceProviding {
    var outputs: [LiveAudioOutputDevice]
    var inputs: [AudioDevice]

    init(
        outputs: [LiveAudioOutputDevice] = [],
        inputs: [AudioDevice] = []
    ) {
        self.outputs = outputs
        self.inputs = inputs
    }

    func outputDeviceSnapshots() -> [LiveAudioOutputDevice] { outputs }
    func inputDevices() -> [AudioDevice] { inputs }
    func systemOutputDevice() -> LiveAudioOutputDevice? { nil }
    func outputDevice(forPersistentID persistentID: String) -> LiveAudioOutputDevice? {
        outputs.first { $0.device.id == persistentID }
    }
    func startObserving(_ handler: @escaping () -> Void) {}
    func stopObserving() {}
}

private extension CueletVirtualAudioDriverService {
    func status(
        selectedID: String = AudioDevice.systemOutput.id
    ) -> CueletVirtualAudioDriverStatus {
        status(
            selectedOutputDeviceID: selectedID,
            routingStatus: .applyingSystemOutput
        )
    }
}
