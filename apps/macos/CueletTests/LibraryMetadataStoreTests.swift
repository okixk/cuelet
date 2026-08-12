import XCTest
@testable import Cuelet

final class LibraryMetadataStoreTests: XCTestCase {
    func testSchemaV2RoundTripPreservesPortableAndMacFields() throws {
        let root = try makeTemporaryDirectory()
        let fileURL = root.appendingPathComponent("Sounds/Fixture Unicode é.wav")
        try FileManager.default.createDirectory(at: fileURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data("audio".utf8).write(to: fileURL)
        let category = SoundCategory(id: "user-fixture", name: "Fixtures", defaultColorHex: "#3478F6", iconID: "sparkles")
        let id = UUID()
        let clip = SoundClip(
            id: id,
            name: "Renamed Fixture",
            filename: fileURL.lastPathComponent,
            category: category,
            duration: 2.5,
            shortcut: SoundShortcut(keyCode: 7, characters: "x", modifiers: [.command, .shift], scope: .global),
            waveform: [],
            isFavorite: true,
            addedAt: Date(timeIntervalSince1970: 1_783_180_800),
            lastPlayedAt: Date(timeIntervalSince1970: 1_783_180_900),
            fileURL: fileURL,
            storageMode: .managed,
            managedRelativePath: "Sounds/Fixture Unicode é.wav",
            originalSourcePath: "/tmp/source.wav",
            originalFilename: "source.wav",
            notes: "Layered fixture",
            aliases: ["boom", "slam"],
            fileIdentity: LibraryService().filesystemIdentity(for: fileURL)
        )
        let store = LibraryMetadataStore(libraryURL: root)
        let document = store.document(
            from: [clip],
            categories: [category],
            ignoredManagedPaths: ["Sounds/Removed.wav"]
        )

        try store.save(document)

        guard case .loaded(let loaded, let migratedFromVersion) = store.load() else {
            return XCTFail("Expected metadata to load")
        }
        XCTAssertNil(migratedFromVersion)
        XCTAssertEqual(loaded.schemaVersion, 2)
        XCTAssertEqual(loaded.categories, [category])
        XCTAssertEqual(loaded.ignoredManagedPaths, ["Sounds/Removed.wav"])
        let sound = try XCTUnwrap(loaded.soundsByKey["Sounds/Fixture Unicode é.wav"])
        XCTAssertEqual(sound.id, id)
        XCTAssertEqual(sound.storageMode, .managed)
        XCTAssertEqual(sound.displayName, "Renamed Fixture")
        XCTAssertEqual(sound.originalFilename, "source.wav")
        XCTAssertEqual(sound.notes, "Layered fixture")
        XCTAssertEqual(sound.aliases, ["boom", "slam"])
        XCTAssertEqual(sound.shortcut?.scope, .global)
        XCTAssertTrue(sound.favorite)

        let permissions = try XCTUnwrap(
            (try FileManager.default.attributesOfItem(atPath: store.url.path)[.posixPermissions]) as? NSNumber
        )
        XCTAssertEqual(permissions.intValue & 0o777, 0o600)
    }

    func testLegacyFixtureMigratesIdempotentlyAndCreatesBackup() throws {
        let root = try makeTemporaryDirectory()
        let store = LibraryMetadataStore(libraryURL: root)
        let fixture = """
        {
          "sounds": {
            "fx/impact.wav": {
              "title": "Impact",
              "category": "FX",
              "favorite": true,
              "notes": "Legacy note",
              "aliases": ["boom"]
            }
          }
        }
        """
        try Data(fixture.utf8).write(to: store.url)

        guard case .loaded(let migrated, let oldVersion) = store.load() else {
            return XCTFail("Expected legacy metadata")
        }
        XCTAssertEqual(oldVersion, 1)
        let firstID = try XCTUnwrap(migrated.soundsByKey["fx/impact.wav"]?.id)
        XCTAssertEqual(migrated.soundsByKey["fx/impact.wav"]?.displayName, "Impact")

        try store.save(migrated, loadedLegacyVersion: oldVersion)
        XCTAssertTrue(FileManager.default.fileExists(atPath: store.legacyBackupURL.path))

        guard case .loaded(let reloaded, let secondMigration) = store.load() else {
            return XCTFail("Expected migrated metadata")
        }
        XCTAssertNil(secondMigration)
        XCTAssertEqual(reloaded.soundsByKey["fx/impact.wav"]?.id, firstID)
        XCTAssertEqual(try Data(contentsOf: store.legacyBackupURL), Data(fixture.utf8))
    }

    func testCorruptPrimaryRecoversFromBackupWithoutSilentReset() throws {
        let root = try makeTemporaryDirectory()
        let store = LibraryMetadataStore(libraryURL: root)
        let first = LibraryMetadataDocument(
            soundsByKey: ["one.wav": metadata(id: UUID(), name: "One", relativePath: "one.wav")]
        )
        let second = LibraryMetadataDocument(
            soundsByKey: ["two.wav": metadata(id: UUID(), name: "Two", relativePath: "two.wav")]
        )
        try store.save(first)
        try store.save(second)
        try Data("{ damaged".utf8).write(to: store.url)

        guard case .recovered(let recovered, let primaryError) = store.load() else {
            return XCTFail("Expected backup recovery")
        }
        XCTAssertFalse(primaryError.isEmpty)
        XCTAssertNotNil(recovered.soundsByKey["one.wav"])
        XCTAssertNil(recovered.soundsByKey["two.wav"])
        XCTAssertEqual(try Data(contentsOf: store.url), Data("{ damaged".utf8))
    }

    func testUnreadableSoundEntryFailsWithoutChangingOriginal() throws {
        let root = try makeTemporaryDirectory()
        let store = LibraryMetadataStore(libraryURL: root)
        let data = Data(#"{"version":2,"sounds":{"bad.wav":"not an object"}}"#.utf8)
        try data.write(to: store.url)

        guard case .failure(let message) = store.load() else {
            return XCTFail("Expected explicit failure")
        }
        XCTAssertTrue(message.contains("bad.wav"))
        XCTAssertEqual(try Data(contentsOf: store.url), data)
    }

    func testMissingManagedAndLinkedEntriesRemainInMergedLibrary() throws {
        let root = try makeTemporaryDirectory()
        let managedID = UUID()
        let linkedID = UUID()
        let document = LibraryMetadataDocument(soundsByKey: [
            "Sounds/Missing.wav": metadata(id: managedID, name: "Managed Missing", relativePath: "Sounds/Missing.wav"),
            "@linked/fixture": LibrarySoundMetadata(
                id: linkedID,
                storageMode: .linked,
                managedRelativePath: nil,
                externalSourcePath: root.appendingPathComponent("External Missing.wav").path,
                originalSourcePath: root.appendingPathComponent("External Missing.wav").path,
                securityScopedBookmark: nil,
                displayName: "Linked Missing",
                originalFilename: "External Missing.wav",
                missing: true,
                favorite: true,
                categoryID: "uncategorized",
                notes: "keep me",
                aliases: [],
                shortcut: nil,
                cachedDuration: 0,
                lastPlayedAt: nil,
                importedAt: Date(),
                fileIdentity: nil
            )
        ])

        let clips = try LibraryService().loadLibrary(at: root, scansSubfolders: true, metadata: document)

        XCTAssertEqual(Set(clips.map(\.id)), [managedID, linkedID])
        XCTAssertTrue(clips.allSatisfy(\.isMissing))
        XCTAssertEqual(clips.first { $0.id == linkedID }?.notes, "keep me")
        XCTAssertTrue(clips.first { $0.id == linkedID }?.isFavorite == true)
    }

    private func metadata(id: UUID, name: String, relativePath: String) -> LibrarySoundMetadata {
        LibrarySoundMetadata(
            id: id,
            storageMode: .managed,
            managedRelativePath: relativePath,
            externalSourcePath: nil,
            originalSourcePath: nil,
            securityScopedBookmark: nil,
            displayName: name,
            originalFilename: URL(fileURLWithPath: relativePath).lastPathComponent,
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
    }

    private func makeTemporaryDirectory() throws -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }
}
