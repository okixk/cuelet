import XCTest
@testable import Cuelet

@MainActor
final class AppStateBehaviorTests: XCTestCase {
    func testRealLibraryStartsWithOnlyUncategorizedCategoryAndNoSelection() throws {
        let root = try makeTemporaryDirectory()
        try Data("sound".utf8).write(to: root.appendingPathComponent("Kick.wav"))

        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )

        appState.loadLibrary(at: root)

        XCTAssertEqual(appState.categories.map(\.id), [SoundCategory.uncategorized.id])
        XCTAssertNil(appState.selectedClipID)
    }

    func testCreatesRenamesAssignsColorsIconsAndDeletesCustomCategory() {
        let settingsURL = temporarySettingsURL()
        let appState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        appState.clips = [
            SoundClip(name: "Kick", filename: "kick.wav", category: .uncategorized, duration: 1, waveform: [])
        ]

        let category = appState.createCategory(named: "Stingers", colorHex: "#3478F6")
        appState.assign(appState.clips[0], to: category)

        XCTAssertEqual(appState.clips[0].category.id, category.id)
        XCTAssertEqual(appState.categories.map(\.name), ["Uncategorized", "Stingers"])
        XCTAssertEqual(appState.settings.categoryColorHexes[category.id], "#3478F6")

        let renamed = appState.renameCategory(category, to: "Drops")

        XCTAssertEqual(appState.categories.map(\.name), ["Uncategorized", "Drops"])
        XCTAssertEqual(appState.clips[0].category.id, renamed.id)

        appState.changeColor(for: renamed, to: "#D9822B")
        XCTAssertEqual(appState.settings.categoryColorHexes[renamed.id], "#D9822B")
        XCTAssertEqual(appState.clips[0].category.defaultColorHex, "#D9822B")

        appState.changeIcon(for: renamed, to: "waveform")
        XCTAssertEqual(appState.settings.customCategories.first { $0.id == renamed.id }?.systemImage, "waveform")
        XCTAssertEqual(appState.clips[0].category.systemImage, "waveform")

        appState.changeIcon(for: renamed, to: "face.smiling")
        XCTAssertEqual(appState.settings.customCategories.first { $0.id == renamed.id }?.systemImage, "face.smiling")
        XCTAssertEqual(appState.clips[0].category.systemImage, "face.smiling")

        let reloadedAppState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        XCTAssertEqual(reloadedAppState.settings.categoryColorHexes[renamed.id], "#D9822B")
        XCTAssertEqual(reloadedAppState.settings.customCategories.first { $0.id == renamed.id }?.systemImage, "face.smiling")

        appState.deleteCategory(renamed)

        XCTAssertEqual(appState.categories.map(\.id), [SoundCategory.uncategorized.id])
        XCTAssertEqual(appState.clips[0].category.id, SoundCategory.uncategorized.id)
    }

    func testStoredCategoryMetadataAppliesToAssignedClipsAfterReload() throws {
        let root = try makeTemporaryDirectory()
        try silentWAVData(duration: 0.1).write(to: root.appendingPathComponent("Impact.wav"))

        let settingsURL = temporarySettingsURL()
        let appState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        appState.loadLibrary(at: root)

        let category = appState.createCategory(named: "Stingers", colorHex: "#3478F6")
        appState.assign(try XCTUnwrap(appState.clips.first), to: category)
        let renamed = appState.renameCategory(category, to: "Drops")
        appState.changeColor(for: renamed, to: "#D9822B")
        appState.changeIcon(for: renamed, to: "wand.and.stars")

        let reloadedAppState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        reloadedAppState.loadLibrary(at: root)

        let reloadedClip = try XCTUnwrap(reloadedAppState.clips.first)
        XCTAssertEqual(reloadedClip.category.id, category.id)
        XCTAssertEqual(reloadedClip.category.name, "Drops")
        XCTAssertEqual(reloadedClip.category.defaultColorHex, "#D9822B")
        XCTAssertEqual(reloadedClip.category.iconID, "sparkles")
        XCTAssertEqual(reloadedClip.category.systemImage, "sparkles")
    }

    func testRenamingCategoryRejectsDuplicateNamesAndPersistsSuccessfulRename() {
        let settingsURL = temporarySettingsURL()
        let appState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )

        let firstCategory = appState.createCategory(named: "Drums", colorHex: "#3478F6")
        let secondCategory = appState.createCategory(named: "Impacts", colorHex: "#2E8B57")

        let duplicateRename = appState.renameCategory(secondCategory, to: "drums")

        XCTAssertEqual(duplicateRename.name, "Impacts")
        XCTAssertEqual(appState.settings.customCategories.map(\.name), ["Drums", "Impacts"])

        let renamed = appState.renameCategory(firstCategory, to: "Percussion")

        let reloadedAppState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        XCTAssertEqual(renamed.name, "Percussion")
        XCTAssertEqual(reloadedAppState.settings.customCategories.map(\.name), ["Percussion", "Impacts"])
    }

    func testDeletingCustomCategoryPersistsAndMovesReloadedSoundsToUncategorized() throws {
        let root = try makeTemporaryDirectory()
        try silentWAVData(duration: 0.1).write(to: root.appendingPathComponent("Hit.wav"))

        let settingsURL = temporarySettingsURL()
        let appState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        appState.loadLibrary(at: root)

        let category = appState.createCategory(named: "Hits", colorHex: "#D64545")
        let clip = try XCTUnwrap(appState.clips.first)
        appState.assign(clip, to: category)

        appState.deleteCategory(category)

        let reloadedAppState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        reloadedAppState.loadLibrary(at: root)

        XCTAssertFalse(reloadedAppState.categories.contains { $0.id == category.id })
        XCTAssertEqual(reloadedAppState.clips.first?.category.id, SoundCategory.uncategorized.id)
        XCTAssertFalse(reloadedAppState.settings.soundCategoryAssignments.values.contains(category.id))
    }

    func testSortOptionOrdersVisibleClipsAcrossNamesDatesDurationsAndCategory() {
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let drums = appState.createCategory(named: "Drums", colorHex: "#3478F6")
        let vocals = appState.createCategory(named: "Vocals", colorHex: "#D65780")
        let olderDate = Date(timeIntervalSinceReferenceDate: 100)
        let newerDate = Date(timeIntervalSinceReferenceDate: 300)
        let newestDate = Date(timeIntervalSinceReferenceDate: 500)
        appState.clips = [
            SoundClip(name: "Beta", filename: "beta.wav", category: vocals, duration: 3, waveform: [], addedAt: newerDate),
            SoundClip(name: "Alpha", filename: "alpha.wav", category: drums, duration: 8, waveform: [], addedAt: newestDate),
            SoundClip(name: "Gamma", filename: "gamma.wav", category: .uncategorized, duration: 1, waveform: [], addedAt: olderDate)
        ]

        appState.sortOption = .nameAscending
        XCTAssertEqual(appState.visibleClips.map(\.displayName), ["Alpha", "Beta", "Gamma"])

        appState.sortOption = .nameDescending
        XCTAssertEqual(appState.visibleClips.map(\.displayName), ["Gamma", "Beta", "Alpha"])

        appState.sortOption = .latestAdded
        XCTAssertEqual(appState.visibleClips.map(\.displayName), ["Alpha", "Beta", "Gamma"])

        appState.sortOption = .oldestAdded
        XCTAssertEqual(appState.visibleClips.map(\.displayName), ["Gamma", "Beta", "Alpha"])

        appState.sortOption = .durationShortest
        XCTAssertEqual(appState.visibleClips.map(\.displayName), ["Gamma", "Beta", "Alpha"])

        appState.sortOption = .durationLongest
        XCTAssertEqual(appState.visibleClips.map(\.displayName), ["Alpha", "Beta", "Gamma"])

        appState.sortOption = .category
        XCTAssertEqual(appState.visibleClips.map(\.displayName), ["Alpha", "Gamma", "Beta"])
    }

    func testSortOptionPersistsAfterReload() {
        let settingsURL = temporarySettingsURL()
        let appState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )

        appState.sortOption = .durationLongest

        let reloadedAppState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )

        XCTAssertEqual(reloadedAppState.sortOption, .durationLongest)
    }

    func testCategoryColorAndIconEditingIgnoresUneditableCategories() {
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )

        appState.changeColor(for: .uncategorized, to: "#D64545")
        appState.changeIcon(for: .uncategorized, to: "flame")

        XCTAssertEqual(appState.settings.categoryColorHexes[SoundCategory.uncategorized.id], "#8E8E93")
        XCTAssertEqual(appState.categories.first { $0.id == SoundCategory.uncategorized.id }?.iconID, "folder")
        XCTAssertEqual(appState.categories.first { $0.id == SoundCategory.uncategorized.id }?.systemImage, "folder")
    }

    func testSelectionSupportsReplaceToggleRangeAndClearing() {
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let clips = [
            SoundClip(name: "A One", filename: "a-one.wav", category: .uncategorized, duration: 1, waveform: []),
            SoundClip(name: "B Two", filename: "b-two.wav", category: .uncategorized, duration: 1, waveform: []),
            SoundClip(name: "C Three", filename: "c-three.wav", category: .uncategorized, duration: 1, waveform: []),
            SoundClip(name: "D Four", filename: "d-four.wav", category: .uncategorized, duration: 1, waveform: [])
        ]
        appState.clips = clips

        appState.select(clips[0])
        XCTAssertEqual(appState.selectedSoundIDs, [clips[0].id])
        XCTAssertEqual(appState.focusedSoundID, clips[0].id)
        XCTAssertEqual(appState.selectionAnchorSoundID, clips[0].id)

        appState.select(clips[1])
        XCTAssertEqual(appState.selectedSoundIDs, [clips[1].id])

        appState.toggleSelection(of: clips[0])
        XCTAssertEqual(appState.selectedSoundIDs, [clips[0].id, clips[1].id])
        XCTAssertEqual(appState.focusedSoundID, clips[0].id)

        appState.toggleSelection(of: clips[1])
        XCTAssertEqual(appState.selectedSoundIDs, [clips[0].id])
        XCTAssertEqual(appState.focusedSoundID, clips[1].id)

        appState.extendSelection(to: clips[3])
        XCTAssertEqual(appState.selectedSoundIDs, [clips[0].id, clips[1].id, clips[2].id, clips[3].id])
        XCTAssertEqual(appState.focusedSoundID, clips[3].id)
        XCTAssertEqual(appState.selectionAnchorSoundID, clips[0].id)

        appState.clearSelection()
        XCTAssertTrue(appState.selectedSoundIDs.isEmpty)
        XCTAssertNil(appState.focusedSoundID)
        XCTAssertNil(appState.selectionAnchorSoundID)
    }

    func testSelectionSynchronizesWithVisibleFilteredClips() {
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let clips = [
            SoundClip(name: "Rain", filename: "rain.wav", category: .ambience, duration: 1, waveform: []),
            SoundClip(name: "Door Knock", filename: "door-knock.wav", category: .effects, duration: 1, waveform: []),
            SoundClip(name: "Soft Door Knock", filename: "soft-door-knock.wav", category: .effects, duration: 1, waveform: [])
        ]
        appState.clips = clips
        appState.selectAllVisibleSounds()

        XCTAssertEqual(appState.selectedSoundIDs, Set(clips.map(\.id)))

        appState.searchText = "door"

        XCTAssertEqual(appState.selectedSoundIDs, Set(clips.dropFirst().map(\.id)))
        XCTAssertNotEqual(appState.focusedSoundID, clips[0].id)
        XCTAssertNotEqual(appState.selectionAnchorSoundID, clips[0].id)
    }

    func testKeyboardSelectionMovesAndExtendsThroughVisibleOrder() {
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let clips = [
            SoundClip(name: "A One", filename: "a-one.wav", category: .uncategorized, duration: 1, waveform: []),
            SoundClip(name: "B Two", filename: "b-two.wav", category: .uncategorized, duration: 1, waveform: []),
            SoundClip(name: "C Three", filename: "c-three.wav", category: .uncategorized, duration: 1, waveform: [])
        ]
        appState.clips = clips

        appState.selectNextVisibleClip(direction: .right)
        XCTAssertEqual(appState.selectedSoundIDs, [clips[0].id])

        appState.selectNextVisibleClip(direction: .right)
        XCTAssertEqual(appState.selectedSoundIDs, [clips[1].id])
        XCTAssertEqual(appState.selectionAnchorSoundID, clips[1].id)

        appState.selectNextVisibleClip(direction: .right, extendsSelection: true)
        XCTAssertEqual(appState.selectedSoundIDs, [clips[1].id, clips[2].id])
        XCTAssertEqual(appState.focusedSoundID, clips[2].id)
        XCTAssertEqual(appState.selectionAnchorSoundID, clips[1].id)
    }

    func testPlaybackActionsDoNotCreateSelectionState() throws {
        let root = try makeTemporaryDirectory()
        let fileURL = root.appendingPathComponent("short.wav")
        try silentWAVData(duration: 0.5).write(to: fileURL)

        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let playableClip = SoundClip(
            name: "Playable",
            filename: "short.wav",
            category: .uncategorized,
            duration: 0.5,
            waveform: [],
            fileURL: fileURL
        )
        appState.clips = [playableClip]

        appState.performPrimaryPlaybackAction(for: playableClip)
        XCTAssertTrue(appState.selectedSoundIDs.isEmpty)
        XCTAssertNil(appState.focusedSoundID)

        _ = appState.handleEscapeFromKeyboard()

        XCTAssertFalse(appState.playbackState.playingClipIDs.contains(playableClip.id))
        XCTAssertTrue(appState.selectedSoundIDs.isEmpty)
        XCTAssertNil(appState.focusedSoundID)
    }

    func testContextMenuTargetUsesCurrentSelectionOnlyWhenClickedClipIsSelected() {
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let clips = [
            SoundClip(name: "One", filename: "one.wav", category: .uncategorized, duration: 1, waveform: []),
            SoundClip(name: "Two", filename: "two.wav", category: .uncategorized, duration: 1, waveform: []),
            SoundClip(name: "Three", filename: "three.wav", category: .uncategorized, duration: 1, waveform: [])
        ]
        appState.clips = clips
        appState.select(clips[0])
        appState.toggleSelection(of: clips[1])

        XCTAssertEqual(appState.contextMenuTargetClips(for: clips[1]).map(\.id), [clips[0].id, clips[1].id])
        XCTAssertEqual(appState.contextMenuTargetClips(for: clips[2]).map(\.id), [clips[2].id])

        appState.prepareContextMenu(for: clips[2])

        XCTAssertEqual(appState.selectedSoundIDs, [clips[2].id])
        XCTAssertEqual(appState.contextMenuTargetClips(for: clips[2]).map(\.id), [clips[2].id])
    }

    func testCategoryAssignmentSurvivesRescan() throws {
        let root = try makeTemporaryDirectory()
        try Data("sound".utf8).write(to: root.appendingPathComponent("Kick.wav"))

        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        appState.loadLibrary(at: root)

        let category = appState.createCategory(named: "Drums", colorHex: "#2E8B57")
        appState.assign(try XCTUnwrap(appState.clips.first), to: category)

        appState.rescanLibrary()

        XCTAssertEqual(appState.clips.first?.category.id, category.id)
    }

    func testFavoritesPersistAcrossRealLibraryReload() throws {
        let root = try makeTemporaryDirectory()
        try silentWAVData(duration: 0.1).write(to: root.appendingPathComponent("Kick.wav"))
        try silentWAVData(duration: 0.1).write(to: root.appendingPathComponent("Snare.wav"))

        let settingsURL = temporarySettingsURL()
        let appState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        appState.loadLibrary(at: root)

        let favoriteClip = try XCTUnwrap(appState.clips.first { $0.filename == "Kick.wav" })
        appState.toggleFavorite(favoriteClip)
        XCTAssertTrue(try XCTUnwrap(appState.clips.first { $0.id == favoriteClip.id }).isFavorite)

        let reloadedAppState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        reloadedAppState.loadLibrary(at: root)
        reloadedAppState.selectedSidebarItem = .favorites

        XCTAssertEqual(reloadedAppState.visibleClips.map(\.filename), ["Kick.wav"])
        XCTAssertTrue(try XCTUnwrap(reloadedAppState.clips.first { $0.filename == "Kick.wav" }).isFavorite)
    }

    func testSoundShortcutPersistsAcrossRealLibraryReload() throws {
        let root = try makeTemporaryDirectory()
        try silentWAVData(duration: 0.1).write(to: root.appendingPathComponent("Kick.wav"))

        let settingsURL = temporarySettingsURL()
        let appState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        appState.loadLibrary(at: root)

        let clip = try XCTUnwrap(appState.clips.first)
        let shortcut = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift])
        XCTAssertEqual(appState.assignShortcut(shortcut, to: clip, replacingConflicts: false), .assigned)

        let reloadedAppState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        reloadedAppState.loadLibrary(at: root)

        XCTAssertEqual(reloadedAppState.clips.first?.shortcut, shortcut)
        XCTAssertEqual(reloadedAppState.settings.soundShortcutAssignments[root.appendingPathComponent("Kick.wav").path], shortcut)
    }

    func testReplacingConflictingSoundShortcutClearsPreviousSound() throws {
        let settingsURL = temporarySettingsURL()
        let first = SoundClip(name: "Kick", filename: "kick.wav", category: .uncategorized, duration: 1, waveform: [])
        let second = SoundClip(name: "Snare", filename: "snare.wav", category: .uncategorized, duration: 1, waveform: [])
        let shortcut = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift])
        let appState = AppState(
            settingsStore: SettingsStore(url: settingsURL),
            installKeyboardShortcuts: false
        )
        appState.clips = [first, second]

        XCTAssertEqual(appState.assignShortcut(shortcut, to: first, replacingConflicts: false), .assigned)
        XCTAssertEqual(appState.conflictingClip(for: shortcut, excluding: second.id)?.id, first.id)
        if case .conflict(let conflict) = appState.assignShortcut(shortcut, to: second, replacingConflicts: false) {
            XCTAssertEqual(conflict.id, first.id)
        } else {
            XCTFail("Expected a shortcut conflict")
        }
        XCTAssertEqual(appState.assignShortcut(shortcut, to: second, replacingConflicts: true), .assigned)

        XCTAssertNil(appState.clips.first { $0.id == first.id }?.shortcut)
        XCTAssertEqual(appState.clips.first { $0.id == second.id }?.shortcut, shortcut)
    }

    func testPlaySoundShortcutMatchesKeyCodeAndModifiersOnly() throws {
        let root = try makeTemporaryDirectory()
        let fileURL = root.appendingPathComponent("short.wav")
        try silentWAVData(duration: 0.5).write(to: fileURL)

        let storedShortcut = SoundShortcut(keyCode: 7, characters: "X", modifiers: [.command, .shift])
        let incomingShortcut = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift])
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let clip = SoundClip(
            name: "Playable",
            filename: "short.wav",
            category: .uncategorized,
            duration: 0.5,
            shortcut: storedShortcut,
            waveform: [],
            fileURL: fileURL
        )
        appState.clips = [clip]

        XCTAssertTrue(appState.playSoundShortcut(incomingShortcut))
        XCTAssertTrue(appState.playbackState.playingClipIDs.contains(clip.id))
    }

    func testPlayButtonActionDoesNotChangeSelectionWhenStartingOrStopping() throws {
        let root = try makeTemporaryDirectory()
        let fileURL = root.appendingPathComponent("short.wav")
        try silentWAVData(duration: 0.5).write(to: fileURL)

        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let selectedClip = SoundClip(
            name: "Selected",
            filename: "selected.wav",
            category: .uncategorized,
            duration: 1,
            waveform: []
        )
        let playableClip = SoundClip(
            name: "Playable",
            filename: "short.wav",
            category: .uncategorized,
            duration: 0.5,
            waveform: [],
            fileURL: fileURL
        )
        appState.clips = [selectedClip, playableClip]
        appState.select(selectedClip)

        appState.performPrimaryPlaybackAction(for: playableClip)

        XCTAssertEqual(appState.selectedClipID, selectedClip.id)
        XCTAssertTrue(appState.playbackState.playingClipIDs.contains(playableClip.id))

        appState.performPrimaryPlaybackAction(for: playableClip)

        XCTAssertEqual(appState.selectedClipID, selectedClip.id)
        XCTAssertFalse(appState.playbackState.playingClipIDs.contains(playableClip.id))
    }

    func testPlaybackServiceDoesNotMarkClipPlayingWhenFileURLIsMissing() {
        let playbackService = PlaybackService()
        var playbackState = PlaybackState()
        let clip = SoundClip(
            name: "Missing",
            filename: "missing.wav",
            category: .uncategorized,
            duration: 1,
            waveform: []
        )

        let result = playbackService.play(clip: clip, settings: CueletSettings(), playbackState: &playbackState)

        XCTAssertFalse(result.didStart)
        XCTAssertFalse(playbackState.playingClipIDs.contains(clip.id))
        XCTAssertNil(playbackState.lastPlayedClipID)
    }

    func testPlaybackStateClearsAllPlaybackMetadataWhenStoppingAll() {
        let clipID = UUID()
        var playbackState = PlaybackState()

        playbackState.markPlaying(clipID, startedAt: Date())
        playbackState.stopAll()

        XCTAssertFalse(playbackState.isPlaying)
        XCTAssertTrue(playbackState.playbackStartDatesByClipID.isEmpty)
        XCTAssertNil(playbackState.lastPlayedClipID)
    }

    func testPlaybackStateClearsWhenSoundFinishesNaturally() async throws {
        let root = try makeTemporaryDirectory()
        let fileURL = root.appendingPathComponent("short.wav")
        try silentWAVData(duration: 0.05).write(to: fileURL)

        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let clip = SoundClip(
            name: "Short",
            filename: "short.wav",
            category: .uncategorized,
            duration: 0.05,
            waveform: [],
            fileURL: fileURL
        )
        appState.clips = [clip]

        appState.play(clip)
        XCTAssertTrue(appState.playbackState.playingClipIDs.contains(clip.id))

        let deadline = Date().addingTimeInterval(2)
        while appState.playbackState.playingClipIDs.contains(clip.id), Date() < deadline {
            try await Task.sleep(nanoseconds: 50_000_000)
        }

        XCTAssertFalse(appState.playbackState.playingClipIDs.contains(clip.id))
        XCTAssertNil(appState.playbackState.playbackStartDatesByClipID[clip.id])
    }

    func testReturnPlaybackDoesNotChooseTheFirstVisibleSound() throws {
        let root = try makeTemporaryDirectory()
        let fileURL = root.appendingPathComponent("first.wav")
        try silentWAVData(duration: 0.5).write(to: fileURL)
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let clip = SoundClip(
            name: "First",
            filename: "first.wav",
            category: .uncategorized,
            duration: 0.5,
            waveform: [],
            fileURL: fileURL
        )
        appState.clips = [clip]

        appState.playSelectedVisibleSound()

        XCTAssertFalse(appState.playbackState.isPlaying)
        XCTAssertTrue(appState.selectedSoundIDs.isEmpty)
    }

    func testEscapeClearsSearchThenSelectionBeforeStoppingPlayback() throws {
        let root = try makeTemporaryDirectory()
        let fileURL = root.appendingPathComponent("playing.wav")
        try silentWAVData(duration: 1).write(to: fileURL)
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let clip = SoundClip(
            name: "Playing",
            filename: "playing.wav",
            category: .uncategorized,
            duration: 1,
            waveform: [],
            fileURL: fileURL
        )
        appState.clips = [clip]
        appState.play(clip)
        appState.select(clip)
        appState.searchText = "Play"

        XCTAssertTrue(appState.handleEscapeFromKeyboard())
        XCTAssertEqual(appState.searchText, "")
        XCTAssertTrue(appState.isSelected(clip))
        XCTAssertTrue(appState.playbackState.isPlaying)

        XCTAssertTrue(appState.handleEscapeFromKeyboard())
        XCTAssertFalse(appState.isSelected(clip))
        XCTAssertTrue(appState.playbackState.isPlaying)

        XCTAssertTrue(appState.handleEscapeFromKeyboard())
        XCTAssertFalse(appState.playbackState.isPlaying)
    }

    func testFinderRevealURLPreparationPreservesUnicodeAndOmitsMissingFiles() throws {
        let root = try makeTemporaryDirectory()
        let fileURL = root.appendingPathComponent("日本語 Geräusche.wav")
        try Data("sound".utf8).write(to: fileURL)
        let appState = AppState(
            settingsStore: SettingsStore(url: temporarySettingsURL()),
            installKeyboardShortcuts: false
        )
        let existing = SoundClip(
            name: "日本語 Geräusche",
            filename: fileURL.lastPathComponent,
            category: .uncategorized,
            duration: 1,
            waveform: [],
            fileURL: fileURL
        )
        let missing = SoundClip(
            name: "файл",
            filename: "файл.wav",
            category: .uncategorized,
            duration: 1,
            waveform: [],
            fileURL: root.appendingPathComponent("файл.wav")
        )

        XCTAssertEqual(appState.finderRevealURLs(for: [existing, missing]), [fileURL])
    }

    func testPlaybackProgressClampsToDuration() {
        XCTAssertEqual(PlaybackService.Progress(position: 2, duration: 8).fraction, 0.25)
        XCTAssertEqual(PlaybackService.Progress(position: 9, duration: 8).fraction, 1)
        XCTAssertEqual(PlaybackService.Progress(position: -1, duration: 8).fraction, 0)
        XCTAssertEqual(PlaybackService.Progress(position: 1, duration: 0).fraction, 0)
    }

    private func makeTemporaryDirectory() throws -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    private func temporarySettingsURL() -> URL {
        FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString)
            .appendingPathExtension("json")
    }

    private func silentWAVData(duration: TimeInterval) -> Data {
        let sampleRate: UInt32 = 44_100
        let bitsPerSample: UInt16 = 16
        let channelCount: UInt16 = 1
        let sampleCount = UInt32(Double(sampleRate) * duration)
        let blockAlign = channelCount * bitsPerSample / 8
        let byteRate = sampleRate * UInt32(blockAlign)
        let dataSize = sampleCount * UInt32(blockAlign)

        var data = Data()
        data.append(contentsOf: "RIFF".utf8)
        data.appendLittleEndian(36 + dataSize)
        data.append(contentsOf: "WAVE".utf8)
        data.append(contentsOf: "fmt ".utf8)
        data.appendLittleEndian(UInt32(16))
        data.appendLittleEndian(UInt16(1))
        data.appendLittleEndian(channelCount)
        data.appendLittleEndian(sampleRate)
        data.appendLittleEndian(byteRate)
        data.appendLittleEndian(blockAlign)
        data.appendLittleEndian(bitsPerSample)
        data.append(contentsOf: "data".utf8)
        data.appendLittleEndian(dataSize)
        data.append(Data(repeating: 0, count: Int(dataSize)))
        return data
    }
}

private extension Data {
    mutating func appendLittleEndian<T: FixedWidthInteger>(_ value: T) {
        var littleEndianValue = value.littleEndian
        Swift.withUnsafeBytes(of: &littleEndianValue) { bytes in
            append(contentsOf: bytes)
        }
    }
}
