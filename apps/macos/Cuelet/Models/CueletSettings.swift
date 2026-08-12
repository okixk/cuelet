import AVFoundation
import Foundation

struct CueletSettings: Codable, Equatable {
    static let currentSchemaVersion = 3

    var schemaVersion = currentSchemaVersion
    var libraryMetadataMigrationVersion = 0
    var libraryPath = ""
    var viewMode = ViewMode.grid
    var sortOption = SoundSortOption.nameAscending
    var outputDeviceName = "System Default"
    var outputDeviceID = AudioDevice.systemOutput.id
    var outputFallbackPolicy = AudioOutputFallbackPolicy.stopAndWait
    var inputDeviceID: String?
    var isInputMonitoringEnabled = false
    var soundboardVolume = 1.0
    var microphoneVolume = 1.0
    var allowsSimultaneousPlayback = true
    var stopOnLibraryChange = true
    var overlayOpacity = 0.92
    var overlayColumns = 3
    var appearanceMode = "System"
    var copiesImportedFiles = true
    var preservesFolderStructure = true
    var scansSubfolders = true
    var globalStopShortcut = "⌘."
    var keepsRunningAfterWindowClose = false
    var showsMenuBarItem = false
    var launchesAtLogin = false
    var startsHidden = false
    var categoryColorHexes = SoundCategory.defaultColorHexes
    var customCategories: [SoundCategory] = []
    var categoryNames: [String: String] = [:]
    var soundCategoryAssignments: [String: String] = [:]
    var soundShortcutAssignments: [String: SoundShortcut] = [:]
    var favoriteSoundIDs: Set<String> = []

    enum CodingKeys: String, CodingKey {
        case schemaVersion
        case libraryMetadataMigrationVersion
        case libraryPath
        case viewMode
        case sortOption
        case outputDeviceName
        case outputDeviceID
        case outputFallbackPolicy
        case inputDeviceID
        case isInputMonitoringEnabled
        case soundboardVolume
        case microphoneVolume
        case allowsSimultaneousPlayback
        case stopOnLibraryChange
        case overlayOpacity
        case overlayColumns
        case appearanceMode
        case copiesImportedFiles
        case preservesFolderStructure
        case scansSubfolders
        case globalStopShortcut
        case keepsRunningAfterWindowClose
        case showsMenuBarItem
        case launchesAtLogin
        case startsHidden
        case categoryColorHexes
        case customCategories
        case categoryNames
        case soundCategoryAssignments
        case soundShortcutAssignments
        case favoriteSoundIDs
    }

    init() {}

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let decodedSchemaVersion = try container.decodeIfPresent(Int.self, forKey: .schemaVersion) ?? 1
        schemaVersion = Self.currentSchemaVersion
        libraryMetadataMigrationVersion = try container.decodeIfPresent(Int.self, forKey: .libraryMetadataMigrationVersion) ?? libraryMetadataMigrationVersion
        libraryPath = try container.decodeIfPresent(String.self, forKey: .libraryPath) ?? libraryPath
        viewMode = try container.decodeIfPresent(ViewMode.self, forKey: .viewMode) ?? viewMode
        sortOption = try container.decodeIfPresent(SoundSortOption.self, forKey: .sortOption) ?? sortOption
        outputDeviceName = try container.decodeIfPresent(String.self, forKey: .outputDeviceName) ?? outputDeviceName
        outputDeviceID = try container.decodeIfPresent(String.self, forKey: .outputDeviceID) ?? outputDeviceID
        outputFallbackPolicy = try container.decodeIfPresent(AudioOutputFallbackPolicy.self, forKey: .outputFallbackPolicy) ?? outputFallbackPolicy
        inputDeviceID = try container.decodeIfPresent(String.self, forKey: .inputDeviceID)
        isInputMonitoringEnabled = try container.decodeIfPresent(Bool.self, forKey: .isInputMonitoringEnabled) ?? isInputMonitoringEnabled
        soundboardVolume = try container.decodeIfPresent(Double.self, forKey: .soundboardVolume) ?? soundboardVolume
        microphoneVolume = try container.decodeIfPresent(Double.self, forKey: .microphoneVolume) ?? microphoneVolume
        allowsSimultaneousPlayback = try container.decodeIfPresent(Bool.self, forKey: .allowsSimultaneousPlayback) ?? allowsSimultaneousPlayback
        stopOnLibraryChange = try container.decodeIfPresent(Bool.self, forKey: .stopOnLibraryChange) ?? stopOnLibraryChange
        overlayOpacity = try container.decodeIfPresent(Double.self, forKey: .overlayOpacity) ?? overlayOpacity
        overlayColumns = try container.decodeIfPresent(Int.self, forKey: .overlayColumns) ?? overlayColumns
        appearanceMode = try container.decodeIfPresent(String.self, forKey: .appearanceMode) ?? appearanceMode
        copiesImportedFiles = try container.decodeIfPresent(Bool.self, forKey: .copiesImportedFiles) ?? copiesImportedFiles
        preservesFolderStructure = try container.decodeIfPresent(Bool.self, forKey: .preservesFolderStructure) ?? preservesFolderStructure
        scansSubfolders = try container.decodeIfPresent(Bool.self, forKey: .scansSubfolders) ?? scansSubfolders
        globalStopShortcut = try container.decodeIfPresent(String.self, forKey: .globalStopShortcut) ?? globalStopShortcut
        keepsRunningAfterWindowClose = try container.decodeIfPresent(Bool.self, forKey: .keepsRunningAfterWindowClose) ?? keepsRunningAfterWindowClose
        showsMenuBarItem = try container.decodeIfPresent(Bool.self, forKey: .showsMenuBarItem) ?? showsMenuBarItem
        launchesAtLogin = try container.decodeIfPresent(Bool.self, forKey: .launchesAtLogin) ?? launchesAtLogin
        startsHidden = try container.decodeIfPresent(Bool.self, forKey: .startsHidden) ?? startsHidden
        categoryColorHexes = try container.decodeIfPresent([String: String].self, forKey: .categoryColorHexes) ?? categoryColorHexes
        customCategories = try container.decodeIfPresent([SoundCategory].self, forKey: .customCategories) ?? customCategories
        categoryNames = try container.decodeIfPresent([String: String].self, forKey: .categoryNames) ?? categoryNames
        soundCategoryAssignments = try container.decodeIfPresent([String: String].self, forKey: .soundCategoryAssignments) ?? soundCategoryAssignments
        soundShortcutAssignments = try container.decodeIfPresent([String: SoundShortcut].self, forKey: .soundShortcutAssignments) ?? soundShortcutAssignments
        favoriteSoundIDs = try container.decodeIfPresent(Set<String>.self, forKey: .favoriteSoundIDs) ?? favoriteSoundIDs

        // Older development builds persisted temporary numeric AudioDeviceIDs as
        // "coreaudio-123". They cannot identify the same device after a reconnect,
        // so migrate those values to the safe system-output selection. Stable
        // "coreaudio:<UID>" values introduced by schema 2 remain valid.
        if decodedSchemaVersion < Self.currentSchemaVersion,
           outputDeviceID.hasPrefix("coreaudio-"),
           outputDeviceID.dropFirst("coreaudio-".count).allSatisfy(\.isNumber) {
            outputDeviceID = AudioDevice.systemOutput.id
            outputDeviceName = AudioDevice.systemOutput.name
        }
    }
}

enum AudioDeviceKind: String, Hashable {
    case input
    case output
    case virtual
}

struct AudioDevice: Identifiable, Hashable {
    let id: String
    var name: String
    var kind: AudioDeviceKind
    var isDefault: Bool
    var isVirtual: Bool
    var manufacturer: String?
    var transportName: String?
    var isAlive: Bool

    init(
        id: String,
        name: String,
        kind: AudioDeviceKind,
        isDefault: Bool,
        isVirtual: Bool,
        manufacturer: String? = nil,
        transportName: String? = nil,
        isAlive: Bool = true
    ) {
        self.id = id
        self.name = name
        self.kind = kind
        self.isDefault = isDefault
        self.isVirtual = isVirtual
        self.manufacturer = manufacturer
        self.transportName = transportName
        self.isAlive = isAlive
    }

    var coreAudioUID: String? {
        guard id.hasPrefix("coreaudio:") else { return nil }
        let uid = String(id.dropFirst("coreaudio:".count))
        return uid.isEmpty ? nil : uid
    }

    static func persistentID(forCoreAudioUID uid: String) -> String {
        "coreaudio:\(uid)"
    }

    static let systemOutput = AudioDevice(
        id: "system-output",
        name: "System Default",
        kind: .output,
        isDefault: true,
        isVirtual: false,
        transportName: "Follows macOS"
    )
}

enum AudioOutputFallbackPolicy: String, CaseIterable, Identifiable, Codable {
    case stopAndWait
    case systemOutput

    var id: String { rawValue }

    var title: String {
        switch self {
        case .stopAndWait: "Stop and Wait"
        case .systemOutput: "Temporarily Use System Output"
        }
    }

    var explanation: String {
        switch self {
        case .stopAndWait:
            "Stops playback if the selected device disappears and waits for that exact device UID to return."
        case .systemOutput:
            "Stops current playback, then sends future playback to System Output until the exact selected UID returns."
        }
    }
}

enum AudioRouteStatusKind: String, Equatable {
    case applying
    case ready
    case systemOutput
    case explicitDevice
    case unavailable
    case fallbackSystemOutput
    case reconnecting
    case failed
}

struct AudioRouteStatus: Equatable {
    var kind: AudioRouteStatusKind
    var selectedDeviceID: String
    var selectedName: String
    var activeDeviceID: String?
    var activeName: String?
    var message: String
    var technicalDetails: String?

    static let applyingSystemOutput = AudioRouteStatus(
        kind: .applying,
        selectedDeviceID: AudioDevice.systemOutput.id,
        selectedName: AudioDevice.systemOutput.name,
        activeDeviceID: nil,
        activeName: nil,
        message: "Applying System Output…",
        technicalDetails: nil
    )

    var isConfirmedActive: Bool {
        switch kind {
        case .systemOutput, .explicitDevice, .fallbackSystemOutput: activeDeviceID != nil
        case .applying, .ready, .unavailable, .reconnecting, .failed: false
        }
    }

    var allowsPlayback: Bool {
        switch kind {
        case .ready, .systemOutput, .explicitDevice, .fallbackSystemOutput: true
        case .applying, .unavailable, .reconnecting, .failed: false
        }
    }
}

enum MicrophonePermissionState: String, Codable, Hashable {
    case unknown
    case notDetermined
    case authorized
    case denied
    case restricted
    case unavailable
    case missingUsageDescription

    init(status: AVAuthorizationStatus) {
        switch status {
        case .notDetermined: self = .notDetermined
        case .restricted: self = .restricted
        case .denied: self = .denied
        case .authorized: self = .authorized
        @unknown default: self = .unknown
        }
    }

    var title: String {
        switch self {
        case .unknown: "Unknown"
        case .notDetermined: "Not Requested"
        case .authorized: "Allowed"
        case .denied: "Denied"
        case .restricted: "Restricted"
        case .unavailable: "Unavailable"
        case .missingUsageDescription: "Missing Usage Description"
        }
    }
}

struct InputLevelState: Codable, Equatable {
    var averagePower: Double
    var peakPower: Double
    var isMonitoring: Bool

    static let inactive = InputLevelState(averagePower: 0, peakPower: 0, isMonitoring: false)
}
