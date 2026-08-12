import AVFoundation
import AppKit
import Foundation

struct LibraryService {
    enum ImportMode: String, CaseIterable, Identifiable {
        case copy
        case link

        var id: String { rawValue }
    }

    struct ImportResult {
        var imported: [SoundClip]
        var duplicates: [URL]
        var createdManagedFiles: [URL]
    }

    struct StagedManagedDeletion {
        let originalURL: URL
        let stagedURL: URL
    }

    enum LibraryError: LocalizedError {
        case unreadableFolder(URL)
        case missingFileURL
        case fileUnavailable(URL)
        case invalidRename(String)
        case targetAlreadyExists(URL)
        case renameFailed(Error)
        case unsafeLibraryRoot(URL)
        case unsupportedFile(URL)
        case unsafeSource(URL)
        case sourceChanged(URL)
        case unsafeManagedPath(String)
        case managedFileChanged(URL)
        case linkedFileDeletion
        case deleteFailed(Error)

        var errorDescription: String? {
            switch self {
            case .unreadableFolder(let url):
                "Cuelet could not read “\(url.path)”. Check folder permissions and try again."
            case .missingFileURL:
                "Cuelet cannot rename this sound because it is not linked to an audio file on disk."
            case .fileUnavailable(let url):
                "Cuelet could not find “\(url.lastPathComponent)” on disk. Rescan the library and try again."
            case .invalidRename(let name):
                "“\(name)” is not a valid file name. Avoid slashes, colons, and empty names."
            case .targetAlreadyExists(let url):
                "A file named “\(url.lastPathComponent)” already exists. Choose a different name."
            case .renameFailed(let error):
                "Cuelet could not rename the file: \(error.localizedDescription)"
            case .unsafeLibraryRoot(let url):
                "Cuelet cannot use “\(url.lastPathComponent)” because the library root is a symbolic link or is not a readable directory."
            case .unsupportedFile(let url):
                "“\(url.lastPathComponent)” is not a supported audio file."
            case .unsafeSource(let url):
                "Cuelet refused “\(url.lastPathComponent)” because symbolic links and Finder aliases are not safe import sources."
            case .sourceChanged(let url):
                "“\(url.lastPathComponent)” changed while Cuelet was importing it. Nothing was overwritten."
            case .unsafeManagedPath(let path):
                "Cuelet refused the managed path “\(path)” because it does not stay inside the library."
            case .managedFileChanged(let url):
                "“\(url.lastPathComponent)” changed since Cuelet last recorded it. Rescan or relink it before deleting."
            case .linkedFileDeletion:
                "Linked files cannot be deleted from Cuelet. Remove the library entry instead."
            case .deleteFailed(let error):
                "Cuelet could not delete the managed file: \(error.localizedDescription)"
            }
        }
    }

    private let supportedAudioExtensions: Set<String> = ["mp3", "wav", "m4a", "aiff", "aif", "flac"]

    func scanLibrary(at folderURL: URL, scansSubfolders: Bool) throws -> [SoundClip] {
        try validateLibraryRoot(folderURL)
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: folderURL.path, isDirectory: &isDirectory), isDirectory.boolValue else {
            throw LibraryError.unreadableFolder(folderURL)
        }

        guard FileManager.default.isReadableFile(atPath: folderURL.path) else {
            throw LibraryError.unreadableFolder(folderURL)
        }

        let fileURLs: [URL]
        if scansSubfolders {
            let resourceKeys: [URLResourceKey] = [.isRegularFileKey, .isReadableKey]
            let enumerator = FileManager.default.enumerator(
                at: folderURL,
                includingPropertiesForKeys: resourceKeys,
                options: [.skipsHiddenFiles, .skipsPackageDescendants]
            )
            fileURLs = (enumerator?.compactMap { $0 as? URL } ?? []).filter(isSupportedAudioFile)
        } else {
            fileURLs = try FileManager.default.contentsOfDirectory(
                at: folderURL,
                includingPropertiesForKeys: [.isRegularFileKey, .isReadableKey],
                options: [.skipsHiddenFiles]
            ).filter(isSupportedAudioFile)
        }

        return fileURLs
            .sorted { lhs, rhs in lhs.lastPathComponent.localizedStandardCompare(rhs.lastPathComponent) == .orderedAscending }
            .enumerated()
            .map { offset, url in
                makeClip(
                    from: url,
                    fallbackOrder: offset,
                    managedRelativePath: relativePath(for: url, libraryURL: folderURL)
                )
            }
    }

    func loadLibrary(
        at folderURL: URL,
        scansSubfolders: Bool,
        metadata: LibraryMetadataDocument
    ) throws -> [SoundClip] {
        let scanned = try scanLibrary(at: folderURL, scansSubfolders: scansSubfolders)
        let scannedByRelativePath = Dictionary(uniqueKeysWithValues: scanned.compactMap { clip in
            clip.managedRelativePath.map { ($0, clip) }
        })
        let categoryByID = Dictionary(
            uniqueKeysWithValues: ([SoundCategory.uncategorized] + metadata.categories).map { ($0.id, $0) }
        )
        var result: [SoundClip] = []
        var representedManagedPaths: Set<String> = []

        for (key, stored) in metadata.soundsByKey.sorted(by: { $0.key < $1.key }) {
            let category = categoryByID[stored.categoryID] ?? .uncategorized
            switch stored.storageMode {
            case .managed:
                let relativePath = stored.managedRelativePath ?? key
                representedManagedPaths.insert(relativePath)
                let url = try safeManagedURL(relativePath: relativePath, libraryURL: folderURL)
                let scannedClip = scannedByRelativePath[relativePath]
                let exists = scannedClip != nil && FileManager.default.fileExists(atPath: url.path)
                let currentIdentity = exists ? filesystemIdentity(for: url) : nil
                let identityMatches = stored.fileIdentity.map { storedIdentity in
                    currentIdentity.map { identitiesMatch(storedIdentity, $0) } ?? false
                } ?? true
                result.append(SoundClip(
                    id: stored.id,
                    name: stored.displayName,
                    filename: url.lastPathComponent.isEmpty ? stored.originalFilename : url.lastPathComponent,
                    category: category,
                    duration: stored.cachedDuration > 0 ? stored.cachedDuration : (scannedClip?.duration ?? 0),
                    shortcut: stored.shortcut,
                    waveform: scannedClip?.waveform ?? defaultWaveform(for: stored.originalFilename),
                    isFavorite: stored.favorite,
                    addedAt: stored.importedAt,
                    lastPlayedAt: stored.lastPlayedAt,
                    fileURL: url,
                    storageMode: .managed,
                    managedRelativePath: relativePath,
                    externalSourcePath: nil,
                    originalSourcePath: stored.originalSourcePath,
                    isMissing: !exists || !identityMatches,
                    originalFilename: stored.originalFilename,
                    notes: stored.notes,
                    aliases: stored.aliases,
                    fileIdentity: stored.fileIdentity ?? currentIdentity
                ))
            case .linked:
                let resolution = resolveLinkedURL(stored)
                let resolvedURL = resolution.url
                let exists = resolvedURL.map { isUsableAudioFile($0) } ?? false
                let currentIdentity = exists ? resolvedURL.flatMap(filesystemIdentity) : nil
                let identityMatches = stored.fileIdentity.map { storedIdentity in
                    currentIdentity.map { identitiesMatch(storedIdentity, $0) } ?? false
                } ?? true
                result.append(SoundClip(
                    id: stored.id,
                    name: stored.displayName,
                    filename: resolvedURL?.lastPathComponent ?? stored.originalFilename,
                    category: category,
                    duration: stored.cachedDuration,
                    shortcut: stored.shortcut,
                    waveform: defaultWaveform(for: stored.originalFilename),
                    isFavorite: stored.favorite,
                    addedAt: stored.importedAt,
                    lastPlayedAt: stored.lastPlayedAt,
                    fileURL: resolvedURL ?? stored.externalSourcePath.map { URL(fileURLWithPath: $0) },
                    storageMode: .linked,
                    managedRelativePath: nil,
                    externalSourcePath: resolvedURL?.path ?? stored.externalSourcePath,
                    originalSourcePath: stored.originalSourcePath ?? stored.externalSourcePath,
                    securityScopedBookmark: stored.securityScopedBookmark,
                    isBookmarkStale: resolution.isStale,
                    isMissing: !exists || !identityMatches,
                    originalFilename: stored.originalFilename,
                    notes: stored.notes,
                    aliases: stored.aliases,
                    fileIdentity: stored.fileIdentity ?? currentIdentity
                ))
            }
        }

        result.append(contentsOf: scanned.filter { clip in
            guard let relativePath = clip.managedRelativePath else { return false }
            return !representedManagedPaths.contains(relativePath)
                && !metadata.ignoredManagedPaths.contains(relativePath)
        })
        return result
    }

    func importFiles(
        _ fileURLs: [URL],
        mode: ImportMode,
        libraryURL: URL,
        existingClips: [SoundClip]
    ) throws -> ImportResult {
        try validateLibraryRoot(libraryURL)
        var imported: [SoundClip] = []
        var duplicates: [URL] = []
        var createdFiles: [URL] = []

        for source in fileURLs {
            guard isSupportedAudioFile(source) else { throw LibraryError.unsupportedFile(source) }
            guard !isSymbolicLinkOrAlias(source) else { throw LibraryError.unsafeSource(source) }
            let beforeIdentity = try requiredIdentity(for: source)
            let allExisting = existingClips + imported
            if allExisting.contains(where: { clip in
                clip.originalSourcePath == source.standardizedFileURL.path
                    || clip.externalSourcePath == source.standardizedFileURL.path
                    || clip.fileIdentity.map { identitiesMatch($0, beforeIdentity) } == true
            }) {
                duplicates.append(source)
                continue
            }

            switch mode {
            case .copy:
                let soundsDirectory = libraryURL.appendingPathComponent("Sounds", isDirectory: true)
                try FileManager.default.createDirectory(
                    at: soundsDirectory,
                    withIntermediateDirectories: true,
                    attributes: [.posixPermissions: 0o700]
                )
                let destination = collisionSafeDestination(for: source.lastPathComponent, in: soundsDirectory)
                var coordinationError: NSError?
                var copyError: Error?
                NSFileCoordinator().coordinate(
                    readingItemAt: source,
                    options: [.withoutChanges],
                    error: &coordinationError
                ) { coordinatedSource in
                    do {
                        try FileManager.default.copyItem(at: coordinatedSource, to: destination)
                    } catch {
                        copyError = error
                    }
                }
                if let coordinationError { throw coordinationError }
                if let copyError { throw copyError }

                guard let afterIdentity = filesystemIdentity(for: source),
                      identitiesMatch(beforeIdentity, afterIdentity) else {
                    try? FileManager.default.removeItem(at: destination)
                    throw LibraryError.sourceChanged(source)
                }
                createdFiles.append(destination)
                let relativePath = relativePath(for: destination, libraryURL: libraryURL)
                var clip = makeClip(
                    from: destination,
                    fallbackOrder: existingClips.count + imported.count,
                    managedRelativePath: relativePath,
                    id: UUID()
                )
                clip.originalFilename = source.lastPathComponent
                clip.originalSourcePath = source.standardizedFileURL.path
                imported.append(clip)
            case .link:
                let bookmark = makeBookmark(for: source)
                imported.append(SoundClip(
                    name: source.deletingPathExtension().lastPathComponent,
                    filename: source.lastPathComponent,
                    category: .uncategorized,
                    duration: duration(for: source),
                    waveform: defaultWaveform(for: source.lastPathComponent),
                    addedAt: Date(),
                    fileURL: source,
                    storageMode: .linked,
                    externalSourcePath: source.standardizedFileURL.path,
                    originalSourcePath: source.standardizedFileURL.path,
                    securityScopedBookmark: bookmark,
                    originalFilename: source.lastPathComponent,
                    fileIdentity: beforeIdentity
                ))
            }
        }

        return ImportResult(imported: imported, duplicates: duplicates, createdManagedFiles: createdFiles)
    }

    func rollbackCreatedManagedFiles(_ urls: [URL], libraryURL: URL) {
        for url in urls {
            let relativePath = relativePath(for: url, libraryURL: libraryURL)
            guard (try? safeManagedURL(relativePath: relativePath, libraryURL: libraryURL)) == url.standardizedFileURL else {
                continue
            }
            try? FileManager.default.removeItem(at: url)
        }
    }

    func deleteManagedFile(_ clip: SoundClip, libraryURL: URL) throws {
        let staged = try stageManagedDeletion(clip, libraryURL: libraryURL)
        try commitManagedDeletion(staged)
    }

    func stageManagedDeletion(_ clip: SoundClip, libraryURL: URL) throws -> StagedManagedDeletion {
        guard clip.storageMode == .managed else { throw LibraryError.linkedFileDeletion }
        guard let relativePath = clip.managedRelativePath else {
            throw LibraryError.unsafeManagedPath(clip.filename)
        }
        let target = try safeManagedURL(relativePath: relativePath, libraryURL: libraryURL)
        guard FileManager.default.fileExists(atPath: target.path), !isSymbolicLinkOrAlias(target) else {
            throw LibraryError.fileUnavailable(target)
        }
        guard let currentIdentity = filesystemIdentity(for: target),
              clip.fileIdentity.map({ identitiesMatch($0, currentIdentity) }) ?? true else {
            throw LibraryError.managedFileChanged(target)
        }
        let staged = target.deletingLastPathComponent().appendingPathComponent(
            ".cuelet-delete-\(UUID().uuidString)-\(target.lastPathComponent)"
        )
        do {
            try FileManager.default.moveItem(at: target, to: staged)
        } catch {
            throw LibraryError.deleteFailed(error)
        }
        return StagedManagedDeletion(originalURL: target, stagedURL: staged)
    }

    func commitManagedDeletion(_ staged: StagedManagedDeletion) throws {
        do {
            try FileManager.default.removeItem(at: staged.stagedURL)
        } catch {
            throw LibraryError.deleteFailed(error)
        }
    }

    func rollbackManagedDeletion(_ staged: StagedManagedDeletion) {
        guard FileManager.default.fileExists(atPath: staged.stagedURL.path),
              !FileManager.default.fileExists(atPath: staged.originalURL.path) else { return }
        try? FileManager.default.moveItem(at: staged.stagedURL, to: staged.originalURL)
    }

    func relink(_ clip: SoundClip, to source: URL, libraryURL: URL) throws -> SoundClip {
        guard isSupportedAudioFile(source), !isSymbolicLinkOrAlias(source) else {
            throw LibraryError.unsafeSource(source)
        }
        var updated = clip
        let identity = try requiredIdentity(for: source)
        if clip.storageMode == .linked {
            updated.fileURL = source
            updated.externalSourcePath = source.standardizedFileURL.path
            updated.originalSourcePath = source.standardizedFileURL.path
            updated.securityScopedBookmark = makeBookmark(for: source)
        } else {
            guard let relativePath = clip.managedRelativePath else {
                throw LibraryError.unsafeManagedPath(clip.filename)
            }
            let destination = try safeManagedURL(relativePath: relativePath, libraryURL: libraryURL)
            guard !FileManager.default.fileExists(atPath: destination.path) else {
                throw LibraryError.targetAlreadyExists(destination)
            }
            try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
            try FileManager.default.copyItem(at: source, to: destination)
            updated.fileURL = destination
            updated.originalSourcePath = source.standardizedFileURL.path
            updated.fileIdentity = filesystemIdentity(for: destination)
        }
        updated.filename = updated.fileURL?.lastPathComponent ?? source.lastPathComponent
        updated.originalFilename = source.lastPathComponent
        updated.duration = duration(for: updated.fileURL ?? source)
        updated.isMissing = false
        updated.isBookmarkStale = false
        if updated.storageMode == .linked { updated.fileIdentity = identity }
        return updated
    }

    func importFiles(_ fileURLs: [URL], into clips: [SoundClip]) -> [SoundClip] {
        let importedClips = fileURLs
            .filter(isSupportedAudioFile)
            .sorted { lhs, rhs in lhs.lastPathComponent.localizedStandardCompare(rhs.lastPathComponent) == .orderedAscending }
            .enumerated()
            .map { offset, url in
                makeClip(from: url, fallbackOrder: clips.count + offset, managedRelativePath: nil)
            }

        return clips + importedClips.filter { importedClip in
            !clips.contains { $0.fileURL == importedClip.fileURL }
        }
    }

    func renameClipFile(_ clip: SoundClip, to proposedName: String) throws -> SoundClip {
        guard let sourceURL = clip.fileURL else {
            throw LibraryError.missingFileURL
        }

        let trimmedName = proposedName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard isValidFileBaseName(trimmedName) else {
            throw LibraryError.invalidRename(proposedName)
        }

        guard FileManager.default.fileExists(atPath: sourceURL.path) else {
            throw LibraryError.fileUnavailable(sourceURL)
        }

        let fileExtension = sourceURL.pathExtension
        let targetFilename = fileExtension.isEmpty ? trimmedName : "\(trimmedName).\(fileExtension)"
        let targetURL = sourceURL
            .deletingLastPathComponent()
            .appendingPathComponent(targetFilename, isDirectory: false)

        if sourceURL.standardizedFileURL == targetURL.standardizedFileURL {
            var updatedClip = clip
            updatedClip.name = trimmedName
            updatedClip.filename = targetFilename
            updatedClip.fileURL = targetURL
            return updatedClip
        }

        if FileManager.default.fileExists(atPath: targetURL.path) {
            throw LibraryError.targetAlreadyExists(targetURL)
        }

        do {
            try FileManager.default.moveItem(at: sourceURL, to: targetURL)
        } catch {
            throw LibraryError.renameFailed(error)
        }

        var updatedClip = clip
        updatedClip.name = trimmedName
        updatedClip.filename = targetFilename
        updatedClip.fileURL = targetURL
        return updatedClip
    }

    private func isSupportedAudioFile(_ url: URL) -> Bool {
        guard supportedAudioExtensions.contains(url.pathExtension.lowercased()) else { return false }

        let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .isReadableKey, .isSymbolicLinkKey, .isAliasFileKey])
        return values?.isRegularFile == true
            && values?.isReadable != false
            && values?.isSymbolicLink != true
            && values?.isAliasFile != true
    }

    private func isValidFileBaseName(_ name: String) -> Bool {
        !name.isEmpty
            && !name.contains("/")
            && !name.contains(":")
            && name != "."
            && name != ".."
    }

    private func makeClip(
        from url: URL,
        fallbackOrder: Int,
        managedRelativePath: String?,
        id: UUID? = nil
    ) -> SoundClip {
        return SoundClip(
            id: id ?? UUID(uuidString: stableUUIDString(for: managedRelativePath ?? url.standardizedFileURL.path)) ?? UUID(),
            name: url.deletingPathExtension().lastPathComponent,
            filename: url.lastPathComponent,
            category: .uncategorized,
            duration: duration(for: url),
            waveform: defaultWaveform(for: url.lastPathComponent),
            addedAt: addedAt(for: url, fallbackOrder: fallbackOrder),
            fileURL: url,
            storageMode: .managed,
            managedRelativePath: managedRelativePath,
            originalFilename: url.lastPathComponent,
            fileIdentity: filesystemIdentity(for: url)
        )
    }

    private func addedAt(for url: URL, fallbackOrder: Int) -> Date {
        let values = try? url.resourceValues(forKeys: [.creationDateKey, .contentModificationDateKey])
        return values?.creationDate
            ?? values?.contentModificationDate
            ?? Date(timeIntervalSinceReferenceDate: TimeInterval(fallbackOrder))
    }

    private func duration(for url: URL) -> TimeInterval {
        guard let audioFile = try? AVAudioFile(forReading: url) else { return 0 }
        let sampleRate = audioFile.fileFormat.sampleRate
        guard sampleRate > 0 else { return 0 }
        return TimeInterval(audioFile.length) / sampleRate
    }

    private func stableUUIDString(for value: String) -> String {
        let bytes = Array(value.utf8)
        var hash: UInt64 = 14_695_981_039_346_656_037
        for byte in bytes {
            hash ^= UInt64(byte)
            hash &*= 1_099_511_628_211
        }

        return String(format: "00000000-0000-4000-8000-%012llx", hash & 0xFFFFFFFFFFFF)
    }

    private func defaultWaveform(for seed: String) -> [Double] {
        var value = UInt64(seed.utf8.reduce(0) { ($0 &* 31) &+ UInt64($1) })
        return (0..<12).map { _ in
            value = value &* 1_664_525 &+ 1_013_904_223
            return 0.18 + Double(value % 70) / 100
        }
    }

    private func validateLibraryRoot(_ libraryURL: URL) throws {
        var isDirectory: ObjCBool = false
        let values = try? libraryURL.resourceValues(forKeys: [.isSymbolicLinkKey, .isDirectoryKey, .isReadableKey])
        guard FileManager.default.fileExists(atPath: libraryURL.path, isDirectory: &isDirectory),
              isDirectory.boolValue,
              values?.isSymbolicLink != true,
              values?.isReadable != false else {
            throw LibraryError.unsafeLibraryRoot(libraryURL)
        }
    }

    private func relativePath(for url: URL, libraryURL: URL) -> String {
        let rootComponents = libraryURL.standardizedFileURL.pathComponents
        let fileComponents = url.standardizedFileURL.pathComponents
        guard fileComponents.starts(with: rootComponents) else { return url.lastPathComponent }
        return fileComponents.dropFirst(rootComponents.count).joined(separator: "/")
    }

    private func safeManagedURL(relativePath: String, libraryURL: URL) throws -> URL {
        try validateLibraryRoot(libraryURL)
        let candidatePath = NSString(string: relativePath).standardizingPath
        guard !candidatePath.isEmpty,
              !candidatePath.hasPrefix("/"),
              candidatePath != "..",
              !candidatePath.hasPrefix("../") else {
            throw LibraryError.unsafeManagedPath(relativePath)
        }
        let root = libraryURL.standardizedFileURL.resolvingSymlinksInPath()
        let candidate = root.appendingPathComponent(candidatePath).standardizedFileURL
        let resolvedParent = candidate.deletingLastPathComponent().resolvingSymlinksInPath()
        let resolvedCandidate = resolvedParent.appendingPathComponent(candidate.lastPathComponent).standardizedFileURL
        guard resolvedCandidate.path.hasPrefix(root.path + "/") else {
            throw LibraryError.unsafeManagedPath(relativePath)
        }
        return candidate
    }

    private func collisionSafeDestination(for filename: String, in directory: URL) -> URL {
        let source = URL(fileURLWithPath: filename)
        let stem = source.deletingPathExtension().lastPathComponent
        let fileExtension = source.pathExtension
        var candidate = directory.appendingPathComponent(filename)
        var suffix = 2
        while FileManager.default.fileExists(atPath: candidate.path) {
            let nextName = fileExtension.isEmpty ? "\(stem) \(suffix)" : "\(stem) \(suffix).\(fileExtension)"
            candidate = directory.appendingPathComponent(nextName)
            suffix += 1
        }
        return candidate
    }

    private func isSymbolicLinkOrAlias(_ url: URL) -> Bool {
        let values = try? url.resourceValues(forKeys: [.isSymbolicLinkKey, .isAliasFileKey])
        return values?.isSymbolicLink == true || values?.isAliasFile == true
    }

    private func isUsableAudioFile(_ url: URL) -> Bool {
        FileManager.default.fileExists(atPath: url.path) && isSupportedAudioFile(url)
    }

    private func requiredIdentity(for url: URL) throws -> SoundFileIdentity {
        guard let identity = filesystemIdentity(for: url) else {
            throw LibraryError.fileUnavailable(url)
        }
        return identity
    }

    func filesystemIdentity(for url: URL) -> SoundFileIdentity? {
        guard let values = try? url.resourceValues(forKeys: [
            .fileResourceIdentifierKey,
            .volumeIdentifierKey,
            .fileSizeKey,
            .contentModificationDateKey
        ]) else { return nil }
        return SoundFileIdentity(
            resourceIdentifier: values.fileResourceIdentifier.map { String(describing: $0) },
            volumeIdentifier: values.volumeIdentifier.map { String(describing: $0) },
            fileSize: values.fileSize.map(Int64.init),
            contentModificationDate: values.contentModificationDate
        )
    }

    private func identitiesMatch(_ lhs: SoundFileIdentity, _ rhs: SoundFileIdentity) -> Bool {
        if let left = lhs.resourceIdentifier, let right = rhs.resourceIdentifier {
            return left == right && lhs.volumeIdentifier == rhs.volumeIdentifier
        }
        return lhs.fileSize == rhs.fileSize
            && lhs.contentModificationDate == rhs.contentModificationDate
            && lhs.volumeIdentifier == rhs.volumeIdentifier
    }

    private func makeBookmark(for url: URL) -> Data? {
        if let bookmark = try? url.bookmarkData(options: [.withSecurityScope], includingResourceValuesForKeys: nil, relativeTo: nil) {
            return bookmark
        }
        return try? url.bookmarkData(options: [.minimalBookmark], includingResourceValuesForKeys: nil, relativeTo: nil)
    }

    private func resolveLinkedURL(_ metadata: LibrarySoundMetadata) -> (url: URL?, isStale: Bool) {
        if let bookmark = metadata.securityScopedBookmark {
            var stale = false
            if let resolved = try? URL(
                resolvingBookmarkData: bookmark,
                options: [.withSecurityScope, .withoutUI],
                relativeTo: nil,
                bookmarkDataIsStale: &stale
            ) {
                return (resolved, stale)
            }
        }
        return (metadata.externalSourcePath.map { URL(fileURLWithPath: $0) }, false)
    }

}
