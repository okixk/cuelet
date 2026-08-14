import XCTest
@testable import Cuelet

final class LibrarySafetyTests: XCTestCase {
    func testCopyImportUsesManagedRelativePathAndCollisionWithoutOverwrite() throws {
        let root = try makeTemporaryDirectory()
        let firstSourceDirectory = try makeTemporaryDirectory()
        let secondSourceDirectory = try makeTemporaryDirectory()
        let firstSource = firstSourceDirectory.appendingPathComponent("Fixture.wav")
        let secondSource = secondSourceDirectory.appendingPathComponent("Fixture.wav")
        try Data("first".utf8).write(to: firstSource)
        try Data("second".utf8).write(to: secondSource)
        let service = LibraryService()

        let first = try service.importFiles([firstSource], mode: .copy, libraryURL: root, existingClips: [])
        let second = try service.importFiles([secondSource], mode: .copy, libraryURL: root, existingClips: first.imported)

        XCTAssertEqual(first.imported.first?.managedRelativePath, "Sounds/Fixture.wav")
        XCTAssertEqual(second.imported.first?.managedRelativePath, "Sounds/Fixture 2.wav")
        XCTAssertEqual(try String(contentsOf: root.appendingPathComponent("Sounds/Fixture.wav")), "first")
        XCTAssertEqual(try String(contentsOf: root.appendingPathComponent("Sounds/Fixture 2.wav")), "second")
        XCTAssertNotEqual(first.imported.first?.id, second.imported.first?.id)
    }

    func testDuplicateCopyImportDoesNotCreateSecondFile() throws {
        let root = try makeTemporaryDirectory()
        let source = try makeTemporaryDirectory().appendingPathComponent("Duplicate.wav")
        try Data("fixture".utf8).write(to: source)
        let service = LibraryService()
        let first = try service.importFiles([source], mode: .copy, libraryURL: root, existingClips: [])

        let duplicate = try service.importFiles([source], mode: .copy, libraryURL: root, existingClips: first.imported)

        XCTAssertTrue(duplicate.imported.isEmpty)
        XCTAssertEqual(duplicate.duplicates, [source])
        XCTAssertEqual(try FileManager.default.contentsOfDirectory(atPath: root.appendingPathComponent("Sounds").path), ["Duplicate.wav"])
    }

    func testSymlinkAndFinderAliasImportsAreRejected() throws {
        let root = try makeTemporaryDirectory()
        let source = root.appendingPathComponent("Source.wav")
        let symlink = root.appendingPathComponent("Symlink.wav")
        let alias = root.appendingPathComponent("Alias.wav")
        try Data("fixture".utf8).write(to: source)
        try FileManager.default.createSymbolicLink(at: symlink, withDestinationURL: source)
        let aliasData = try source.bookmarkData(options: [.suitableForBookmarkFile], includingResourceValuesForKeys: nil, relativeTo: nil)
        try URL.writeBookmarkData(aliasData, to: alias)
        let service = LibraryService()

        XCTAssertThrowsError(try service.importFiles([symlink], mode: .copy, libraryURL: root, existingClips: []))
        XCTAssertThrowsError(try service.importFiles([alias], mode: .link, libraryURL: root, existingClips: []))
    }

    func testLinkedBookmarkEntrySurvivesRestartAndBecomesMissing() throws {
        let root = try makeTemporaryDirectory()
        let externalDirectory = try makeTemporaryDirectory()
        let external = externalDirectory.appendingPathComponent("External.wav")
        try Data("fixture".utf8).write(to: external)
        let service = LibraryService()
        let imported = try service.importFiles([external], mode: .link, libraryURL: root, existingClips: [])
        let clip = try XCTUnwrap(imported.imported.first)
        XCTAssertNotNil(clip.securityScopedBookmark)
        let store = LibraryMetadataStore(libraryURL: root)
        try store.save(store.document(from: [clip], categories: []))

        try FileManager.default.removeItem(at: external)
        guard case .loaded(let document, _) = store.load() else { return XCTFail("metadata missing") }
        let reloaded = try service.loadLibrary(at: root, scansSubfolders: true, metadata: document)

        XCTAssertEqual(reloaded.first?.id, clip.id)
        XCTAssertEqual(reloaded.first?.storageMode, .linked)
        XCTAssertTrue(reloaded.first?.isMissing == true)
        XCTAssertEqual(reloaded.first?.externalSourcePath, external.path)
    }

    func testManagedFileReplacementIsNotSilentlyAccepted() throws {
        let root = try makeTemporaryDirectory()
        let file = root.appendingPathComponent("Replace.wav")
        try Data("original".utf8).write(to: file)
        let service = LibraryService()
        let original = try XCTUnwrap(service.scanLibrary(at: root, scansSubfolders: true).first)
        let store = LibraryMetadataStore(libraryURL: root)
        let document = store.document(from: [original], categories: [])
        try FileManager.default.removeItem(at: file)
        try Data("different content".utf8).write(to: file)

        let reloaded = try service.loadLibrary(at: root, scansSubfolders: true, metadata: document)

        XCTAssertEqual(reloaded.first?.id, original.id)
        XCTAssertTrue(reloaded.first?.isMissing == true)
    }

    func testLibraryRootAndNestedSymlinksAreRejected() throws {
        let actualRoot = try makeTemporaryDirectory()
        let linkedRoot = actualRoot.deletingLastPathComponent().appendingPathComponent(UUID().uuidString)
        try FileManager.default.createSymbolicLink(at: linkedRoot, withDestinationURL: actualRoot)
        XCTAssertThrowsError(try LibraryService().scanLibrary(at: linkedRoot, scansSubfolders: true))

        let outside = try makeTemporaryDirectory()
        try Data("outside".utf8).write(to: outside.appendingPathComponent("Escape.wav"))
        try FileManager.default.createSymbolicLink(
            at: actualRoot.appendingPathComponent("Nested"),
            withDestinationURL: outside
        )
        let unsafe = LibraryMetadataDocument(soundsByKey: [
            "Nested/Escape.wav": LibrarySoundMetadata(
                id: UUID(),
                storageMode: .managed,
                managedRelativePath: "Nested/Escape.wav",
                externalSourcePath: nil,
                originalSourcePath: nil,
                securityScopedBookmark: nil,
                displayName: "Escape",
                originalFilename: "Escape.wav",
                missing: false,
                favorite: false,
                categoryID: "uncategorized",
                notes: "",
                aliases: [],
                shortcut: nil,
                cachedDuration: 0,
                lastPlayedAt: nil,
                importedAt: Date(),
                fileIdentity: nil
            )
        ])
        XCTAssertThrowsError(try LibraryService().loadLibrary(at: actualRoot, scansSubfolders: true, metadata: unsafe))
    }

    @MainActor
    func testRemoveManagedEntryPersistsAcrossRescanAndPreservesFile() throws {
        let root = try makeTemporaryDirectory()
        let file = root.appendingPathComponent("Keep.wav")
        try Data("fixture".utf8).write(to: file)
        let appState = AppState(settingsStore: SettingsStore(url: temporarySettingsURL()), installKeyboardShortcuts: false)
        appState.loadLibrary(at: root)
        let clip = try XCTUnwrap(appState.clips.first)

        XCTAssertTrue(appState.removeLibraryEntries([clip]))
        XCTAssertTrue(FileManager.default.fileExists(atPath: file.path))
        appState.rescanLibrary()

        XCTAssertTrue(appState.clips.isEmpty)
        XCTAssertTrue(FileManager.default.fileExists(atPath: file.path))
        guard case .loaded(let metadata, _) = LibraryMetadataStore(libraryURL: root).load() else {
            return XCTFail("metadata missing")
        }
        XCTAssertEqual(metadata.ignoredManagedPaths, ["Keep.wav"])
    }

    @MainActor
    func testCorruptStartupLibraryReportsStatusWithoutPresentingModal() throws {
        let root = try makeTemporaryDirectory()
        let metadataURL = root.appendingPathComponent(LibraryMetadataStore.filename)
        let corruptMetadata = Data("{not valid json".utf8)
        try corruptMetadata.write(to: metadataURL)

        var settings = CueletSettings()
        settings.libraryPath = root.path
        let store = SettingsStore(url: temporarySettingsURL())
        XCTAssertTrue(store.save(settings))

        let appState = AppState(settingsStore: store, installKeyboardShortcuts: false)

        XCTAssertTrue(appState.clips.isEmpty)
        XCTAssertFalse(appState.persistenceStatusMessage.isEmpty)
        XCTAssertEqual(appState.settings.libraryPath, root.path)
        XCTAssertEqual(try Data(contentsOf: metadataURL), corruptMetadata)
    }

    @MainActor
    func testDeleteManagedFileRemovesFileAndEntry() throws {
        let root = try makeTemporaryDirectory()
        let file = root.appendingPathComponent("Delete.wav")
        try Data("fixture".utf8).write(to: file)
        let appState = AppState(settingsStore: SettingsStore(url: temporarySettingsURL()), installKeyboardShortcuts: false)
        appState.loadLibrary(at: root)
        let clip = try XCTUnwrap(appState.clips.first)

        XCTAssertTrue(appState.deleteManagedFileWithoutConfirmation(clip))
        XCTAssertFalse(FileManager.default.fileExists(atPath: file.path))
        XCTAssertTrue(appState.clips.isEmpty)
        appState.rescanLibrary()
        XCTAssertTrue(appState.clips.isEmpty)
    }

    @MainActor
    func testLinkedRemovalPreservesExternalFile() throws {
        let settingsURL = temporarySettingsURL()
        let appState = AppState(settingsStore: SettingsStore(url: settingsURL), installKeyboardShortcuts: false)
        let external = try makeTemporaryDirectory().appendingPathComponent("Linked.wav")
        try Data("fixture".utf8).write(to: external)
        let result = try appState.importSounds([external], mode: .link)
        let clip = try XCTUnwrap(result.imported.first)

        XCTAssertTrue(appState.removeLibraryEntries([clip]))
        XCTAssertTrue(FileManager.default.fileExists(atPath: external.path))

        let reloaded = AppState(settingsStore: SettingsStore(url: settingsURL), installKeyboardShortcuts: false)
        XCTAssertTrue(reloaded.clips.isEmpty)
        XCTAssertTrue(FileManager.default.fileExists(atPath: external.path))
    }

    @MainActor
    func testManagedImportAndRelinkPersistAcrossRestart() throws {
        let settingsURL = temporarySettingsURL()
        let sourceDirectory = try makeTemporaryDirectory()
        let source = sourceDirectory.appendingPathComponent("Managed.wav")
        try Data("fixture".utf8).write(to: source)
        let appState = AppState(settingsStore: SettingsStore(url: settingsURL), installKeyboardShortcuts: false)
        let imported = try XCTUnwrap(try appState.importSounds([source], mode: .copy).imported.first)
        let managedURL = try XCTUnwrap(imported.fileURL)
        XCTAssertTrue(FileManager.default.fileExists(atPath: managedURL.path))

        let restarted = AppState(settingsStore: SettingsStore(url: settingsURL), installKeyboardShortcuts: false)
        XCTAssertEqual(restarted.clips.first?.id, imported.id)
        XCTAssertEqual(restarted.clips.first?.managedRelativePath, imported.managedRelativePath)

        try FileManager.default.removeItem(at: managedURL)
        restarted.rescanLibrary()
        let missing = try XCTUnwrap(restarted.clips.first)
        XCTAssertTrue(missing.isMissing)
        let replacement = sourceDirectory.appendingPathComponent("Replacement.wav")
        try Data("replacement".utf8).write(to: replacement)
        try restarted.relink(missing, to: replacement)
        XCTAssertFalse(restarted.clips.first?.isMissing == true)
        XCTAssertTrue(FileManager.default.fileExists(atPath: managedURL.path))

        let reloadedAgain = AppState(settingsStore: SettingsStore(url: settingsURL), installKeyboardShortcuts: false)
        XCTAssertEqual(reloadedAgain.clips.first?.id, imported.id)
        XCTAssertFalse(reloadedAgain.clips.first?.isMissing == true)
    }

    @MainActor
    func testLinkedRenameChangesDisplayNameOnly() throws {
        let appState = AppState(settingsStore: SettingsStore(url: temporarySettingsURL()), installKeyboardShortcuts: false)
        let external = try makeTemporaryDirectory().appendingPathComponent("Original.wav")
        try Data("fixture".utf8).write(to: external)
        let clip = try XCTUnwrap(try appState.importSounds([external], mode: .link).imported.first)

        XCTAssertTrue(appState.renameDisplayName(clip, to: "Cuelet Name"))
        XCTAssertEqual(appState.clips.first?.displayName, "Cuelet Name")
        XCTAssertEqual(appState.clips.first?.externalSourcePath, external.path)
        XCTAssertTrue(FileManager.default.fileExists(atPath: external.path))
        XCTAssertEqual(external.lastPathComponent, "Original.wav")
    }

    @MainActor
    func testLegacyMacSettingsMigrationPreservesMetadataAndCreatesBackup() throws {
        let root = try makeTemporaryDirectory()
        let file = root.appendingPathComponent("Legacy.wav")
        try Data("fixture".utf8).write(to: file)
        let settingsURL = temporarySettingsURL()
        let category = SoundCategory(id: "legacy-category", name: "Legacy", defaultColorHex: "#3478F6")
        let shortcut = SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift])
        var settings = CueletSettings()
        settings.libraryPath = root.path
        settings.customCategories = [category]
        settings.categoryColorHexes[category.id] = category.defaultColorHex
        settings.soundCategoryAssignments[file.path] = category.id
        settings.soundShortcutAssignments[file.path] = shortcut
        settings.favoriteSoundIDs.insert(file.path)
        let store = SettingsStore(url: settingsURL)
        XCTAssertTrue(store.save(settings))

        let migrated = AppState(settingsStore: store, installKeyboardShortcuts: false)
        let migratedClip = try XCTUnwrap(migrated.clips.first)
        XCTAssertEqual(migratedClip.category.id, category.id)
        XCTAssertEqual(migratedClip.shortcut, shortcut)
        XCTAssertTrue(migratedClip.isFavorite)
        XCTAssertEqual(migrated.settings.libraryMetadataMigrationVersion, 2)
        XCTAssertTrue(FileManager.default.fileExists(atPath: store.legacyMetadataBackupURL.path))

        var withoutLegacyMaps = migrated.settings
        withoutLegacyMaps.soundCategoryAssignments = [:]
        withoutLegacyMaps.soundShortcutAssignments = [:]
        withoutLegacyMaps.favoriteSoundIDs = []
        XCTAssertTrue(store.save(withoutLegacyMaps))
        let reloaded = AppState(settingsStore: store, installKeyboardShortcuts: false)
        XCTAssertEqual(reloaded.clips.first?.id, migratedClip.id)
        XCTAssertEqual(reloaded.clips.first?.category.id, category.id)
        XCTAssertEqual(reloaded.clips.first?.shortcut, shortcut)
        XCTAssertTrue(reloaded.clips.first?.isFavorite == true)
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
}
