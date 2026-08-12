import Foundation

enum CueletVirtualAudioDriverStateKind: String, Equatable {
    case notInstalled
    case preparedForInstallation
    case restartRequired
    case ready
    case selected
    case unavailable
    case versionMismatch
    case updateAvailable
    case installationError
    case routingError
}

struct CueletVirtualAudioDriverStatus: Equatable {
    static let expectedVersion = "0.1.8"
    static let expectedBuildVersion = "9"
    static let bundleIdentifier = "ch.oki.cuelet.virtual-microphone.driver"
    static let bundleName = "CueletVirtualAudio.driver"
    static let deviceName = "Cuelet Virtual Microphone"
    static let deviceUID = "ch.oki.cuelet.virtual-microphone"
    static let modelUID = "ch.oki.cuelet.virtual-microphone.model"
    static let destinationPath = "/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver"

    // These builds changed diagnostic surfaces only. Their audio transport is
    // compatible with the 0.1.8/9 transport contract; the public Release
    // bundle disables the optional event telemetry at compile time.
    private static let transportCompatibleBuilds: Set<String> = [
        "0.1.9/10",
        "0.1.10/11",
        "0.1.11/12",
    ]

    static func isCompatible(version: String, buildVersion: String) -> Bool {
        if version == expectedVersion && buildVersion == expectedBuildVersion {
            return true
        }
        return transportCompatibleBuilds.contains(
            "\(version)/\(buildVersion)"
        )
    }

    var kind: CueletVirtualAudioDriverStateKind
    var installedVersion: String?
    var installedBuildVersion: String?
    var preparedVersion: String?
    var preparedBuildVersion: String?
    var preparedBundlePath: String?
    var inputStreamVisible: Bool
    var outputStreamVisible: Bool
    var technicalDetails: String

    var title: String {
        switch kind {
        case .notInstalled, .preparedForInstallation: "Driver not installed"
        case .restartRequired: "Restart required"
        case .ready: "Driver ready"
        case .selected: "Driver ready — selected"
        case .unavailable: "Driver unavailable"
        case .versionMismatch, .updateAvailable: "Driver update required"
        case .installationError: "Driver installation error"
        case .routingError: "Audio routing error"
        }
    }

    var message: String {
        switch kind {
        case .notInstalled:
            "The Cuelet audio driver is not installed. Run the Cuelet Installer package to install or repair Cuelet, then restart your Mac."
        case .preparedForInstallation:
            "The Cuelet audio driver is not installed. Run the Cuelet Installer package again to install it, then restart your Mac."
        case .restartRequired:
            "Restart your Mac to finish installing the Cuelet audio driver."
        case .ready:
            "Cuelet Virtual Microphone is ready. Select it as Cuelet’s output to send soundboard audio to other apps."
        case .selected:
            "Cuelet playback is configured for the virtual device. Receiving apps can select Cuelet Virtual Microphone as their input."
        case .unavailable:
            "The Cuelet audio driver is installed but unavailable. Restart your Mac; if it remains unavailable, run the Cuelet Installer package again."
        case .versionMismatch:
            "The installed Cuelet audio driver is not compatible with this version of Cuelet. Install a current Cuelet package before using the virtual microphone."
        case .updateAvailable:
            "Run the Cuelet Installer package again to update the audio driver, then restart your Mac."
        case .installationError:
            "Cuelet could not verify the audio driver installation. Run the Cuelet Installer package again to repair it."
        case .routingError:
            "Cuelet Virtual Microphone is available, but Cuelet could not route playback to it. Try selecting it again."
        }
    }

    var isDeviceReady: Bool {
        inputStreamVisible && outputStreamVisible &&
            (kind == .ready || kind == .selected || kind == .routingError)
    }

    static let notInstalled = CueletVirtualAudioDriverStatus(
        kind: .notInstalled,
        installedVersion: nil,
        installedBuildVersion: nil,
        preparedVersion: nil,
        preparedBuildVersion: nil,
        preparedBundlePath: nil,
        inputStreamVisible: false,
        outputStreamVisible: false,
        technicalDetails: "Runtime readiness requires the stable Core Audio UID, not only a file on disk."
    )
}

struct CueletVirtualAudioBundleMetadata: Equatable {
    var bundleIdentifier: String
    var version: String
    var buildVersion: String
    var executable: String
}

protocol CueletVirtualAudioBundleInspecting {
    func metadata(at bundleURL: URL) -> CueletVirtualAudioBundleMetadata?
    func modificationDate(at bundleURL: URL) -> Date?
    func fileExists(at bundleURL: URL) -> Bool
}

struct FileSystemVirtualAudioBundleInspector: CueletVirtualAudioBundleInspecting {
    func metadata(at bundleURL: URL) -> CueletVirtualAudioBundleMetadata? {
        let plistURL = bundleURL.appendingPathComponent("Contents/Info.plist")
        guard let data = try? Data(contentsOf: plistURL),
              let dictionary = try? PropertyListSerialization.propertyList(
                from: data,
                format: nil
              ) as? [String: Any],
              let bundleIdentifier = dictionary["CFBundleIdentifier"] as? String,
              let version = dictionary["CFBundleShortVersionString"] as? String,
              let buildVersion = dictionary["CFBundleVersion"] as? String,
              let executable = dictionary["CFBundleExecutable"] as? String else {
            return nil
        }
        return CueletVirtualAudioBundleMetadata(
            bundleIdentifier: bundleIdentifier,
            version: version,
            buildVersion: buildVersion,
            executable: executable
        )
    }

    func modificationDate(at bundleURL: URL) -> Date? {
        let plistURL = bundleURL.appendingPathComponent("Contents/Info.plist")
        return try? plistURL.resourceValues(forKeys: [.contentModificationDateKey])
            .contentModificationDate
    }

    func fileExists(at bundleURL: URL) -> Bool {
        FileManager.default.fileExists(atPath: bundleURL.path)
    }
}

@MainActor
protocol CueletVirtualAudioDriverServicing: AnyObject {
    func status(
        selectedOutputDeviceID: String,
        routingStatus: AudioRouteStatus
    ) -> CueletVirtualAudioDriverStatus
    func recordInstallationError(_ message: String?)
}

@MainActor
final class CueletVirtualAudioDriverService: CueletVirtualAudioDriverServicing {
    private let deviceProvider: AudioDeviceProviding
    private let bundleInspector: CueletVirtualAudioBundleInspecting
    private let installedBundleURL: URL
    private let preparedBundleURLs: [URL]
    private let bootDate: Date
    private var installationError: String?

    init(
        deviceProvider: AudioDeviceProviding,
        bundleInspector: CueletVirtualAudioBundleInspecting = FileSystemVirtualAudioBundleInspector(),
        installedBundleURL: URL = URL(
            fileURLWithPath: CueletVirtualAudioDriverStatus.destinationPath,
            isDirectory: true
        ),
        preparedBundleURLs: [URL]? = nil,
        bootDate: Date = Date(
            timeIntervalSinceNow: -ProcessInfo.processInfo.systemUptime
        )
    ) {
        self.deviceProvider = deviceProvider
        self.bundleInspector = bundleInspector
        self.installedBundleURL = installedBundleURL
        self.bootDate = bootDate

        if let preparedBundleURLs {
            self.preparedBundleURLs = preparedBundleURLs
        } else {
            var candidates: [URL] = []
#if DEBUG
            if let override = ProcessInfo.processInfo.environment[
                "CUELET_VIRTUAL_AUDIO_DRIVER_BUNDLE"
            ], !override.isEmpty {
                candidates.append(URL(fileURLWithPath: override, isDirectory: true))
            }
#endif
            if let resources = Bundle.main.resourceURL {
                candidates.append(
                    resources
                        .appendingPathComponent("Driver", isDirectory: true)
                        .appendingPathComponent(
                            CueletVirtualAudioDriverStatus.bundleName,
                            isDirectory: true
                        )
                )
            }
            self.preparedBundleURLs = candidates
        }
    }

    func recordInstallationError(_ message: String?) {
        installationError = message
    }

    func status(
        selectedOutputDeviceID: String,
        routingStatus: AudioRouteStatus
    ) -> CueletVirtualAudioDriverStatus {
        let installedExists = bundleInspector.fileExists(at: installedBundleURL)
        let installedMetadata = installedExists
            ? bundleInspector.metadata(at: installedBundleURL)
            : nil
        let prepared = preparedBundleURLs.lazy.compactMap { url -> (
            URL,
            CueletVirtualAudioBundleMetadata
        )? in
            guard self.bundleInspector.fileExists(at: url),
                  let metadata = self.bundleInspector.metadata(at: url),
                  metadata.bundleIdentifier == CueletVirtualAudioDriverStatus.bundleIdentifier,
                  metadata.executable == "CueletVirtualAudio" else {
                return nil
            }
            return (url, metadata)
        }.first

        let preparedVersion = prepared?.1.version
        let preparedBuildVersion = prepared?.1.buildVersion
        let preparedPath = prepared?.0.path
        let outputVisible = deviceProvider.outputDeviceSnapshots().contains {
            $0.device.isAlive &&
                $0.device.coreAudioUID == CueletVirtualAudioDriverStatus.deviceUID
        }
        let inputVisible = deviceProvider.inputDevices().contains {
            $0.isAlive &&
                $0.coreAudioUID == CueletVirtualAudioDriverStatus.deviceUID
        }

        var details = [
            "Bundle ID: \(CueletVirtualAudioDriverStatus.bundleIdentifier)",
            "Device UID: \(CueletVirtualAudioDriverStatus.deviceUID)",
            "Model UID: \(CueletVirtualAudioDriverStatus.modelUID)",
            "Expected version: \(CueletVirtualAudioDriverStatus.expectedVersion)",
            "Expected build: \(CueletVirtualAudioDriverStatus.expectedBuildVersion)",
            "Installed path: \(installedBundleURL.path)",
            "Installed version: \(installedMetadata?.version ?? "none")",
            "Installed build: \(installedMetadata?.buildVersion ?? "none")",
            "Prepared version: \(preparedVersion ?? "none")",
            "Prepared build: \(preparedBuildVersion ?? "none")",
            "Output stream visible: \(outputVisible ? "yes" : "no")",
            "Input stream visible: \(inputVisible ? "yes" : "no")",
        ]

        if let installationError {
            details.append("Installation error: \(installationError)")
            return makeStatus(
                .installationError,
                installed: installedMetadata,
                preparedVersion: preparedVersion,
                preparedPath: preparedPath,
                inputVisible: inputVisible,
                outputVisible: outputVisible,
                details: details
            )
        }

        guard installedExists else {
            return makeStatus(
                prepared == nil ? .notInstalled : .preparedForInstallation,
                installed: nil,
                preparedVersion: preparedVersion,
                preparedPath: preparedPath,
                inputVisible: inputVisible,
                outputVisible: outputVisible,
                details: details
            )
        }

        guard let installedMetadata,
              installedMetadata.bundleIdentifier == CueletVirtualAudioDriverStatus.bundleIdentifier,
              installedMetadata.executable == "CueletVirtualAudio" else {
            details.append("Identity check: failed")
            return makeStatus(
                .installationError,
                installed: installedMetadata,
                preparedVersion: preparedVersion,
                preparedPath: preparedPath,
                inputVisible: inputVisible,
                outputVisible: outputVisible,
                details: details
            )
        }

        if !CueletVirtualAudioDriverStatus.isCompatible(
            version: installedMetadata.version,
            buildVersion: installedMetadata.buildVersion
        ) {
            let preparedIsCompatible = prepared.map {
                CueletVirtualAudioDriverStatus.isCompatible(
                    version: $0.1.version,
                    buildVersion: $0.1.buildVersion
                )
            } == true
            let preparedIsNewer = preparedIsCompatible && prepared.map {
                version(installedMetadata.version, isOlderThan: $0.1.version) ||
                    (installedMetadata.version == $0.1.version &&
                     version(installedMetadata.buildVersion,
                             isOlderThan: $0.1.buildVersion))
            } == true
            let state: CueletVirtualAudioDriverStateKind =
                preparedIsNewer
                ? .updateAvailable
                : .versionMismatch
            return makeStatus(
                state,
                installed: installedMetadata,
                preparedVersion: preparedVersion,
                preparedPath: preparedPath,
                inputVisible: inputVisible,
                outputVisible: outputVisible,
                details: details
            )
        }

        // The Installer touches the installed driver's Info.plist after a
        // successful install. A live device visible during the same boot may
        // still be the previously loaded plug-in, so the install timestamp
        // must take precedence over live-device visibility.
        let modifiedAfterBoot = bundleInspector.modificationDate(
            at: installedBundleURL
        ).map { $0 >= bootDate } ?? false
        if modifiedAfterBoot {
            return makeStatus(
                .restartRequired,
                installed: installedMetadata,
                preparedVersion: preparedVersion,
                preparedPath: preparedPath,
                inputVisible: inputVisible,
                outputVisible: outputVisible,
                details: details
            )
        }

        if inputVisible && outputVisible {
            let expectedOutputID = AudioDevice.persistentID(
                forCoreAudioUID: CueletVirtualAudioDriverStatus.deviceUID
            )
            let isSelected = selectedOutputDeviceID == expectedOutputID
            let state: CueletVirtualAudioDriverStateKind
            if isSelected && routingStatus.kind == .failed {
                state = .routingError
                if let routingDetails = routingStatus.technicalDetails {
                    details.append("Routing error: \(routingDetails)")
                }
            } else {
                state = isSelected ? .selected : .ready
            }
            return makeStatus(
                state,
                installed: installedMetadata,
                preparedVersion: preparedVersion,
                preparedPath: preparedPath,
                inputVisible: true,
                outputVisible: true,
                details: details
            )
        }

        return makeStatus(
            .unavailable,
            installed: installedMetadata,
            preparedVersion: preparedVersion,
            preparedPath: preparedPath,
            inputVisible: inputVisible,
            outputVisible: outputVisible,
            details: details
        )
    }

    private func makeStatus(
        _ kind: CueletVirtualAudioDriverStateKind,
        installed: CueletVirtualAudioBundleMetadata?,
        preparedVersion: String?,
        preparedPath: String?,
        inputVisible: Bool,
        outputVisible: Bool,
        details: [String]
    ) -> CueletVirtualAudioDriverStatus {
        CueletVirtualAudioDriverStatus(
            kind: kind,
            installedVersion: installed?.version,
            installedBuildVersion: installed?.buildVersion,
            preparedVersion: preparedVersion,
            preparedBuildVersion: preparedPath.flatMap {
                bundleInspector.metadata(at: URL(fileURLWithPath: $0))?.buildVersion
            },
            preparedBundlePath: preparedPath,
            inputStreamVisible: inputVisible,
            outputStreamVisible: outputVisible,
            technicalDetails: details.joined(separator: "\n")
        )
    }

    private func version(_ left: String, isOlderThan right: String) -> Bool {
        left.compare(right, options: .numeric) == .orderedAscending
    }
}
