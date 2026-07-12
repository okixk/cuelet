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
        didSet { synchronizeSelectionWithVisibleClips() }
    }
    @Published var searchText = "" {
        didSet { synchronizeSelectionWithVisibleClips() }
    }
    @Published var selectedSoundIDs: Set<SoundClip.ID> = []
    @Published var focusedSoundID: SoundClip.ID?
    @Published var selectionAnchorSoundID: SoundClip.ID?
    @Published var clips: [SoundClip] = [] {
        didSet { synchronizeSelectionWithVisibleClips() }
    }
    @Published var playbackState = PlaybackState()
    @Published var settings = CueletSettings() {
        didSet {
            settingsStore.save(settings)
            playbackService.setVolume(settings.soundboardVolume)
        }
    }
    @Published var viewMode = ViewMode.grid {
        didSet {
            settings.viewMode = viewMode
        }
    }
    @Published var sortOption = SoundSortOption.nameAscending {
        didSet {
            settings.sortOption = sortOption
            synchronizeSelectionWithVisibleClips()
        }
    }
    @Published var showsMockLibrary = false
    @Published var outputDevices: [AudioDevice] = [.systemOutput]
    @Published var inputDevices: [AudioDevice] = []
    @Published var microphonePermissionState: MicrophonePermissionState = .unknown
    @Published var inputLevelState: InputLevelState = .inactive
    @Published var audioStatusMessage = ""
    @Published var searchFocusRequestID = UUID()
    @Published var shortcutCaptureRequest: ShortcutCaptureRequest?
    @Published var categoryEditorRequest: CategoryEditorRequest?
    @Published private(set) var globalShortcutStatusMessage = "No global shortcuts assigned"
    private weak var mainWindow: NSWindow?
    private var didApplyStartupVisibility = false

    let libraryService: LibraryService
    let playbackService: PlaybackService
    let settingsStore: SettingsStore
    let searchService = SearchService()
    let profileService = ProfileService()
    let audioDeviceService = AudioDeviceService()
    let audioPermissionService = AudioPermissionService()
    let microphoneService = MicrophoneService()
    let launchAtLoginService = LaunchAtLoginService()
    private let globalShortcutService: GlobalShortcutRegistering
    private let localKeyboardShortcutService: LocalKeyboardShortcutService?

    init(
        settingsStore: SettingsStore = SettingsStore(),
        libraryService: LibraryService = LibraryService(),
        playbackService: PlaybackService = PlaybackService(),
        globalShortcutService: GlobalShortcutRegistering = CarbonGlobalShortcutService(),
        installKeyboardShortcuts: Bool = true,
        launchArguments: [String] = CommandLine.arguments
    ) {
        self.settingsStore = settingsStore
        self.libraryService = libraryService
        self.playbackService = playbackService
        self.globalShortcutService = globalShortcutService
        self.localKeyboardShortcutService = installKeyboardShortcuts ? LocalKeyboardShortcutService() : nil

        settings = settingsStore.load()
        viewMode = settings.viewMode
        sortOption = settings.sortOption
        playbackService.playbackDidFinish = { [weak self] clipID in
            self?.handlePlaybackFinished(clipID)
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

        loadInitialLibrary(usesDemoArgument: launchArguments.contains("--demo"))
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
        var resolvedCategories = [SoundCategory.uncategorized] + settings.customCategories
        if showsMockLibrary {
            resolvedCategories.append(contentsOf: clips.map(\.category))
        }
        return uniquedCategories(resolvedCategories)
    }

    var assignableCategories: [SoundCategory] {
        var resolvedCategories = [SoundCategory.uncategorized] + settings.customCategories
        if showsMockLibrary {
            resolvedCategories.append(contentsOf: SoundCategory.demoCategories)
        }
        return uniquedCategories(resolvedCategories)
    }

    var librarySubtitle: String {
        if showsMockLibrary { return "Demo Library" }
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
        clips = applyStoredClipMetadata(to: libraryService.importFiles(panel.urls, into: clips))
        if !clips.isEmpty {
            showsMockLibrary = false
            settings.showsDemoLibrary = false
        }
    }

    func rescanLibrary() {
        guard !showsMockLibrary else {
            clips = libraryService.rescanMockLibrary(currentClips: clips)
            return
        }

        guard !settings.libraryPath.isEmpty else { return }
        loadLibrary(at: URL(fileURLWithPath: expandedPath(settings.libraryPath)))
    }

    func loadDemoLibrary(persistChoice: Bool = true) {
        clips = libraryService.loadMockLibrary()
        showsMockLibrary = true
        if persistChoice {
            settings.showsDemoLibrary = true
        }
        selectedSidebarItem = .library
        refreshGlobalShortcutRegistrations()
    }

    func hideDemoLibrary() {
        settings.showsDemoLibrary = false
        showsMockLibrary = false

        if settings.libraryPath.isEmpty {
            clips = []
            refreshGlobalShortcutRegistrations()
        } else {
            loadLibrary(at: URL(fileURLWithPath: expandedPath(settings.libraryPath)))
        }
    }

    func loadLibrary(at folderURL: URL) {
        do {
            let scannedClips = applyStoredClipMetadata(
                to: try libraryService.scanLibrary(at: folderURL, scansSubfolders: settings.scansSubfolders)
            )
            if settings.stopOnLibraryChange {
                stopAllPlayback()
            }
            settings.libraryPath = folderURL.path
            settings.showsDemoLibrary = false
            showsMockLibrary = false
            clips = scannedClips
            selectedSidebarItem = .library
            selectedClipID = nil
            refreshGlobalShortcutRegistrations()
        } catch {
            showError(error.localizedDescription)
        }
    }

    func setScansSubfolders(_ scansSubfolders: Bool) {
        settings.scansSubfolders = scansSubfolders
        if !showsMockLibrary, !settings.libraryPath.isEmpty {
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
            playbackService.stop(clip: clip, playbackState: &playbackState)
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
        guard let index = clips.firstIndex(where: { $0.id == clip.id }) else { return }
        clips[index].isFavorite.toggle()
        guard let key = assignmentKey(for: clips[index]) else { return }
        if clips[index].isFavorite {
            settings.favoriteSoundIDs.insert(key)
        } else {
            settings.favoriteSoundIDs.remove(key)
        }
    }

    func setFavorite(_ clipsToUpdate: [SoundClip], isFavorite: Bool) {
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
    }

    func assign(_ clip: SoundClip, to category: SoundCategory) {
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
    }

    func assign(_ clipsToAssign: [SoundClip], to category: SoundCategory) {
        clipsToAssign.forEach { assign($0, to: category) }
    }

    func updateShortcut(for clip: SoundClip) {
        beginShortcutCapture(for: clip)
    }

    func beginShortcutCapture(for clip: SoundClip) {
        shortcutCaptureRequest = ShortcutCaptureRequest(clipID: clip.id)
    }

    func dismissShortcutCapture() {
        shortcutCaptureRequest = nil
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
        guard let index = clips.firstIndex(where: { $0.id == clip.id }) else { return }

        let alert = NSAlert()
        alert.messageText = "Rename Sound"
        alert.informativeText = "Enter a new name for this sound."
        alert.addButton(withTitle: "Rename")
        alert.addButton(withTitle: "Cancel")

        let textField = NSTextField(string: clip.displayName)
        textField.frame = NSRect(x: 0, y: 0, width: 280, height: 24)
        alert.accessoryView = textField

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let name = textField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty else { return }

        if playbackState.playingClipIDs.contains(clip.id) {
            stop(clip)
        }

        do {
            let oldAssignmentKey = assignmentKey(for: clips[index])
            let wasFavorite = clips[index].isFavorite
            clips[index] = try libraryService.renameClipFile(clips[index], to: name)
            if let oldAssignmentKey,
               let newAssignmentKey = assignmentKey(for: clips[index]),
               let assignedCategoryID = settings.soundCategoryAssignments.removeValue(forKey: oldAssignmentKey) {
                settings.soundCategoryAssignments[newAssignmentKey] = assignedCategoryID
            }
            if let oldAssignmentKey,
               let newAssignmentKey = assignmentKey(for: clips[index]),
               let assignedShortcut = settings.soundShortcutAssignments.removeValue(forKey: oldAssignmentKey) {
                settings.soundShortcutAssignments[newAssignmentKey] = assignedShortcut
                clips[index].shortcut = assignedShortcut
            }
            if wasFavorite,
               let oldAssignmentKey,
               let newAssignmentKey = assignmentKey(for: clips[index]) {
                settings.favoriteSoundIDs.remove(oldAssignmentKey)
                settings.favoriteSoundIDs.insert(newAssignmentKey)
                clips[index].isFavorite = true
            }
        } catch {
            showError(error.localizedDescription)
        }
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
        let removedIDs = Set(uniqueClips.map(\.id))
        uniqueClips.forEach { stop($0) }
        clips.removeAll { removedIDs.contains($0.id) }
        selectedSoundIDs.subtract(removedIDs)

        if let focusedSoundID, removedIDs.contains(focusedSoundID) {
            self.focusedSoundID = firstSelectedVisibleClipID() ?? selectedSoundIDs.first
        }

        if let selectionAnchorSoundID, removedIDs.contains(selectionAnchorSoundID) {
            self.selectionAnchorSoundID = firstSelectedVisibleClipID() ?? selectedSoundIDs.first
        }
        refreshGlobalShortcutRegistrations()
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
    }

    func changeIcon(for category: SoundCategory, to iconID: String) {
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

        return renamedCategory
    }

    func deleteCategory(_ category: SoundCategory) {
        guard canEditCategory(category) else { return }

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
        outputDevices = audioDeviceService.outputDevices()
        inputDevices = audioDeviceService.inputDevices()
        microphonePermissionState = audioPermissionService.microphonePermissionState()

        if !outputDevices.contains(where: { $0.id == settings.outputDeviceID }) {
            settings.outputDeviceID = AudioDevice.systemOutput.id
        }
        if !settings.audioRoutingMode.isImplemented {
            settings.audioRoutingMode = .speakerOnly
        }

        if let inputDeviceID = settings.inputDeviceID,
           !inputDevices.contains(where: { $0.id == inputDeviceID }) {
            settings.inputDeviceID = nil
        }

        switch microphonePermissionState {
        case .denied:
            audioStatusMessage = "Microphone permission denied. Enable access in System Settings to use input metering."
        case .missingUsageDescription:
            audioStatusMessage = "Microphone access is unavailable in this development executable. Use the Finder-launchable app bundle."
        default:
            audioStatusMessage = ""
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
        clips[index].lastPlayedAt = Date()
    }

    private func handlePlaybackFinished(_ clipID: SoundClip.ID) {
        playbackState.stop(clipID)
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
        let result = playbackService.play(clip: clip, settings: settings, playbackState: &playbackState)
        guard result.didStart else {
            if let error = result.error {
                showError(error.localizedDescription)
            }
            return false
        }

        markPlayed(clip)
        return true
    }

    private func loadInitialLibrary(usesDemoArgument: Bool) {
        if usesDemoArgument {
            loadDemoLibrary(persistChoice: false)
            return
        }

        if !settings.libraryPath.isEmpty {
            do {
                clips = try libraryService.scanLibrary(
                    at: URL(fileURLWithPath: expandedPath(settings.libraryPath)),
                    scansSubfolders: settings.scansSubfolders
                )
                clips = applyStoredClipMetadata(to: clips)
                showsMockLibrary = false
                selectedClipID = nil
                return
            } catch {
                showError(error.localizedDescription)
            }
        }

        if settings.showsDemoLibrary {
            loadDemoLibrary(persistChoice: false)
        }
    }

    private func synchronizeSelectionWithVisibleClips() {
        let visibleIDs = Set(visibleClips.map(\.id))
        selectedSoundIDs.formIntersection(visibleIDs)

        if let focusedSoundID, !visibleIDs.contains(focusedSoundID) {
            self.focusedSoundID = firstSelectedVisibleClipID()
        }

        if let selectionAnchorSoundID, !visibleIDs.contains(selectionAnchorSoundID) {
            self.selectionAnchorSoundID = firstSelectedVisibleClipID()
        }

        if selectedSoundIDs.isEmpty {
            focusedSoundID = nil
            selectionAnchorSoundID = nil
        }
    }

    private func expandedPath(_ path: String) -> String {
        NSString(string: path).expandingTildeInPath
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
