import Darwin
import Foundation

struct LibraryMetadataDocument: Equatable {
    static let currentVersion = 2

    var schemaVersion = currentVersion
    var categories: [SoundCategory] = []
    var soundsByKey: [String: LibrarySoundMetadata] = [:]
    var ignoredManagedPaths: Set<String> = []
}

struct LibrarySoundMetadata: Equatable {
    var id: UUID
    var storageMode: SoundStorageMode
    var managedRelativePath: String?
    var externalSourcePath: String?
    var originalSourcePath: String?
    var securityScopedBookmark: Data?
    var displayName: String
    var originalFilename: String
    var missing: Bool
    var favorite: Bool
    var categoryID: String
    var notes: String
    var aliases: [String]
    var shortcut: SoundShortcut?
    var cachedDuration: TimeInterval
    var lastPlayedAt: Date?
    var importedAt: Date
    var fileIdentity: SoundFileIdentity?
}

struct LibraryMetadataStore {
    enum LoadResult {
        case missing
        case loaded(LibraryMetadataDocument, migratedFromVersion: Int?)
        case recovered(LibraryMetadataDocument, primaryError: String)
        case failure(String)
    }

    enum StoreError: LocalizedError {
        case invalidRoot
        case unsupportedVersion(Int)
        case corrupt(String)
        case writeFailed(String)

        var errorDescription: String? {
            switch self {
            case .invalidRoot:
                "The Cuelet metadata file does not contain a JSON object."
            case .unsupportedVersion(let version):
                "This Cuelet library uses unsupported metadata schema version \(version)."
            case .corrupt(let message):
                "Cuelet could not decode the library metadata: \(message)"
            case .writeFailed(let message):
                "Cuelet could not save the library metadata: \(message)"
            }
        }
    }

    static let filename = ".cuelet-metadata.json"

    let url: URL

    init(libraryURL: URL) {
        url = libraryURL.appendingPathComponent(Self.filename, isDirectory: false)
    }

    init(url: URL) {
        self.url = url
    }

    var backupURL: URL {
        url.appendingPathExtension("backup")
    }

    var legacyBackupURL: URL {
        url.appendingPathExtension("v1.bak")
    }

    func load() -> LoadResult {
        guard FileManager.default.fileExists(atPath: url.path) else { return .missing }

        do {
            let decoded = try decode(Data(contentsOf: url))
            let migratedVersion = decoded.originalVersion < LibraryMetadataDocument.currentVersion
                ? decoded.originalVersion
                : nil
            return .loaded(decoded.document, migratedFromVersion: migratedVersion)
        } catch {
            let primaryMessage = error.localizedDescription
            guard FileManager.default.fileExists(atPath: backupURL.path) else {
                return .failure(primaryMessage)
            }

            do {
                let recovered = try decode(Data(contentsOf: backupURL)).document
                return .recovered(recovered, primaryError: primaryMessage)
            } catch {
                return .failure("\(primaryMessage) The recovery copy also could not be decoded: \(error.localizedDescription)")
            }
        }
    }

    func save(_ document: LibraryMetadataDocument, loadedLegacyVersion: Int? = nil) throws {
        if let loadedLegacyVersion, loadedLegacyVersion < LibraryMetadataDocument.currentVersion {
            try backupLegacyFileIfNeeded()
        }
        try writeEncoded(document, preservingPrimaryAsBackup: true)
    }

    func restoreRecoveredDocument(_ document: LibraryMetadataDocument) throws {
        // Keep the known-good recovery file instead of replacing it with the corrupt primary.
        try writeEncoded(document, preservingPrimaryAsBackup: false)
    }

    func document(
        from clips: [SoundClip],
        categories: [SoundCategory],
        ignoredManagedPaths: Set<String> = []
    ) -> LibraryMetadataDocument {
        var sounds: [String: LibrarySoundMetadata] = [:]
        for clip in clips where clip.managedRelativePath != nil || clip.storageMode == .linked {
            let key = metadataKey(for: clip)
            sounds[key] = LibrarySoundMetadata(
                id: clip.id,
                storageMode: clip.storageMode,
                managedRelativePath: clip.managedRelativePath,
                externalSourcePath: clip.externalSourcePath,
                originalSourcePath: clip.originalSourcePath,
                securityScopedBookmark: clip.securityScopedBookmark,
                displayName: clip.displayName,
                originalFilename: clip.originalFilename,
                missing: clip.isMissing,
                favorite: clip.isFavorite,
                categoryID: clip.category.id,
                notes: clip.notes,
                aliases: clip.aliases,
                shortcut: clip.shortcut,
                cachedDuration: clip.duration,
                lastPlayedAt: clip.lastPlayedAt,
                importedAt: clip.addedAt,
                fileIdentity: clip.fileIdentity
            )
        }
        return LibraryMetadataDocument(
            categories: categories.filter { $0.id != SoundCategory.uncategorized.id },
            soundsByKey: sounds,
            ignoredManagedPaths: ignoredManagedPaths
        )
    }

    private func metadataKey(for clip: SoundClip) -> String {
        if clip.storageMode == .managed, let relativePath = clip.managedRelativePath, !relativePath.isEmpty {
            return relativePath.replacingOccurrences(of: "\\", with: "/")
        }
        return "@linked/\(clip.id.uuidString.lowercased())"
    }

    private func writeEncoded(_ document: LibraryMetadataDocument, preservingPrimaryAsBackup: Bool) throws {
        do {
            let parent = url.deletingLastPathComponent()
            try FileManager.default.createDirectory(
                at: parent,
                withIntermediateDirectories: true,
                attributes: [.posixPermissions: 0o700]
            )
            let data = try encode(document)

            if preservingPrimaryAsBackup, FileManager.default.fileExists(atPath: url.path) {
                let current = try Data(contentsOf: url)
                try atomicWrite(current, to: backupURL)
            }
            try atomicWrite(data, to: url)
        } catch let error as StoreError {
            throw error
        } catch {
            throw StoreError.writeFailed(error.localizedDescription)
        }
    }

    private func backupLegacyFileIfNeeded() throws {
        guard FileManager.default.fileExists(atPath: url.path),
              !FileManager.default.fileExists(atPath: legacyBackupURL.path) else { return }
        do {
            try atomicWrite(Data(contentsOf: url), to: legacyBackupURL)
        } catch {
            throw StoreError.writeFailed("The legacy metadata backup could not be created: \(error.localizedDescription)")
        }
    }

    private func atomicWrite(_ data: Data, to destination: URL) throws {
        let temporary = destination.deletingLastPathComponent().appendingPathComponent(
            ".\(destination.lastPathComponent).tmp-\(UUID().uuidString)",
            isDirectory: false
        )
        do {
            try data.write(to: temporary, options: [.withoutOverwriting])
            try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: temporary.path)
            guard Darwin.rename(temporary.path, destination.path) == 0 else {
                throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
            }
        } catch {
            try? FileManager.default.removeItem(at: temporary)
            throw error
        }
    }

    private struct DecodedDocument {
        let document: LibraryMetadataDocument
        let originalVersion: Int
    }

    private func decode(_ data: Data) throws -> DecodedDocument {
        let object: Any
        do {
            object = try JSONSerialization.jsonObject(with: data)
        } catch {
            throw StoreError.corrupt(error.localizedDescription)
        }
        guard let root = object as? [String: Any] else { throw StoreError.invalidRoot }

        let version = (root["version"] as? NSNumber)?.intValue ?? 1
        guard version <= LibraryMetadataDocument.currentVersion else {
            throw StoreError.unsupportedVersion(version)
        }

        let categories: [SoundCategory]
        do {
            categories = try decodeCategories(root["categories"])
        } catch {
            throw StoreError.corrupt("Invalid category entry: \(error.localizedDescription)")
        }

        var sounds: [String: LibrarySoundMetadata] = [:]
        if let rawSounds = root["sounds"] {
            guard let soundObjects = rawSounds as? [String: Any] else {
                throw StoreError.corrupt("The sounds member is not an object.")
            }
            for (key, value) in soundObjects {
                guard !key.isEmpty, let dictionary = value as? [String: Any] else {
                    throw StoreError.corrupt("The sound entry “\(key)” is unreadable; the original file was left untouched.")
                }
                do {
                    sounds[key] = try decodeSound(dictionary, key: key, categories: categories)
                } catch {
                    throw StoreError.corrupt("The sound entry “\(key)” is unreadable: \(error.localizedDescription). The original file was left untouched.")
                }
            }
        }

        return DecodedDocument(
            document: LibraryMetadataDocument(
                categories: categories,
                soundsByKey: sounds,
                ignoredManagedPaths: Set(root["ignoredManagedPaths"] as? [String] ?? [])
            ),
            originalVersion: version
        )
    }

    private func decodeCategories(_ object: Any?) throws -> [SoundCategory] {
        guard let object else { return [] }
        guard let array = object as? [[String: Any]] else {
            throw StoreError.corrupt("The categories member is not an array.")
        }
        return try array.map { category in
            guard let id = category["id"] as? String, !id.isEmpty,
                  let name = category["name"] as? String, !name.isEmpty else {
                throw StoreError.corrupt("A category is missing its ID or name.")
            }
            return SoundCategory(
                id: id,
                name: name,
                defaultColorHex: category["color"] as? String
                    ?? category["defaultColorHex"] as? String
                    ?? "#8E8E93",
                iconID: category["icon"] as? String
                    ?? category["iconID"] as? String
                    ?? category["systemImage"] as? String
                    ?? "tag",
                isUserEditable: category["editable"] as? Bool
                    ?? category["isUserEditable"] as? Bool
                    ?? true
            )
        }.filter { $0.id != SoundCategory.uncategorized.id }
    }

    private func decodeSound(
        _ object: [String: Any],
        key: String,
        categories: [SoundCategory]
    ) throws -> LibrarySoundMetadata {
        let id = (object["soundId"] as? String).flatMap(UUID.init(uuidString:))
            ?? stableUUID(for: key)
        let storageMode = SoundStorageMode(rawValue: object["storageMode"] as? String ?? "managed") ?? .managed
        let filename = object["sourceFileName"] as? String
            ?? object["originalFilename"] as? String
            ?? URL(fileURLWithPath: object["externalPath"] as? String ?? key).lastPathComponent
        let legacyCategoryName = object["category"] as? String
        let categoryID = object["categoryId"] as? String
            ?? categories.first(where: { $0.name == legacyCategoryName })?.id
            ?? SoundCategory.uncategorized.id
        let shortcut: SoundShortcut?
        if let shortcutObject = object["shortcut"] {
            let shortcutData = try JSONSerialization.data(withJSONObject: shortcutObject)
            shortcut = try JSONDecoder().decode(SoundShortcut.self, from: shortcutData)
        } else {
            shortcut = nil
        }

        return LibrarySoundMetadata(
            id: id,
            storageMode: storageMode,
            managedRelativePath: object["managedRelativePath"] as? String
                ?? (storageMode == .managed ? key : nil),
            externalSourcePath: object["externalPath"] as? String,
            originalSourcePath: object["originalSourcePath"] as? String,
            securityScopedBookmark: (object["securityScopedBookmark"] as? String).flatMap { Data(base64Encoded: $0) },
            displayName: object["displayName"] as? String
                ?? object["title"] as? String
                ?? URL(fileURLWithPath: filename).deletingPathExtension().lastPathComponent,
            originalFilename: filename,
            missing: object["missing"] as? Bool ?? false,
            favorite: object["favorite"] as? Bool ?? false,
            categoryID: categoryID,
            notes: object["notes"] as? String ?? object["note"] as? String ?? "",
            aliases: object["aliases"] as? [String] ?? [],
            shortcut: shortcut,
            cachedDuration: (object["durationSeconds"] as? NSNumber)?.doubleValue ?? 0,
            lastPlayedAt: date(from: object["lastPlayedAt"]),
            importedAt: date(from: object["addedAt"]) ?? .distantPast,
            fileIdentity: decodeIdentity(object)
        )
    }

    private func encode(_ document: LibraryMetadataDocument) throws -> Data {
        var root: [String: Any] = ["version": LibraryMetadataDocument.currentVersion]
        root["categories"] = document.categories.map { category in
            [
                "id": category.id,
                "name": category.name,
                "color": category.defaultColorHex,
                "icon": category.iconID,
                "editable": category.isUserEditable
            ] as [String: Any]
        }

        var sounds: [String: Any] = [:]
        for (key, sound) in document.soundsByKey {
            var object: [String: Any] = [
                "soundId": sound.id.uuidString.lowercased(),
                "storageMode": sound.storageMode.rawValue,
                "displayName": sound.displayName,
                "title": sound.displayName,
                "sourceFileName": sound.originalFilename,
                "missing": sound.missing,
                "favorite": sound.favorite,
                "categoryId": sound.categoryID,
                "notes": sound.notes,
                "aliases": sound.aliases,
                "durationSeconds": sound.cachedDuration.isFinite && sound.cachedDuration >= 0 ? sound.cachedDuration : 0,
                "durationKnown": sound.cachedDuration > 0,
                "addedAt": sound.importedAt.timeIntervalSince1970
            ]
            if let managedRelativePath = sound.managedRelativePath { object["managedRelativePath"] = managedRelativePath }
            if let externalSourcePath = sound.externalSourcePath { object["externalPath"] = externalSourcePath }
            if let originalSourcePath = sound.originalSourcePath { object["originalSourcePath"] = originalSourcePath }
            if let securityScopedBookmark = sound.securityScopedBookmark {
                object["securityScopedBookmark"] = securityScopedBookmark.base64EncodedString()
            }
            if let lastPlayedAt = sound.lastPlayedAt { object["lastPlayedAt"] = lastPlayedAt.timeIntervalSince1970 }
            if let shortcut = sound.shortcut {
                object["shortcut"] = try JSONSerialization.jsonObject(with: JSONEncoder().encode(shortcut))
            }
            if let identity = sound.fileIdentity {
                if let value = identity.resourceIdentifier { object["fileResourceIdentifier"] = value }
                if let value = identity.volumeIdentifier { object["volumeIdentifier"] = value }
                if let value = identity.fileSize { object["fileSize"] = value }
                if let value = identity.contentModificationDate {
                    object["contentModificationDate"] = value.timeIntervalSince1970
                }
            }
            sounds[key] = object
        }
        root["sounds"] = sounds
        root["ignoredManagedPaths"] = document.ignoredManagedPaths.sorted()
        return try JSONSerialization.data(withJSONObject: root, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
    }

    private func date(from value: Any?) -> Date? {
        guard let seconds = (value as? NSNumber)?.doubleValue, seconds.isFinite else { return nil }
        return Date(timeIntervalSince1970: seconds)
    }

    private func decodeIdentity(_ object: [String: Any]) -> SoundFileIdentity? {
        let identity = SoundFileIdentity(
            resourceIdentifier: object["fileResourceIdentifier"] as? String,
            volumeIdentifier: object["volumeIdentifier"] as? String,
            fileSize: (object["fileSize"] as? NSNumber)?.int64Value,
            contentModificationDate: date(from: object["contentModificationDate"])
        )
        return identity.resourceIdentifier == nil
            && identity.volumeIdentifier == nil
            && identity.fileSize == nil
            && identity.contentModificationDate == nil
            ? nil
            : identity
    }

    private func stableUUID(for key: String) -> UUID {
        var first: UInt64 = 14_695_981_039_346_656_037
        var second: UInt64 = 10_995_116_282_11
        for byte in key.utf8 {
            first ^= UInt64(byte)
            first &*= 1_099_511_628_211
            second = (second &* 33) ^ UInt64(byte)
        }
        let text = String(
            format: "%08llx-%04llx-4%03llx-8%03llx-%012llx",
            (first >> 32) & 0xFFFF_FFFF,
            (first >> 16) & 0xFFFF,
            first & 0x0FFF,
            (second >> 48) & 0x0FFF,
            second & 0xFFFF_FFFF_FFFF
        )
        return UUID(uuidString: text) ?? UUID()
    }
}
