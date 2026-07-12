import AVFoundation
import Foundation

struct CueletSettings: Codable, Equatable {
    var libraryPath = ""
    var viewMode = ViewMode.grid
    var sortOption = SoundSortOption.nameAscending
    var showsDemoLibrary = false
    var outputDeviceName = "System Default"
    var outputDeviceID = AudioDevice.systemOutput.id
    var inputDeviceID: String?
    var audioRoutingMode = AudioRoutingMode.speakerOnly
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
    var showAdvancedDiagnostics = false
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
        case libraryPath
        case viewMode
        case sortOption
        case showsDemoLibrary
        case outputDeviceName
        case outputDeviceID
        case inputDeviceID
        case audioRoutingMode
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
        case showAdvancedDiagnostics
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
        libraryPath = try container.decodeIfPresent(String.self, forKey: .libraryPath) ?? libraryPath
        viewMode = try container.decodeIfPresent(ViewMode.self, forKey: .viewMode) ?? viewMode
        sortOption = try container.decodeIfPresent(SoundSortOption.self, forKey: .sortOption) ?? sortOption
        showsDemoLibrary = try container.decodeIfPresent(Bool.self, forKey: .showsDemoLibrary) ?? showsDemoLibrary
        outputDeviceName = try container.decodeIfPresent(String.self, forKey: .outputDeviceName) ?? outputDeviceName
        outputDeviceID = try container.decodeIfPresent(String.self, forKey: .outputDeviceID) ?? outputDeviceID
        inputDeviceID = try container.decodeIfPresent(String.self, forKey: .inputDeviceID)
        audioRoutingMode = try container.decodeIfPresent(AudioRoutingMode.self, forKey: .audioRoutingMode) ?? audioRoutingMode
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
        showAdvancedDiagnostics = try container.decodeIfPresent(Bool.self, forKey: .showAdvancedDiagnostics) ?? showAdvancedDiagnostics
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
    }
}

enum AudioDeviceKind: String, Codable, Hashable {
    case input
    case output
    case virtual
}

struct AudioDevice: Identifiable, Hashable, Codable {
    let id: String
    var name: String
    var kind: AudioDeviceKind
    var isDefault: Bool
    var isVirtual: Bool

    static let systemOutput = AudioDevice(
        id: "system-output",
        name: "System Default",
        kind: .output,
        isDefault: true,
        isVirtual: false
    )
}

enum AudioRoutingMode: String, CaseIterable, Identifiable, Codable {
    case speakerOnly
    case speakerAndMonitor
    case virtualDevice
    case microphonePassthrough

    var id: String { rawValue }

    var title: String {
        switch self {
        case .speakerOnly: "Speaker Only"
        case .speakerAndMonitor: "Speaker + Monitor"
        case .virtualDevice: "Virtual Device Output"
        case .microphonePassthrough: "Microphone Passthrough Mix"
        }
    }

    var isImplemented: Bool {
        switch self {
        case .speakerOnly: true
        case .speakerAndMonitor, .virtualDevice, .microphonePassthrough: false
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
