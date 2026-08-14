import AppKit
import Foundation
import SwiftUI
import UniformTypeIdentifiers

struct ShortcutCaptureRequest: Identifiable, Equatable {
    let id = UUID()
    let clipID: SoundClip.ID
}

struct CategoryEditorRequest: Identifiable, Equatable {
    let id = UUID()
    let categoryID: String?
    let clipIDsToAssign: [SoundClip.ID]
}

enum ShortcutAssignmentResult: Equatable {
    case assigned
    case conflict(SoundClip)
    case invalid(String)
    case registrationFailed(String)
    case persistenceFailed(String)
    case notFound
}

enum AppPersistenceError: LocalizedError {
    case metadata(String)
    case migrationBackup

    var errorDescription: String? {
        switch self {
        case .metadata(let message): message
        case .migrationBackup:
            "Cuelet could not create a safety copy of the existing macOS metadata, so migration was stopped before changing it."
        }
    }
}

@MainActor
final class AppState: ObservableObject {
    enum SidebarItem: Hashable, Identifiable {
        case library
        case favorites
        case recent
        case allCategories
        case category(SoundCategory)

        var id: String {
            switch self {
            case .library: "library"
            case .favorites: "favorites"
            case .recent: "recent"
            case .allCategories: "allCategories"
            case .category(let category): "category-\(category.id)"
            }
        }

        var title: String {
            switch self {
            case .library: "Library"
            case .favorites: "Favorites"
            case .recent: "Recent"
            case .allCategories: "All Categories"
            case .category(let category): category.name
            }
        }

        var systemImage: String {
            switch self {
            case .library: "rectangle.grid.2x2"
            case .favorites: "star"
            case .recent: "clock"
            case .allCategories: "square.grid.2x2"
            case .category(let category): category.systemImage
            }
        }

        var libraryFilter: LibraryFilter? {
            switch self {
            case .library: .all
            case .favorites: .favorites
            case .recent: .recent
            case .allCategories: .allCategories
            case .category(let category): .category(category)
            }
        }
    }

    @Published var selectedSidebarItem: SidebarItem = .library {
        didSet {
            guard selectedSidebarItem != oldValue else { return }
            synchronizeSelectionWithVisibleClips()
        }
    }
    @Published var searchText = "" {
        didSet {
            guard searchText != oldValue else { return }
            synchronizeSelectionWithVisibleClips()
        }
    }
    @Published var selectedSoundIDs: Set<SoundClip.ID> = []
    @Published var focusedSoundID: SoundClip.ID?
    @Published var selectionAnchorSoundID: SoundClip.ID?
    @Published var clips: [SoundClip] = [] {
        didSet {
            guard clips != oldValue else { return }
            synchronizeSelectionWithVisibleClips()
        }
    }
    @Published var playbackState = PlaybackState()
    @Published var settings = CueletSettings() {
        didSet {
            if settingsPersistenceEnabled, !settingsStore.save(settings) {
                persistenceStatusMessage = "Cuelet could not save settings. Existing settings were left recoverable on disk."
            }
            playbackService.setVolume(settings.soundboardVolume)
        }
    }
    @Published var viewMode = ViewMode.grid {
        didSet {
            guard viewMode != oldValue else { return }
            settings.viewMode = viewMode
        }
    }
    @Published var sortOption = SoundSortOption.nameAscending {
        didSet {
            guard sortOption != oldValue else { return }
            settings.sortOption = sortOption
            synchronizeSelectionWithVisibleClips()
        }
    }
    @Published var outputDevices: [AudioDevice] = [.systemOutput]
    @Published var inputDevices: [AudioDevice] = []
    @Published private(set) var audioRouteStatus = AudioRouteStatus.applyingSystemOutput
    @Published private(set) var virtualAudioDriverStatus = CueletVirtualAudioDriverStatus.notInstalled
    @Published var microphonePermissionState: MicrophonePermissionState = .unknown
    @Published var inputLevelState: InputLevelState = .inactive
    @Published var audioStatusMessage = ""
    @Published var searchFocusRequestID = UUID()
    @Published var shortcutCaptureRequest: ShortcutCaptureRequest?
    @Published var categoryEditorRequest: CategoryEditorRequest?
    @Published private(set) var globalShortcutStatusMessage = "No global shortcuts assigned"
    @Published private(set) var persistenceStatusMessage = ""
    private weak var mainWindow: NSWindow?
    private var didApplyStartupVisibility = false
    private var settingsPersistenceEnabled = false
    private var loadedMetadataVersion: Int?
    private var ignoredManagedPaths: Set<String> = []
    private var globalShortcutRegistrationsSuspendedForCapture = false

    let libraryService: LibraryService
    let playbackService: PlaybackService
    let settingsStore: SettingsStore
    let searchService = SearchService()
    let profileService = ProfileService()
    let audioDeviceService: AudioDeviceProviding
    let virtualAudioDriverService: CueletVirtualAudioDriverServicing
    let audioPermissionService = AudioPermissionService()
    let microphoneService = MicrophoneService()
    let launchAtLoginService = LaunchAtLoginService()
    private let globalShortcutService: GlobalShortcutRegistering
    private let localKeyboardShortcutService: LocalKeyboardShortcutService?

    init(
        settingsStore: SettingsStore = SettingsStore(),
        libraryService: LibraryService = LibraryService(),
        playbackService: PlaybackService = PlaybackService(),
        audioDeviceService: AudioDeviceProviding? = nil,
        virtualAudioDriverService: CueletVirtualAudioDriverServicing? = nil,
        globalShortcutService: GlobalShortcutRegistering = CarbonGlobalShortcutService(),
        installKeyboardShortcuts: Bool = true
    ) {
        self.settingsStore = settingsStore
        self.libraryService = libraryService
        self.playbackService = playbackService
        let resolvedAudioDeviceService = audioDeviceService ?? AudioDeviceService()
        self.audioDeviceService = resolvedAudioDeviceService
        self.virtualAudioDriverService = virtualAudioDriverService
            ?? CueletVirtualAudioDriverService(deviceProvider: resolvedAudioDeviceService)
        self.globalShortcutService = globalShortcutService
        self.localKeyboardShortcutService = installKeyboardShortcuts ? LocalKeyboardShortcutService() : nil

        switch settingsStore.loadResult() {
        case .missing(let loaded), .loaded(let loaded):
            settings = loaded
            settingsPersistenceEnabled = true
        case .recovered(let loaded, let primaryError):
            settings = loaded
            settingsPersistenceEnabled = true
            if settingsStore.save(loaded, preservePrimaryAsBackup: false) {
                persistenceStatusMessage = "Recovered settings from the safety copy. The unreadable primary was replaced; the recovery copy remains available."
            } else {
                persistenceStatusMessage = "Loaded the settings recovery copy, but could not restore the primary: \(primaryError)"
            }
        case .failure(let message):
            settings = CueletSettings()
            settingsPersistenceEnabled = false
            persistenceStatusMessage = message
        }
#if DEBUG
        if let isolatedLibraryPath = ProcessInfo.processInfo.environment["CUELET_LIBRARY_PATH"],
           !isolatedLibraryPath.isEmpty {
            settings.libraryPath = NSString(string: isolatedLibraryPath).expandingTildeInPath
        }
#endif
        viewMode = settings.viewMode
        sortOption = settings.sortOption
        playbackService.playbackDidFinish = { [weak self] clipID in
            self?.handlePlaybackFinished(clipID)
        }
        playbackService.outputRouteDidConfirm = { [weak self] requestedUID, actualUID in
            self?.handleOutputRouteConfirmation(requestedUID: requestedUID, actualUID: actualUID)
        }
        globalShortcutService.setHandler { [weak self] clipID in
            Task { @MainActor in
                self?.playClipFromGlobalShortcut(clipID)
            }
        }

        if let localKeyboardShortcutService {
            localKeyboardShortcutService.install(
                handlers: LocalKeyboardShortcutService.Handlers(
                    isSoundboardShortcutAvailable: { [weak self] in
                        self?.canHandleLocalSoundboardShortcuts == true
                    },
                    playSelected: { [weak self] in
                        self?.playSelectedVisibleSoundFromKeyboard() ?? false
                    },
                    stopOrClearSelection: { [weak self] in
                        self?.handleEscapeFromKeyboard() ?? false
                    },
                    moveSelection: { [weak self] direction, extendsSelection in
                        self?.selectNextVisibleClipFromKeyboard(
                            direction: direction,
                            extendsSelection: extendsSelection
                        ) ?? false
                    },
                    selectAll: { [weak self] in
                        self?.selectAllVisibleSoundsFromKeyboard() ?? false
                    },
                    playAssignedShortcut: { [weak self] shortcut in
                        self?.playSoundShortcut(shortcut) ?? false
                    }
                )
            )
        }

        self.audioDeviceService.startObserving { [weak self] in
            self?.refreshAudioRouting()
        }
        refreshAudioRouting()
        loadInitialLibrary()
        refreshGlobalShortcutRegistrations()
    }

    var selectedClipID: SoundClip.ID? {
        get {
            selectedClip?.id
        }
        set {
            guard let newValue else {
                clearSelection()
                return
            }

            selectedSoundIDs = [newValue]
            focusedSoundID = newValue
            selectionAnchorSoundID = newValue
        }
    }

    var selectedClip: SoundClip? {
        if let focusedSoundID,
           selectedSoundIDs.contains(focusedSoundID),
           let focusedClip = clips.first(where: { $0.id == focusedSoundID }) {
            return focusedClip
        }

        return selectedVisibleClips.first ?? clips.first { selectedSoundIDs.contains($0.id) }
    }

    var selectedVisibleClips: [SoundClip] {
        visibleClips.filter { selectedSoundIDs.contains($0.id) }
    }

    var activeLibraryFilter: LibraryFilter {
        selectedSidebarItem.libraryFilter ?? .all
    }

    var visibleClips: [SoundClip] {
        sortedClips(searchService.filter(clips: clips, searchText: searchText, filter: activeLibraryFilter))
    }

    var categories: [SoundCategory] {
        uniquedCategories([SoundCategory.uncategorized] + settings.customCategories)
    }

    var assignableCategories: [SoundCategory] {
        uniquedCategories([SoundCategory.uncategorized] + settings.customCategories)
    }

    var librarySubtitle: String {
        return settings.libraryPath.isEmpty ? "No library selected" : settings.libraryPath
    }

    var navigationTitle: String {
        selectedSidebarItem.title
    }

    var visibleSoundCountText: String {
        let count = visibleClips.count
        return count == 1 ? "1 sound" : "\(count) sounds"
    }

    var playingClips: [SoundClip] {
        let activeIDs = playbackState.playingClipIDs
        return clips
            .filter { activeIDs.contains($0.id) }
            .sorted { lhs, rhs in
                let lhsDate = playbackState.playbackStartDatesByClipID[lhs.id] ?? .distantPast
                let rhsDate = playbackState.playbackStartDatesByClipID[rhs.id] ?? .distantPast
                if lhsDate == rhsDate {
                    return lhs.displayName.localizedStandardCompare(rhs.displayName) == .orderedAscending
                }
                return lhsDate > rhsDate
            }
    }

    var mostRecentPlayingClip: SoundClip? {
        guard let clipID = playbackState.mostRecentPlayingClipID else { return nil }
        return clips.first { $0.id == clipID }
    }

    private var canHandleLocalSoundboardShortcuts: Bool {
        guard let mainWindow, NSApp.keyWindow === mainWindow else { return false }
        guard shortcutCaptureRequest == nil, categoryEditorRequest == nil else { return false }
        return selectedSidebarItem.libraryFilter != nil && !visibleClips.isEmpty
    }

    func registerMainWindow(_ window: NSWindow?) {
        guard mainWindow !== window else { return }
        mainWindow = window
        window?.isReleasedWhenClosed = false
    }

    func showMainWindow() {
        NSApp.activate(ignoringOtherApps: true)
        mainWindow?.makeKeyAndOrderFront(nil)
    }

    func applyStartupVisibilityIfNeeded() {
        guard !didApplyStartupVisibility else { return }
        didApplyStartupVisibility = true
        if settings.startsHidden && settings.keepsRunningAfterWindowClose {
            mainWindow?.orderOut(nil)
        }
    }

    func setLaunchesAtLogin(_ isEnabled: Bool) {
        do {
            try launchAtLoginService.setEnabled(isEnabled)
            settings.launchesAtLogin = isEnabled
        } catch {
            settings.launchesAtLogin = launchAtLoginService.isEnabled
            showError("Could not update Launch at Login: \(error.localizedDescription)")
        }
    }

    func prepareForTermination() {
        stopAllPlayback()
        audioDeviceService.stopObserving()
        microphoneService.stopMonitoring { [weak self] state in
            self?.inputLevelState = state
        }
        globalShortcutService.unregisterAll()
        updateGlobalShortcutStatus()
    }

    func chooseLibrary() {
        let panel = NSOpenPanel()
        panel.title = "Choose Sound Library"
        panel.message = "Choose a folder containing audio files."
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = false

        guard panel.runModal() == .OK, let url = panel.url else { return }
        loadLibrary(at: url)
    }

    func importSounds() {
        let panel = NSOpenPanel()
        panel.title = "Import Sounds"
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.audio]

        guard panel.runModal() == .OK else { return }

        let choice = NSAlert()
        choice.messageText = "How should Cuelet import these sounds?"
        choice.informativeText = "Copy keeps a durable file in Cuelet's managed library. Link preserves the original location and remembers access to it."
        let preferredMode: LibraryService.ImportMode = settings.copiesImportedFiles ? .copy : .link
        choice.addButton(withTitle: preferredMode == .copy ? "Copy into Cuelet Library" : "Link External Files")
        choice.addButton(withTitle: preferredMode == .copy ? "Link External Files" : "Copy into Cuelet Library")
        choice.addButton(withTitle: "Cancel")

        let response = choice.runModal()
        let mode: LibraryService.ImportMode
        switch response {
        case .alertFirstButtonReturn: mode = preferredMode
        case .alertSecondButtonReturn: mode = preferredMode == .copy ? .link : .copy
        default: return
        }
        settings.copiesImportedFiles = mode == .copy

        do {
            let result = try importSounds(panel.urls, mode: mode)
            if !result.duplicates.isEmpty {
                let duplicateAlert = NSAlert()
                duplicateAlert.alertStyle = .informational
                duplicateAlert.messageText = result.duplicates.count == 1
                    ? "That sound is already in Cuelet"
                    : "\(result.duplicates.count) sounds are already in Cuelet"
                duplicateAlert.informativeText = "Cuelet did not create duplicate library entries or overwrite any files."
                duplicateAlert.addButton(withTitle: "OK")
                duplicateAlert.runModal()
            }
        } catch {
            showError(error.localizedDescription)
        }
    }

    @discardableResult
    func importSounds(_ urls: [URL], mode: LibraryService.ImportMode) throws -> LibraryService.ImportResult {
        let libraryURL = try ensureManagedLibrary()
        let result = try libraryService.importFiles(
            urls,
            mode: mode,
            libraryURL: libraryURL,
            existingClips: clips
        )
        guard !result.imported.isEmpty else { return result }

        let updatedClips = clips + result.imported
        do {
            try persistLibraryMetadata(clips: updatedClips, libraryURL: libraryURL)
        } catch {
            libraryService.rollbackCreatedManagedFiles(result.createdManagedFiles, libraryURL: libraryURL)
            throw error
        }

        clips = updatedClips
        selectedSidebarItem = .library
        refreshGlobalShortcutRegistrations()
        return result
    }

    func rescanLibrary() {
        guard !settings.libraryPath.isEmpty else { return }
        loadLibrary(at: URL(fileURLWithPath: expandedPath(settings.libraryPath)))
    }

    func loadLibrary(at folderURL: URL, presentsErrors: Bool = true) {
        do {
            let metadataStore = LibraryMetadataStore(libraryURL: folderURL)
            let loadedClips: [SoundClip]

            switch metadataStore.load() {
            case .missing:
                ignoredManagedPaths = []
                let scanned = try libraryService.scanLibrary(
                    at: folderURL,
                    scansSubfolders: settings.scansSubfolders
                )
                loadedClips = applyStoredClipMetadata(to: scanned)
                let hasLegacySoundMetadata = !settings.soundCategoryAssignments.isEmpty
                    || !settings.soundShortcutAssignments.isEmpty
                    || !settings.favoriteSoundIDs.isEmpty
                if hasLegacySoundMetadata, !settingsStore.backupLegacyMetadataIfNeeded() {
                    throw AppPersistenceError.migrationBackup
                }
                try persistLibraryMetadata(clips: loadedClips, libraryURL: folderURL)
                if hasLegacySoundMetadata {
                    settings.libraryMetadataMigrationVersion = LibraryMetadataDocument.currentVersion
                    persistenceStatusMessage = "Migrated existing macOS sound metadata to schema version \(LibraryMetadataDocument.currentVersion). A safety copy of settings.json was retained."
                }
            case .loaded(let document, let migratedFromVersion):
                loadedMetadataVersion = migratedFromVersion
                ignoredManagedPaths = document.ignoredManagedPaths
                applyLibraryCategories(document.categories)
                loadedClips = try libraryService.loadLibrary(
                    at: folderURL,
                    scansSubfolders: settings.scansSubfolders,
                    metadata: document
                )
                try persistLibraryMetadata(clips: loadedClips, libraryURL: folderURL)
                if let migratedFromVersion {
                    persistenceStatusMessage = "Migrated library metadata from version \(migratedFromVersion) to version \(LibraryMetadataDocument.currentVersion). The original is preserved as .v1.bak."
                }
            case .recovered(let document, let primaryError):
                try metadataStore.restoreRecoveredDocument(document)
                ignoredManagedPaths = document.ignoredManagedPaths
                applyLibraryCategories(document.categories)
                loadedClips = try libraryService.loadLibrary(
                    at: folderURL,
                    scansSubfolders: settings.scansSubfolders,
                    metadata: document
                )
                persistenceStatusMessage = "Recovered the library from .cuelet-metadata.json.backup. The damaged primary reported: \(primaryError)"
            case .failure(let message):
                throw AppPersistenceError.metadata(message)
            }

            if settings.stopOnLibraryChange {
                stopAllPlayback()
            }
            settings.libraryPath = folderURL.path
            clips = loadedClips
            selectedSidebarItem = .library
            selectedClipID = nil
            refreshGlobalShortcutRegistrations()
        } catch {
            if presentsErrors {
                showError(error.localizedDescription)
            } else {
                persistenceStatusMessage = error.localizedDescription
            }
        }
    }

    func setScansSubfolders(_ scansSubfolders: Bool) {
        settings.scansSubfolders = scansSubfolders
        if !settings.libraryPath.isEmpty {
            rescanLibrary()
        }
    }

    func requestSearchFocus() {
        selectedSidebarItem = .library
        searchFocusRequestID = UUID()
    }

    func clearSearchOrStopAll() {
        if !searchText.isEmpty {
            searchText = ""
        } else if !selectedSoundIDs.isEmpty {
            clearSelection()
        } else {
            stopAllPlayback()
        }
    }

    func isSelected(_ clip: SoundClip) -> Bool {
        selectedSoundIDs.contains(clip.id)
    }

    func isFocused(_ clip: SoundClip) -> Bool {
        focusedSoundID == clip.id
    }

    func select(_ clip: SoundClip) {
        selectedSoundIDs = [clip.id]
        focusedSoundID = clip.id
        selectionAnchorSoundID = clip.id
    }

    func select(_ clip: SoundClip, modifiers: NSEvent.ModifierFlags) {
        if modifiers.contains(.shift) {
            extendSelection(to: clip)
        } else if modifiers.contains(.command) {
            toggleSelection(of: clip)
        } else {
            select(clip)
        }
    }

    func toggleSelection(of clip: SoundClip) {
        if selectedSoundIDs.contains(clip.id) {
            selectedSoundIDs.remove(clip.id)
        } else {
            selectedSoundIDs.insert(clip.id)
            selectionAnchorSoundID = clip.id
        }

        focusedSoundID = clip.id

        if selectedSoundIDs.isEmpty {
            selectionAnchorSoundID = nil
        } else if selectionAnchorSoundID == nil || !selectedSoundIDs.contains(selectionAnchorSoundID!) {
            selectionAnchorSoundID = firstSelectedVisibleClipID() ?? selectedSoundIDs.first
        }
    }

    func extendSelection(to clip: SoundClip) {
        let visible = visibleClips
        guard let targetIndex = visible.firstIndex(where: { $0.id == clip.id }) else {
            select(clip)
            return
        }

        let anchorID = validSelectionAnchor(in: visible) ?? clip.id
        guard let anchorIndex = visible.firstIndex(where: { $0.id == anchorID }) else {
            select(clip)
            return
        }

        let bounds = min(anchorIndex, targetIndex)...max(anchorIndex, targetIndex)
        selectedSoundIDs = Set(bounds.map { visible[$0].id })
        focusedSoundID = clip.id
        selectionAnchorSoundID = anchorID
    }

    func clearSelection() {
        selectedSoundIDs.removeAll()
        focusedSoundID = nil
        selectionAnchorSoundID = nil
    }

    func resignTextInputFocus() {
        guard let window = NSApplication.shared.keyWindow,
              LocalKeyboardShortcutService.isTextInputOrEditableResponder(window.firstResponder) else {
            return
        }
        window.makeFirstResponder(nil)
    }

    func selectAllVisibleSounds() {
        let visible = visibleClips
        selectedSoundIDs = Set(visible.map(\.id))
        focusedSoundID = visible.first?.id
        selectionAnchorSoundID = visible.first?.id
    }

    func prepareContextMenu(for clip: SoundClip) {
        resignTextInputFocus()

        guard !selectedSoundIDs.contains(clip.id) else {
            focusedSoundID = clip.id
            if selectionAnchorSoundID == nil {
                selectionAnchorSoundID = clip.id
            }
            return
        }

        select(clip)
    }

    func contextMenuTargetClips(for clip: SoundClip) -> [SoundClip] {
        if selectedSoundIDs.contains(clip.id) {
            let selected = selectedVisibleClips
            return selected.isEmpty ? [clip] : selected
        }

        return [clip]
    }

    func selectNextVisibleClip(direction: LocalKeyboardShortcutService.SelectionDirection, extendsSelection: Bool = false) {
        let clips = visibleClips
        guard !clips.isEmpty else { return }

        guard let currentIndex = focusedVisibleIndex(in: clips) else {
            select(clips[0])
            return
        }

        let columnStep = viewMode == .grid ? 3 : 1
        let nextIndex: Int
        switch direction {
        case .left:
            nextIndex = max(currentIndex - 1, 0)
        case .right:
            nextIndex = min(currentIndex + 1, clips.count - 1)
        case .up:
            nextIndex = max(currentIndex - columnStep, 0)
        case .down:
            nextIndex = min(currentIndex + columnStep, clips.count - 1)
        }

        if extendsSelection {
            extendSelection(to: clips[nextIndex])
        } else {
            select(clips[nextIndex])
        }
    }

    func selectNextVisibleClipFromKeyboard(
        direction: LocalKeyboardShortcutService.SelectionDirection,
        extendsSelection: Bool = false
    ) -> Bool {
        guard canHandleLocalSoundboardShortcuts else { return false }
        selectNextVisibleClip(direction: direction, extendsSelection: extendsSelection)
        return true
    }

    func selectAllVisibleSoundsFromKeyboard() -> Bool {
        guard canHandleLocalSoundboardShortcuts else { return false }
        selectAllVisibleSounds()
        return true
    }

    func playSelectedVisibleSound() {
        guard let selectedClip,
              visibleClips.contains(where: { $0.id == selectedClip.id }) else {
            return
        }

        _ = startPlayback(selectedClip)
    }

    func playRecommendedSearchResult() -> Bool {
        guard !visibleClips.isEmpty else {
            NSSound.beep()
            return false
        }

        let clipToPlay = selectedClip.flatMap { selectedClip in
            visibleClips.first { $0.id == selectedClip.id }
        } ?? visibleClips[0]

        play(clipToPlay)
        return true
    }

    func playSelectedVisibleSoundFromKeyboard() -> Bool {
        guard canHandleLocalSoundboardShortcuts else { return false }
        playSelectedVisibleSound()
        return true
    }

    func playSoundShortcut(_ shortcut: SoundShortcut) -> Bool {
        guard let clip = clips.first(where: {
            $0.shortcut?.scope == .local
                && $0.shortcut?.isEnabled == true
                && $0.shortcut?.hasSameKeyCombination(as: shortcut) == true
        }) else {
            return false
        }
        return startPlayback(clip)
    }

    func togglePlayback(for clip: SoundClip) {
        if playbackState.playingClipIDs.contains(clip.id) {
            stop(clip)
        } else {
            _ = startPlayback(clip)
        }
    }

    func play(_ clip: SoundClip) {
        _ = startPlayback(clip)
    }

    func play(_ clipsToPlay: [SoundClip]) {
        clipsToPlay.forEach { clip in
            _ = startPlayback(clip)
        }
    }

    func performPrimaryPlaybackAction(for clip: SoundClip) {
        if playbackState.playingClipIDs.contains(clip.id) {
            stop(clip)
        } else {
            play(clip)
        }
    }

    func stop(_ clip: SoundClip) {
        playbackService.stop(clip: clip, playbackState: &playbackState)
        updateIdleAudioRouteStatusIfNeeded()
    }

    func pause(_ clip: SoundClip) {
        playbackService.pause(clip: clip, playbackState: &playbackState)
    }

    func resume(_ clip: SoundClip) {
        _ = playbackService.resume(clip: clip, playbackState: &playbackState)
    }

    func isPaused(_ clip: SoundClip) -> Bool {
        playbackState.isPaused(clip.id)
    }

    func stop(_ clipsToStop: [SoundClip]) {
        clipsToStop.forEach { stop($0) }
    }

    func playSelectedSound() {
        guard let selectedClip else { return }
        _ = startPlayback(selectedClip)
    }

    func stopAllPlayback() {
        playbackService.stopAll(playbackState: &playbackState)
        updateIdleAudioRouteStatusIfNeeded()
    }

    func handleEscapeFromKeyboard() -> Bool {
        if !searchText.isEmpty {
            searchText = ""
            return true
        }
        if !selectedSoundIDs.isEmpty {
            clearSelection()
            return true
        }
        if playbackState.isPlaying {
            stopAllPlayback()
            return true
        }
        return false
    }

    func toggleFavorite(_ clip: SoundClip) {
        let previousClips = clips
        let previousSettings = settings
        guard let index = clips.firstIndex(where: { $0.id == clip.id }) else { return }
        clips[index].isFavorite.toggle()
        guard let key = assignmentKey(for: clips[index]) else { return }
        if clips[index].isFavorite {
            settings.favoriteSoundIDs.insert(key)
        } else {
            settings.favoriteSoundIDs.remove(key)
        }
        guard persistCurrentLibraryMetadata() else {
            clips = previousClips
            settings = previousSettings
            return
        }
    }

    func setFavorite(_ clipsToUpdate: [SoundClip], isFavorite: Bool) {
        let previousClips = clips
        let previousSettings = settings
        for clip in clipsToUpdate {
            guard let index = clips.firstIndex(where: { $0.id == clip.id }) else { continue }
            clips[index].isFavorite = isFavorite

            guard let key = assignmentKey(for: clips[index]) else { continue }
            if isFavorite {
                settings.favoriteSoundIDs.insert(key)
            } else {
                settings.favoriteSoundIDs.remove(key)
            }
        }
        guard persistCurrentLibraryMetadata() else {
            clips = previousClips
            settings = previousSettings
            return
        }
    }

    func assign(_ clip: SoundClip, to category: SoundCategory) {
        let previousClips = clips
        let previousSettings = settings
        guard let index = clips.firstIndex(where: { $0.id == clip.id }) else { return }
        let resolvedCategory = categoryByID(category.id) ?? category

        var updatedClips = clips
        updatedClips[index].category = resolvedCategory
        clips = updatedClips

        guard let key = assignmentKey(for: updatedClips[index]) else { return }
        var updatedSettings = settings
        if resolvedCategory.id == SoundCategory.uncategorized.id {
            updatedSettings.soundCategoryAssignments[key] = nil
        } else {
            updatedSettings.soundCategoryAssignments[key] = resolvedCategory.id
        }
        settings = updatedSettings
        guard persistCurrentLibraryMetadata() else {
            clips = previousClips
            settings = previousSettings
            return
        }
    }

    func assign(_ clipsToAssign: [SoundClip], to category: SoundCategory) {
        clipsToAssign.forEach { assign($0, to: category) }
    }

    func updateShortcut(for clip: SoundClip) {
        beginShortcutCapture(for: clip)
    }

    func beginShortcutCapture(for clip: SoundClip) {
        if !globalShortcutRegistrationsSuspendedForCapture {
            globalShortcutService.unregisterAll()
            globalShortcutRegistrationsSuspendedForCapture = true
            globalShortcutStatusMessage = "Global shortcuts paused while editing"
        }
        shortcutCaptureRequest = ShortcutCaptureRequest(clipID: clip.id)
    }

    func dismissShortcutCapture() {
        shortcutCaptureRequest = nil
        guard globalShortcutRegistrationsSuspendedForCapture else { return }
        globalShortcutRegistrationsSuspendedForCapture = false
        refreshGlobalShortcutRegistrations()
    }

    func clip(withID clipID: SoundClip.ID) -> SoundClip? {
        clips.first { $0.id == clipID }
    }

    func conflictingClip(for shortcut: SoundShortcut, excluding excludedClipID: SoundClip.ID) -> SoundClip? {
        clips.first { clip in
            clip.id != excludedClipID
                && clip.shortcut?.isEnabled == true
                && clip.shortcut?.hasSameKeyCombination(as: shortcut) == true
        }
    }

    @discardableResult
    func assignShortcut(
        _ shortcut: SoundShortcut,
        to clip: SoundClip,
        replacingConflicts: Bool
    ) -> ShortcutAssignmentResult {
        assignShortcut(shortcut, to: clip.id, replacingConflicts: replacingConflicts)
    }

    @discardableResult
    func assignShortcut(
        _ shortcut: SoundShortcut,
        to clipID: SoundClip.ID,
        replacingConflicts: Bool
    ) -> ShortcutAssignmentResult {
        assignShortcutTransactional(shortcut, to: clipID, replacingConflicts: replacingConflicts)
    }

    @discardableResult
    func assignShortcutTransactional(
        _ candidate: SoundShortcut?,
        to clipID: SoundClip.ID,
        replacingConflicts: Bool
    ) -> ShortcutAssignmentResult {
        let shortcut = candidate?.normalized()
        if let shortcut, !ShortcutCaptureService.validationResult(for: shortcut).isValid {
            if case .invalid(let message) = ShortcutCaptureService.validationResult(for: shortcut) {
                return .invalid(message)
            }
            return .invalid("That shortcut cannot be assigned.")
        }

        guard let targetIndex = clips.firstIndex(where: { $0.id == clipID }) else {
            return .notFound
        }

        if let shortcut,
           let conflict = conflictingClip(for: shortcut, excluding: clipID),
           !replacingConflicts {
            return .conflict(conflict)
        }

        var updatedClips = clips
        var updatedSettings = settings

        if let shortcut, let conflictIndex = updatedClips.firstIndex(where: {
            $0.id != clipID
                && $0.shortcut?.isEnabled == true
                && $0.shortcut?.hasSameKeyCombination(as: shortcut) == true
        }) {
            if let key = assignmentKey(for: updatedClips[conflictIndex]) {
                updatedSettings.soundShortcutAssignments[key] = nil
            }
            updatedClips[conflictIndex].shortcut = nil
        }

        updatedClips[targetIndex].shortcut = shortcut
        if let key = assignmentKey(for: updatedClips[targetIndex]) {
            updatedSettings.soundShortcutAssignments[key] = shortcut
        }

        switch globalShortcutService.tryUpdate(globalAssignments(from: updatedClips)) {
        case .success:
            break
        case .failure(let error):
            updateGlobalShortcutStatus()
            return .registrationFailed(error.localizedDescription)
        }

        guard settingsStore.save(updatedSettings) else {
            let restoreResult = globalShortcutService.tryUpdate(globalAssignments(from: clips))
            updateGlobalShortcutStatus()
            switch restoreResult {
            case .success:
                return .persistenceFailed("Could not save shortcut metadata. The previous shortcut was restored.")
            case .failure:
                return .persistenceFailed(
                    "Could not save shortcut metadata, and the previous global registration could not be restored."
                )
            }
        }

        do {
            try persistLibraryMetadata(clips: updatedClips)
        } catch {
            _ = settingsStore.save(settings)
            let restoreResult = globalShortcutService.tryUpdate(globalAssignments(from: clips))
            updateGlobalShortcutStatus()
            switch restoreResult {
            case .success:
                return .persistenceFailed("Could not save shortcut metadata. The previous shortcut was restored.")
            case .failure:
                return .persistenceFailed(
                    "Could not save shortcut metadata, and the previous global registration could not be restored."
                )
            }
        }

        clips = updatedClips
        settings = updatedSettings
        updateGlobalShortcutStatus()
        return .assigned
    }

    func clearShortcut(for clip: SoundClip) {
        clearShortcut(for: clip.id)
    }

    func clearShortcut(for clipID: SoundClip.ID) {
        _ = assignShortcutTransactional(nil, to: clipID, replacingConflicts: false)
    }

    func rename(_ clip: SoundClip) {
        guard clips.contains(where: { $0.id == clip.id }) else { return }

        let alert = NSAlert()
        alert.messageText = "Rename Sound in Cuelet"
        alert.informativeText = clip.storageMode == .linked
            ? "This changes only the Cuelet display name. The linked external file will not be renamed."
            : "This changes only the Cuelet display name. The managed audio filename will stay unchanged."
        alert.addButton(withTitle: "Rename")
        alert.addButton(withTitle: "Cancel")

        let textField = NSTextField(string: clip.displayName)
        textField.frame = NSRect(x: 0, y: 0, width: 280, height: 24)
        alert.accessoryView = textField

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let name = textField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty else { return }

        _ = renameDisplayName(clip, to: name)
    }

    @discardableResult
    func renameDisplayName(_ clip: SoundClip, to proposedName: String) -> Bool {
        let name = proposedName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty, let index = clips.firstIndex(where: { $0.id == clip.id }) else { return false }
        let previousClips = clips
        clips[index].name = name
        guard persistCurrentLibraryMetadata() else {
            clips = previousClips
            return false
        }
        return true
    }

    func editNotesAndAliases(_ clip: SoundClip) {
        guard clips.contains(where: { $0.id == clip.id }) else { return }
        let alert = NSAlert()
        alert.messageText = "Edit Sound Details"
        alert.informativeText = "Notes and aliases are searchable in Cuelet. Separate aliases with commas."
        alert.addButton(withTitle: "Save")
        alert.addButton(withTitle: "Cancel")

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        let notesLabel = NSTextField(labelWithString: "Notes")
        let notesField = NSTextField(string: clip.notes)
        notesField.placeholderString = "Optional notes"
        notesField.setAccessibilityLabel("Sound notes")
        let aliasesLabel = NSTextField(labelWithString: "Aliases")
        let aliasesField = NSTextField(string: clip.aliases.joined(separator: ", "))
        aliasesField.placeholderString = "boom, impact, sting"
        aliasesField.setAccessibilityLabel("Sound aliases")
        [notesLabel, notesField, aliasesLabel, aliasesField].forEach {
            $0.translatesAutoresizingMaskIntoConstraints = false
            stack.addArrangedSubview($0)
        }
        notesField.widthAnchor.constraint(equalToConstant: 320).isActive = true
        aliasesField.widthAnchor.constraint(equalToConstant: 320).isActive = true
        alert.accessoryView = stack

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let aliases = aliasesField.stringValue
            .split(separator: ",")
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }
        _ = updateDetails(clip, notes: notesField.stringValue, aliases: aliases)
    }

    @discardableResult
    func updateDetails(_ clip: SoundClip, notes: String, aliases: [String]) -> Bool {
        guard let index = clips.firstIndex(where: { $0.id == clip.id }) else { return false }
        let previous = clips[index]
        clips[index].notes = notes.trimmingCharacters(in: .whitespacesAndNewlines)
        clips[index].aliases = Array(Set(aliases.map {
            $0.trimmingCharacters(in: .whitespacesAndNewlines)
        }.filter { !$0.isEmpty })).sorted { $0.localizedStandardCompare($1) == .orderedAscending }
        guard persistCurrentLibraryMetadata() else {
            clips[index] = previous
            return false
        }
        return true
    }

    func revealInFinder(_ clip: SoundClip) {
        revealInFinder([clip])
    }

    func revealInFinder(_ clips: [SoundClip]) {
        let urls = finderRevealURLs(for: clips)
        guard !urls.isEmpty else {
            showError("The selected sound file is missing. Rescan the library and try again.")
            return
        }
        NSWorkspace.shared.activateFileViewerSelecting(urls)
    }

    func finderRevealURLs(for clips: [SoundClip]) -> [URL] {
        uniquedClips(clips).compactMap(\.fileURL).filter {
            FileManager.default.fileExists(atPath: $0.path)
        }
    }

    func removeFromLibrary(_ clip: SoundClip) {
        removeFromLibrary([clip])
    }

    func removeFromLibrary(_ clipsToRemove: [SoundClip]) {
        let uniqueClips = uniquedClips(clipsToRemove)
        guard !uniqueClips.isEmpty else { return }

        let alert = NSAlert()
        alert.alertStyle = .warning
        if uniqueClips.count == 1, let clip = uniqueClips.first {
            alert.messageText = "Remove “\(clip.displayName)” from Cuelet?"
            alert.informativeText = "The audio file will stay on disk. This only removes it from the current library view."
        } else {
            alert.messageText = "Remove \(uniqueClips.count) sounds from Cuelet?"
            alert.informativeText = "The audio files will stay on disk. This only removes them from the current library view."
        }
        alert.addButton(withTitle: "Remove")
        alert.addButton(withTitle: "Cancel")

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        _ = removeLibraryEntries(uniqueClips)
    }

    @discardableResult
    func removeLibraryEntries(_ clipsToRemove: [SoundClip]) -> Bool {
        let uniqueClips = uniquedClips(clipsToRemove)
        guard !uniqueClips.isEmpty else { return false }
        let removedIDs = Set(uniqueClips.map(\.id))
        let remainingClips = clips.filter { !removedIDs.contains($0.id) }
        let previousIgnoredPaths = ignoredManagedPaths
        ignoredManagedPaths.formUnion(uniqueClips.compactMap { clip in
            clip.storageMode == .managed ? clip.managedRelativePath : nil
        })
        do {
            try persistLibraryMetadata(clips: remainingClips)
        } catch {
            ignoredManagedPaths = previousIgnoredPaths
            showError(error.localizedDescription)
            return false
        }

        uniqueClips.forEach { stop($0) }
        clips = remainingClips
        selectedSoundIDs.subtract(removedIDs)

        if let focusedSoundID, removedIDs.contains(focusedSoundID) {
            self.focusedSoundID = firstSelectedVisibleClipID() ?? selectedSoundIDs.first
        }

        if let selectionAnchorSoundID, removedIDs.contains(selectionAnchorSoundID) {
            self.selectionAnchorSoundID = firstSelectedVisibleClipID() ?? selectedSoundIDs.first
        }
        refreshGlobalShortcutRegistrations()
        return true
    }

    func deleteManagedFile(_ clip: SoundClip) {
        guard clip.storageMode == .managed, !clip.isMissing else { return }
        let alert = NSAlert()
        alert.alertStyle = .critical
        alert.messageText = "Permanently delete “\(clip.displayName)”?"
        alert.informativeText = "This deletes the managed audio file and removes it from Cuelet. This action cannot be undone."
        alert.addButton(withTitle: "Delete File")
        alert.addButton(withTitle: "Cancel")
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        _ = deleteManagedFileWithoutConfirmation(clip)
    }

    @discardableResult
    func deleteManagedFileWithoutConfirmation(_ clip: SoundClip) -> Bool {
        guard !settings.libraryPath.isEmpty else { return false }
        let libraryURL = URL(fileURLWithPath: expandedPath(settings.libraryPath), isDirectory: true)
        do {
            let staged = try libraryService.stageManagedDeletion(clip, libraryURL: libraryURL)
            let remaining = clips.filter { $0.id != clip.id }
            do {
                try persistLibraryMetadata(clips: remaining, libraryURL: libraryURL)
            } catch {
                libraryService.rollbackManagedDeletion(staged)
                throw error
            }
            do {
                try libraryService.commitManagedDeletion(staged)
            } catch {
                persistenceStatusMessage = "The library entry was removed, but Cuelet could not finish deleting its quarantined managed file: \(error.localizedDescription)"
                showError(persistenceStatusMessage)
                return false
            }
            stop(clip)
            clips = remaining
            selectedSoundIDs.remove(clip.id)
            refreshGlobalShortcutRegistrations()
            return true
        } catch {
            showError(error.localizedDescription)
            return false
        }
    }

    func locateOrRelink(_ clip: SoundClip) {
        let panel = NSOpenPanel()
        panel.title = clip.storageMode == .linked ? "Relink Sound" : "Locate Replacement for Managed Sound"
        panel.message = "Choose the audio file that should restore “\(clip.displayName)”."
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = [.audio]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try relink(clip, to: url)
        } catch {
            showError(error.localizedDescription)
        }
    }

    func relink(_ clip: SoundClip, to url: URL) throws {
        guard !settings.libraryPath.isEmpty,
              let index = clips.firstIndex(where: { $0.id == clip.id }) else {
            throw AppPersistenceError.metadata("Cuelet cannot relink this sound because no managed library is active.")
        }
        let libraryURL = URL(fileURLWithPath: expandedPath(settings.libraryPath), isDirectory: true)
        let previous = clips[index]
        let updated = try libraryService.relink(previous, to: url, libraryURL: libraryURL)
        clips[index] = updated
        do {
            try persistLibraryMetadata(clips: clips, libraryURL: libraryURL)
        } catch {
            clips[index] = previous
            throw error
        }
    }

    func playbackProgress(for clip: SoundClip) -> PlaybackService.Progress? {
        playbackService.progress(for: clip.id)
    }

    func color(for category: SoundCategory) -> Color {
        Color(hex: colorHex(for: category))
    }

    func categoryColorHex(for category: SoundCategory) -> String {
        colorHex(for: category)
    }

    func name(for category: SoundCategory) -> String {
        settings.categoryNames[category.id] ?? categoryByID(category.id)?.name ?? category.name
    }

    func systemImage(for category: SoundCategory) -> String {
        categoryByID(category.id)?.systemImage ?? category.systemImage
    }

    func iconID(for category: SoundCategory) -> String {
        categoryByID(category.id)?.iconID ?? category.iconID
    }

    func canEditCategory(_ category: SoundCategory) -> Bool {
        category.isUserEditable && settings.customCategories.contains { $0.id == category.id }
    }

    func changeColor(for category: SoundCategory, to hex: String) {
        let previousSettings = settings
        let previousClips = clips
        guard canEditCategory(category),
              SoundCategory.palette.contains(where: { $0.hex == hex }),
              let index = settings.customCategories.firstIndex(where: { $0.id == category.id }) else {
            return
        }

        var updatedSettings = settings
        var updatedCategory = updatedSettings.customCategories[index]
        updatedCategory.defaultColorHex = hex
        updatedSettings.customCategories[index] = updatedCategory
        updatedSettings.categoryColorHexes[category.id] = hex
        settings = updatedSettings
        refreshCategoryCopies(updatedCategory)
        if !persistCurrentLibraryMetadata() {
            settings = previousSettings
            clips = previousClips
        }
    }

    func changeIcon(for category: SoundCategory, to iconID: String) {
        let previousSettings = settings
        let previousClips = clips
        let canonicalID = SoundCategory.canonicalIconID(iconID)
        guard canEditCategory(category),
              SoundCategory.iconChoices.contains(where: { $0.id == canonicalID }),
              let index = settings.customCategories.firstIndex(where: { $0.id == category.id }) else {
            return
        }

        var updatedSettings = settings
        var updatedCategory = updatedSettings.customCategories[index]
        updatedCategory.iconID = canonicalID
        updatedSettings.customCategories[index] = updatedCategory
        settings = updatedSettings
        refreshCategoryCopies(updatedCategory)
        if !persistCurrentLibraryMetadata() {
            settings = previousSettings
            clips = previousClips
        }
    }

    func renameCategory(_ category: SoundCategory) {
        guard canEditCategory(category) else {
            showError("This category cannot be renamed.")
            return
        }

        categoryEditorRequest = CategoryEditorRequest(categoryID: category.id, clipIDsToAssign: [])
    }

    @discardableResult
    func renameCategory(_ category: SoundCategory, to name: String) -> SoundCategory {
        let previousSettings = settings
        let previousClips = clips
        let trimmedName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard canEditCategory(category), !trimmedName.isEmpty else { return category }
        guard categoryNameValidationError(for: trimmedName, excluding: category.id) == nil else {
            return categoryByID(category.id) ?? category
        }
        guard let index = settings.customCategories.firstIndex(where: { $0.id == category.id }) else { return category }

        var updatedSettings = settings
        var renamedCategory = updatedSettings.customCategories[index]
        renamedCategory.name = trimmedName
        updatedSettings.customCategories[index] = renamedCategory
        updatedSettings.categoryNames[category.id] = trimmedName
        settings = updatedSettings

        clips = clips.map { clip in
            guard clip.category.id == category.id else { return clip }
            var updatedClip = clip
            updatedClip.category = renamedCategory
            return updatedClip
        }

        if case .category(let selectedCategory) = selectedSidebarItem, selectedCategory.id == category.id {
            selectedSidebarItem = .category(renamedCategory)
        }

        if !persistCurrentLibraryMetadata() {
            settings = previousSettings
            clips = previousClips
            return categoryByID(category.id) ?? category
        }

        return renamedCategory
    }

    func deleteCategory(_ category: SoundCategory) {
        guard canEditCategory(category) else { return }
        let previousSettings = settings
        let previousClips = clips

        var updatedSettings = settings
        updatedSettings.customCategories.removeAll { $0.id == category.id }
        updatedSettings.categoryColorHexes[category.id] = nil
        updatedSettings.categoryNames[category.id] = nil
        updatedSettings.soundCategoryAssignments = updatedSettings.soundCategoryAssignments.filter { $0.value != category.id }
        settings = updatedSettings

        clips = clips.map { clip in
            guard clip.category.id == category.id else { return clip }
            var updatedClip = clip
            updatedClip.category = .uncategorized
            return updatedClip
        }

        if case .category(let selectedCategory) = selectedSidebarItem, selectedCategory.id == category.id {
            selectedSidebarItem = .allCategories
        }
        if !persistCurrentLibraryMetadata() {
            settings = previousSettings
            clips = previousClips
        }
    }

    func newCategory() {
        categoryEditorRequest = CategoryEditorRequest(categoryID: nil, clipIDsToAssign: [])
    }

    @discardableResult
    func createCategory(named name: String, colorHex: String, iconID: String = "tag") -> SoundCategory {
        let trimmedName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedName.isEmpty else { return .uncategorized }
        if let existingCategory = category(named: trimmedName) {
            return existingCategory
        }

        let previousSettings = settings
        var category = SoundCategory.makeUserCategory(named: trimmedName, colorHex: colorHex)
        category.iconID = SoundCategory.canonicalIconID(iconID)
        while categories.contains(where: { $0.id == category.id }) {
            category = SoundCategory(
                id: "\(category.id)-\(Int.random(in: 100...999))",
                name: category.name,
                defaultColorHex: category.defaultColorHex,
                systemImage: category.systemImage,
                isUserEditable: true
            )
        }

        var updatedSettings = settings
        updatedSettings.customCategories.append(category)
        updatedSettings.categoryColorHexes[category.id] = colorHex
        updatedSettings.categoryNames[category.id] = trimmedName
        settings = updatedSettings
        if !persistCurrentLibraryMetadata() {
            settings = previousSettings
            return .uncategorized
        }
        return category
    }

    func createCategoryAndAssign(to clip: SoundClip) {
        createCategoryAndAssign(to: [clip])
    }

    func createCategoryAndAssign(to clipsToAssign: [SoundClip]) {
        categoryEditorRequest = CategoryEditorRequest(
            categoryID: nil,
            clipIDsToAssign: clipsToAssign.map(\.id)
        )
    }

    func categoryForEditing(_ request: CategoryEditorRequest) -> SoundCategory? {
        request.categoryID.flatMap(categoryByID)
    }

    func saveCategoryEditor(
        request: CategoryEditorRequest,
        name: String,
        colorHex: String,
        iconID: String
    ) -> String? {
        let trimmedName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        if let error = categoryNameValidationError(for: trimmedName, excluding: request.categoryID) {
            return error
        }

        let category: SoundCategory
        if let existing = categoryForEditing(request) {
            category = renameCategory(existing, to: trimmedName)
            changeColor(for: category, to: colorHex)
            changeIcon(for: category, to: iconID)
        } else {
            category = createCategory(named: trimmedName, colorHex: colorHex, iconID: iconID)
        }

        let targetClips = clips.filter { request.clipIDsToAssign.contains($0.id) }
        if !targetClips.isEmpty {
            assign(targetClips, to: categoryByID(category.id) ?? category)
        }
        categoryEditorRequest = nil
        return nil
    }

    func dismissCategoryEditor() {
        categoryEditorRequest = nil
    }

    func confirmDeleteCategory(_ category: SoundCategory) {
        guard canEditCategory(category) else {
            showError("This category cannot be deleted.")
            return
        }

        let affectedSoundCount = clips.filter { $0.category.id == category.id }.count
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "Delete “\(name(for: category))”?"
        alert.informativeText = affectedSoundCount == 0
            ? "This category will be removed."
            : "\(affectedSoundCount) sounds will move to Uncategorized."
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        deleteCategory(category)
    }

    func refreshAudioRouting() {
        outputDevices = [AudioDevice.systemOutput] + audioDeviceService.outputDeviceSnapshots().map(\.device)
        inputDevices = audioDeviceService.inputDevices()
        microphonePermissionState = audioPermissionService.microphonePermissionState()

        if let inputDeviceID = settings.inputDeviceID,
           !inputDevices.contains(where: { $0.id == inputDeviceID }) {
            settings.inputDeviceID = nil
        }

        switch microphonePermissionState {
        case .denied:
            audioStatusMessage = "Microphone permission denied. Enable access in System Settings to use input metering."
        case .missingUsageDescription:
            audioStatusMessage = "Microphone access is unavailable because this app bundle does not include a microphone usage description."
        default:
            audioStatusMessage = ""
        }

        reconcileConfiguredOutput()
        refreshVirtualAudioDriverStatus()
    }

    @discardableResult
    func selectOutputDevice(id: String) -> Bool {
        defer { refreshVirtualAudioDriverStatus() }
        guard id != settings.outputDeviceID else {
            reconcileConfiguredOutput()
            return audioRouteStatus.kind != .failed
        }

        let targetDevice: AudioDevice
        let targetUID: String?
        if id == AudioDevice.systemOutput.id {
            targetDevice = .systemOutput
            targetUID = nil
        } else if let snapshot = audioDeviceService.outputDevice(forPersistentID: id),
                  let uid = snapshot.device.coreAudioUID {
            targetDevice = snapshot.device
            targetUID = uid
        } else {
            announceAudioRouteStatus("The selected output device is unavailable.")
            return false
        }

        audioRouteStatus = AudioRouteStatus(
            kind: .applying,
            selectedDeviceID: targetDevice.id,
            selectedName: targetDevice.name,
            activeDeviceID: nil,
            activeName: nil,
            message: "Applying \(targetDevice.name)…",
            technicalDetails: nil
        )

        let routeWasAlreadyConfigured = playbackService.configuredOutputDeviceUID == targetUID
        let applyResult: Result<Void, PlaybackService.OutputRouteError> = routeWasAlreadyConfigured
            ? .success(())
            : playbackService.applyOutputDeviceUID(targetUID)
        switch applyResult {
        case .success:
            var updatedSettings = settings
            updatedSettings.outputDeviceID = targetDevice.id
            updatedSettings.outputDeviceName = targetDevice.name
            settings = updatedSettings
            if playbackState.isPlaying, routeWasAlreadyConfigured {
                handleOutputRouteConfirmation(requestedUID: targetUID, actualUID: targetUID)
            } else if !playbackState.isPlaying {
                audioRouteStatus = readyRouteStatus(for: targetDevice)
            }
            return true
        case .failure(let error):
            audioRouteStatus = AudioRouteStatus(
                kind: .failed,
                selectedDeviceID: settings.outputDeviceID,
                selectedName: settings.outputDeviceName,
                activeDeviceID: nil,
                activeName: nil,
                message: "Routing failed. The previous selection was kept.",
                technicalDetails: error.localizedDescription
            )
            announceAudioRouteStatus(audioRouteStatus.message)
            return false
        }
    }

    func setOutputFallbackPolicy(_ policy: AudioOutputFallbackPolicy) {
        guard settings.outputFallbackPolicy != policy else { return }
        settings.outputFallbackPolicy = policy
        reconcileConfiguredOutput()
        refreshVirtualAudioDriverStatus()
    }

    func refreshVirtualAudioDriverStatus() {
        virtualAudioDriverStatus = virtualAudioDriverService.status(
            selectedOutputDeviceID: settings.outputDeviceID,
            routingStatus: audioRouteStatus
        )
    }

    private func reconcileConfiguredOutput() {
        if settings.outputDeviceID == AudioDevice.systemOutput.id {
            applyAvailableRoute(device: .systemOutput, uid: nil)
            return
        }

        guard let snapshot = audioDeviceService.outputDevice(forPersistentID: settings.outputDeviceID),
              let uid = snapshot.device.coreAudioUID else {
            handleUnavailableSelectedOutput()
            return
        }

        if settings.outputDeviceName != snapshot.device.name {
            settings.outputDeviceName = snapshot.device.name
        }
        applyAvailableRoute(device: snapshot.device, uid: uid)
    }

    private func applyAvailableRoute(device: AudioDevice, uid: String?) {
        let wasUnavailable = audioRouteStatus.kind == .unavailable
            || audioRouteStatus.kind == .fallbackSystemOutput
            || audioRouteStatus.kind == .reconnecting
        if wasUnavailable {
            audioRouteStatus = AudioRouteStatus(
                kind: .reconnecting,
                selectedDeviceID: device.id,
                selectedName: device.name,
                activeDeviceID: nil,
                activeName: nil,
                message: "Reconnecting to \(device.name)…",
                technicalDetails: nil
            )
        }

        // Core Audio device notifications and opening Settings can both refresh
        // this catalog. Reassigning AVAudioPlayer.currentDevice to the UID it is
        // already using can interrupt some physical and virtual drivers, so an
        // unchanged route is intentionally a no-op.
        if playbackService.configuredOutputDeviceUID == uid,
           audioRouteStatus.kind != .failed {
            if playbackState.isPlaying {
                handleOutputRouteConfirmation(requestedUID: uid, actualUID: uid)
            } else {
                audioRouteStatus = readyRouteStatus(for: device)
            }
            return
        }

        switch playbackService.applyOutputDeviceUID(uid) {
        case .success:
            if !playbackState.isPlaying {
                audioRouteStatus = readyRouteStatus(for: device)
            }
        case .failure(let error):
            stopAllPlayback()
            audioRouteStatus = AudioRouteStatus(
                kind: .failed,
                selectedDeviceID: device.id,
                selectedName: device.name,
                activeDeviceID: nil,
                activeName: nil,
                message: "Cuelet could not route playback to \(device.name).",
                technicalDetails: error.localizedDescription
            )
            announceAudioRouteStatus(audioRouteStatus.message)
        }
    }

    private func handleUnavailableSelectedOutput() {
        let wasAlreadyUnavailable = audioRouteStatus.kind == .unavailable
            || audioRouteStatus.kind == .fallbackSystemOutput
        let isLeavingActiveFallback = audioRouteStatus.kind == .fallbackSystemOutput
            && audioRouteStatus.activeDeviceID != nil
            && settings.outputFallbackPolicy == .stopAndWait
        if !wasAlreadyUnavailable || isLeavingActiveFallback {
            stopAllPlayback()
        }
        let savedID = settings.outputDeviceID
        let savedName = settings.outputDeviceName.isEmpty ? "Selected Output" : settings.outputDeviceName

        switch settings.outputFallbackPolicy {
        case .stopAndWait:
            let missingUID = AudioDevice(
                id: savedID,
                name: savedName,
                kind: .output,
                isDefault: false,
                isVirtual: false
            ).coreAudioUID
            if playbackService.configuredOutputDeviceUID != missingUID {
                _ = playbackService.applyOutputDeviceUID(missingUID)
            }
            audioRouteStatus = AudioRouteStatus(
                kind: .unavailable,
                selectedDeviceID: savedID,
                selectedName: savedName,
                activeDeviceID: nil,
                activeName: nil,
                message: "\(savedName) is unavailable. Playback is stopped until that exact device returns.",
                technicalDetails: "Saved Core Audio device UID could not be resolved."
            )
        case .systemOutput:
            let applyResult: Result<Void, PlaybackService.OutputRouteError> = playbackService.configuredOutputDeviceUID == nil
                ? .success(())
                : playbackService.applyOutputDeviceUID(nil)
            switch applyResult {
            case .success:
                if playbackState.isPlaying {
                    handleOutputRouteConfirmation(requestedUID: nil, actualUID: nil)
                } else {
                    audioRouteStatus = AudioRouteStatus(
                        kind: .fallbackSystemOutput,
                        selectedDeviceID: savedID,
                        selectedName: savedName,
                        activeDeviceID: nil,
                        activeName: nil,
                        message: "\(savedName) is unavailable. Future playback will temporarily use System Output.",
                        technicalDetails: nil
                    )
                }
            case .failure(let error):
                audioRouteStatus = AudioRouteStatus(
                    kind: .failed,
                    selectedDeviceID: savedID,
                    selectedName: savedName,
                    activeDeviceID: nil,
                    activeName: nil,
                    message: "The selected device is unavailable and System Output fallback could not be prepared.",
                    technicalDetails: error.localizedDescription
                )
            }
        }
        if !wasAlreadyUnavailable {
            announceAudioRouteStatus(audioRouteStatus.message)
        }
    }

    private func readyRouteStatus(for device: AudioDevice) -> AudioRouteStatus {
        let message = device.id == AudioDevice.systemOutput.id
            ? "Ready. Playback will follow the current macOS System Output."
            : "Ready to route playback to \(device.name)."
        return AudioRouteStatus(
            kind: .ready,
            selectedDeviceID: device.id,
            selectedName: device.name,
            activeDeviceID: nil,
            activeName: nil,
            message: message,
            technicalDetails: nil
        )
    }

    private func handleOutputRouteConfirmation(requestedUID: String?, actualUID: String?) {
        defer { refreshVirtualAudioDriverStatus() }
        let selectedID = settings.outputDeviceID
        let isFallback = selectedID != AudioDevice.systemOutput.id
            && settings.outputFallbackPolicy == .systemOutput
            && audioDeviceService.outputDevice(forPersistentID: selectedID) == nil

        guard playbackState.isPlaying else {
            updateIdleAudioRouteStatusIfNeeded()
            return
        }

        if isFallback {
            guard requestedUID == nil else { return }
            let activeName = audioDeviceService.systemOutputDevice()?.device.name ?? "System Output"
            audioRouteStatus = AudioRouteStatus(
                kind: .fallbackSystemOutput,
                selectedDeviceID: selectedID,
                selectedName: settings.outputDeviceName,
                activeDeviceID: AudioDevice.systemOutput.id,
                activeName: activeName,
                message: "Playing through System Output temporarily; waiting for \(settings.outputDeviceName).",
                technicalDetails: nil
            )
            return
        }

        if selectedID == AudioDevice.systemOutput.id {
            guard requestedUID == nil else { return }
            let activeName = audioDeviceService.systemOutputDevice()?.device.name ?? "System Output"
            audioRouteStatus = AudioRouteStatus(
                kind: .systemOutput,
                selectedDeviceID: selectedID,
                selectedName: AudioDevice.systemOutput.name,
                activeDeviceID: AudioDevice.systemOutput.id,
                activeName: activeName,
                message: "Playing through \(activeName) via System Output.",
                technicalDetails: nil
            )
            return
        }

        guard let expectedUID = AudioDevice(
            id: selectedID,
            name: "",
            kind: .output,
            isDefault: false,
            isVirtual: false
        ).coreAudioUID,
              requestedUID == expectedUID else { return }

        guard actualUID == expectedUID else {
            stopAllPlayback()
            audioRouteStatus = AudioRouteStatus(
                kind: .failed,
                selectedDeviceID: selectedID,
                selectedName: settings.outputDeviceName,
                activeDeviceID: nil,
                activeName: nil,
                message: "Core Audio did not confirm the selected output. Playback was stopped.",
                technicalDetails: "Requested UID and active player UID did not match."
            )
            announceAudioRouteStatus(audioRouteStatus.message)
            return
        }

        audioRouteStatus = AudioRouteStatus(
            kind: .explicitDevice,
            selectedDeviceID: selectedID,
            selectedName: settings.outputDeviceName,
            activeDeviceID: selectedID,
            activeName: settings.outputDeviceName,
            message: "Playing through \(settings.outputDeviceName).",
            technicalDetails: nil
        )
    }

    private func announceAudioRouteStatus(_ message: String) {
        NSAccessibility.post(
            element: NSApp as Any,
            notification: .announcementRequested,
            userInfo: [.announcement: message]
        )
    }

    private func updateIdleAudioRouteStatusIfNeeded() {
        guard !playbackState.isPlaying else { return }
        if settings.outputDeviceID == AudioDevice.systemOutput.id {
            audioRouteStatus = readyRouteStatus(for: .systemOutput)
        } else if let device = audioDeviceService.outputDevice(forPersistentID: settings.outputDeviceID)?.device {
            audioRouteStatus = readyRouteStatus(for: device)
        } else if settings.outputFallbackPolicy == .systemOutput {
            audioRouteStatus = AudioRouteStatus(
                kind: .fallbackSystemOutput,
                selectedDeviceID: settings.outputDeviceID,
                selectedName: settings.outputDeviceName,
                activeDeviceID: nil,
                activeName: nil,
                message: "\(settings.outputDeviceName) is unavailable. Future playback will temporarily use System Output.",
                technicalDetails: nil
            )
        }
    }

    func requestMicrophonePermission() async {
        microphonePermissionState = await audioPermissionService.requestMicrophonePermission()
        if microphonePermissionState == .missingUsageDescription {
            audioStatusMessage = "This Swift Package executable needs an NSMicrophoneUsageDescription in its app bundle before macOS can show the permission prompt."
        }
    }

    func setInputMonitoringEnabled(_ isEnabled: Bool) {
        settings.isInputMonitoringEnabled = isEnabled

        guard isEnabled else {
            microphoneService.stopMonitoring { [weak self] state in
                self?.inputLevelState = state
            }
            return
        }

        guard microphonePermissionState == .authorized else {
            settings.isInputMonitoringEnabled = false
            audioStatusMessage = "Allow microphone access before starting input monitoring."
            return
        }

        do {
            try microphoneService.startMonitoring { [weak self] state in
                self?.inputLevelState = state
            }
            audioStatusMessage = "Input monitoring is local only. Cuelet is not routing audio into other apps."
        } catch {
            settings.isInputMonitoringEnabled = false
            inputLevelState = .inactive
            audioStatusMessage = "Could not start input monitoring: \(error.localizedDescription)"
        }
    }

    private func markPlayed(_ clip: SoundClip) {
        guard let index = clips.firstIndex(where: { $0.id == clip.id }) else { return }
        let previousDate = clips[index].lastPlayedAt
        clips[index].lastPlayedAt = Date()
        do {
            try persistLibraryMetadata(clips: clips)
        } catch {
            clips[index].lastPlayedAt = previousDate
        }
    }

    private func handlePlaybackFinished(_ clipID: SoundClip.ID) {
        playbackState.stop(clipID)
        updateIdleAudioRouteStatusIfNeeded()
    }

    private func playClipFromGlobalShortcut(_ clipID: SoundClip.ID) {
        guard let clip = clips.first(where: {
            $0.id == clipID && $0.shortcut?.scope == .global && $0.shortcut?.isEnabled == true
        }) else { return }
        _ = startPlayback(clip)
    }

    func refreshGlobalShortcutRegistrations() {
        _ = globalShortcutService.tryUpdate(globalAssignments(from: clips))
        updateGlobalShortcutStatus()
    }

    private func globalAssignments(from clips: [SoundClip]) -> [GlobalShortcutAssignment] {
        clips.compactMap { clip in
            guard let shortcut = clip.shortcut,
                  shortcut.scope == .global,
                  shortcut.isEnabled else { return nil }
            return GlobalShortcutAssignment(clipID: clip.id, shortcut: shortcut)
        }
    }

    private func updateGlobalShortcutStatus() {
        if let message = globalShortcutService.lastErrorMessage {
            globalShortcutStatusMessage = message
            return
        }
        let count = globalShortcutService.registeredClipIDs.count
        globalShortcutStatusMessage = count == 0
            ? "No global shortcuts assigned"
            : count == 1 ? "1 global shortcut registered" : "\(count) global shortcuts registered"
    }

    @discardableResult
    private func startPlayback(_ clip: SoundClip) -> Bool {
        defer { refreshVirtualAudioDriverStatus() }
        guard audioRouteStatus.allowsPlayback else {
            showError(audioRouteStatus.message)
            return false
        }
        let result = playbackService.play(clip: clip, settings: settings, playbackState: &playbackState)
        guard result.didStart else {
            if let error = result.error {
                if case .outputRoutingFailed(let details) = error {
                    audioRouteStatus = AudioRouteStatus(
                        kind: .failed,
                        selectedDeviceID: settings.outputDeviceID,
                        selectedName: settings.outputDeviceName,
                        activeDeviceID: nil,
                        activeName: nil,
                        message: "Cuelet could not start playback on the selected output.",
                        technicalDetails: details
                    )
                    announceAudioRouteStatus(audioRouteStatus.message)
                }
                showError(error.localizedDescription)
            }
            return false
        }

        markPlayed(clip)
        return true
    }

    private func loadInitialLibrary() {
        if !settings.libraryPath.isEmpty {
            loadLibrary(
                at: URL(fileURLWithPath: expandedPath(settings.libraryPath)),
                presentsErrors: false
            )
        }
    }

    private func synchronizeSelectionWithVisibleClips() {
        let visibleIDs = Set(visibleClips.map(\.id))
        let synchronizedSelection = selectedSoundIDs.intersection(visibleIDs)
        if synchronizedSelection != selectedSoundIDs {
            selectedSoundIDs = synchronizedSelection
        }

        if let focusedSoundID, !visibleIDs.contains(focusedSoundID) {
            let updated = firstSelectedVisibleClipID()
            if self.focusedSoundID != updated { self.focusedSoundID = updated }
        }

        if let selectionAnchorSoundID, !visibleIDs.contains(selectionAnchorSoundID) {
            let updated = firstSelectedVisibleClipID()
            if self.selectionAnchorSoundID != updated { self.selectionAnchorSoundID = updated }
        }

        if selectedSoundIDs.isEmpty {
            if focusedSoundID != nil { focusedSoundID = nil }
            if selectionAnchorSoundID != nil { selectionAnchorSoundID = nil }
        }
    }

    private func expandedPath(_ path: String) -> String {
        NSString(string: path).expandingTildeInPath
    }

    private func ensureManagedLibrary() throws -> URL {
        if !settings.libraryPath.isEmpty {
            return URL(fileURLWithPath: expandedPath(settings.libraryPath), isDirectory: true)
        }

        let parent = settingsStore.url.deletingLastPathComponent()
        let libraryURL: URL
        if settingsStore.url.lastPathComponent == "settings.json" {
            libraryURL = parent.appendingPathComponent("Library", isDirectory: true)
        } else {
            libraryURL = parent.appendingPathComponent(
                "\(settingsStore.url.deletingPathExtension().lastPathComponent)-Library",
                isDirectory: true
            )
        }
        try FileManager.default.createDirectory(
            at: libraryURL,
            withIntermediateDirectories: true,
            attributes: [.posixPermissions: 0o700]
        )
        loadLibrary(at: libraryURL)
        guard settings.libraryPath == libraryURL.path else {
            throw AppPersistenceError.metadata("Cuelet could not initialize its managed library.")
        }
        return libraryURL
    }

    private func persistLibraryMetadata(clips clipsToPersist: [SoundClip], libraryURL: URL? = nil) throws {
        let resolvedLibraryURL: URL
        if let libraryURL {
            resolvedLibraryURL = libraryURL
        } else {
            guard !settings.libraryPath.isEmpty else { return }
            resolvedLibraryURL = URL(fileURLWithPath: expandedPath(settings.libraryPath), isDirectory: true)
        }

        let store = LibraryMetadataStore(libraryURL: resolvedLibraryURL)
        let document = store.document(
            from: clipsToPersist,
            categories: settings.customCategories,
            ignoredManagedPaths: ignoredManagedPaths
        )
        do {
            try store.save(document, loadedLegacyVersion: loadedMetadataVersion)
            loadedMetadataVersion = nil
        } catch {
            persistenceStatusMessage = error.localizedDescription
            throw error
        }
    }

    private func persistCurrentLibraryMetadata() -> Bool {
        do {
            try persistLibraryMetadata(clips: clips)
            return true
        } catch {
            showError(error.localizedDescription)
            return false
        }
    }

    private func applyLibraryCategories(_ categories: [SoundCategory]) {
        var updatedSettings = settings
        let retainedIDs = Set(categories.map(\.id)).union([SoundCategory.uncategorized.id])
        updatedSettings.customCategories = categories
        updatedSettings.categoryColorHexes = updatedSettings.categoryColorHexes.filter { retainedIDs.contains($0.key) }
        updatedSettings.categoryNames = updatedSettings.categoryNames.filter { retainedIDs.contains($0.key) }
        for category in categories {
            updatedSettings.categoryColorHexes[category.id] = category.defaultColorHex
            updatedSettings.categoryNames[category.id] = category.name
        }
        settings = updatedSettings
    }

    private func showError(_ message: String) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "Cuelet"
        alert.informativeText = message
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }

    private func sortedClips(_ clips: [SoundClip]) -> [SoundClip] {
        clips.sorted { lhs, rhs in
            switch sortOption {
            case .nameAscending:
                return compareByName(lhs, rhs, ascending: true)
            case .nameDescending:
                return compareByName(lhs, rhs, ascending: false)
            case .latestAdded:
                if lhs.addedAt != rhs.addedAt {
                    return lhs.addedAt > rhs.addedAt
                }
                return compareByName(lhs, rhs, ascending: true)
            case .oldestAdded:
                if lhs.addedAt != rhs.addedAt {
                    return lhs.addedAt < rhs.addedAt
                }
                return compareByName(lhs, rhs, ascending: true)
            case .durationShortest:
                if lhs.duration != rhs.duration {
                    return lhs.duration < rhs.duration
                }
                return compareByName(lhs, rhs, ascending: true)
            case .durationLongest:
                if lhs.duration != rhs.duration {
                    return lhs.duration > rhs.duration
                }
                return compareByName(lhs, rhs, ascending: true)
            case .category:
                let categoryOrder = name(for: lhs.category).localizedStandardCompare(name(for: rhs.category))
                if categoryOrder != .orderedSame {
                    return categoryOrder == .orderedAscending
                }
                return compareByName(lhs, rhs, ascending: true)
            }
        }
    }

    private func compareByName(_ lhs: SoundClip, _ rhs: SoundClip, ascending: Bool) -> Bool {
        let nameOrder = lhs.displayName.localizedStandardCompare(rhs.displayName)
        if nameOrder != .orderedSame {
            return ascending ? nameOrder == .orderedAscending : nameOrder == .orderedDescending
        }

        let filenameOrder = lhs.filename.localizedStandardCompare(rhs.filename)
        if filenameOrder != .orderedSame {
            return filenameOrder == .orderedAscending
        }

        return lhs.id.uuidString < rhs.id.uuidString
    }

    private func colorHex(for category: SoundCategory) -> String {
        settings.categoryColorHexes[category.id] ?? categoryByID(category.id)?.defaultColorHex ?? category.defaultColorHex
    }

    private func categoryByID(_ id: String) -> SoundCategory? {
        categories.first { $0.id == id }
    }

    private func applyStoredClipMetadata(to clips: [SoundClip]) -> [SoundClip] {
        clips.map { clip in
            var updatedClip = clip

            if let key = assignmentKey(for: clip) {
                if let categoryID = settings.soundCategoryAssignments[key],
                   let category = categoryByID(categoryID) {
                    updatedClip.category = category
                }
                updatedClip.isFavorite = settings.favoriteSoundIDs.contains(key)
                updatedClip.shortcut = settings.soundShortcutAssignments[key]
            }

            return updatedClip
        }
    }

    private func refreshCategoryCopies(_ category: SoundCategory) {
        clips = clips.map { clip in
            guard clip.category.id == category.id else { return clip }
            var updatedClip = clip
            updatedClip.category = category
            return updatedClip
        }

        if case .category(let selectedCategory) = selectedSidebarItem, selectedCategory.id == category.id {
            selectedSidebarItem = .category(category)
        }
    }

    private func categoryNameValidationError(for name: String, excluding excludedID: String? = nil) -> String? {
        let normalizedName = normalizedCategoryName(name)
        guard !normalizedName.isEmpty else { return "Category names cannot be empty." }

        let hasDuplicate = categories.contains { category in
            category.id != excludedID && normalizedCategoryName(self.name(for: category)) == normalizedName
        }
        return hasDuplicate ? "A category named “\(name)” already exists." : nil
    }

    private func category(named name: String) -> SoundCategory? {
        let normalizedName = normalizedCategoryName(name)
        return categories.first { category in
            normalizedCategoryName(self.name(for: category)) == normalizedName
        }
    }

    private func normalizedCategoryName(_ name: String) -> String {
        name
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
            .lowercased()
    }

    private func assignmentKey(for clip: SoundClip) -> String? {
        clip.fileURL?.standardizedFileURL.path
    }

    private func focusedVisibleIndex(in clips: [SoundClip]) -> Int? {
        if let focusedSoundID,
           let index = clips.firstIndex(where: { $0.id == focusedSoundID }) {
            return index
        }

        if let selectedID = firstSelectedVisibleClipID(in: clips) {
            return clips.firstIndex { $0.id == selectedID }
        }

        return nil
    }

    private func validSelectionAnchor(in clips: [SoundClip]) -> SoundClip.ID? {
        if let selectionAnchorSoundID,
           clips.contains(where: { $0.id == selectionAnchorSoundID }) {
            return selectionAnchorSoundID
        }

        if let focusedSoundID,
           clips.contains(where: { $0.id == focusedSoundID }) {
            return focusedSoundID
        }

        return firstSelectedVisibleClipID(in: clips)
    }

    private func firstSelectedVisibleClipID(in clips: [SoundClip]? = nil) -> SoundClip.ID? {
        let visible = clips ?? visibleClips
        return visible.first { selectedSoundIDs.contains($0.id) }?.id
    }

    private func uniquedClips(_ clips: [SoundClip]) -> [SoundClip] {
        var seenIDs: Set<SoundClip.ID> = []
        return clips.filter { clip in
            guard !seenIDs.contains(clip.id) else { return false }
            seenIDs.insert(clip.id)
            return true
        }
    }

    private func uniquedCategories(_ categories: [SoundCategory]) -> [SoundCategory] {
        var seenIDs: Set<String> = []
        return categories.filter { category in
            guard !seenIDs.contains(category.id) else { return false }
            seenIDs.insert(category.id)
            return true
        }
    }
}
