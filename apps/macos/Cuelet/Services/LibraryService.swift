import AVFoundation
import Foundation

struct LibraryService {
    enum LibraryError: LocalizedError {
        case unreadableFolder(URL)
        case missingFileURL
        case fileUnavailable(URL)
        case invalidRename(String)
        case targetAlreadyExists(URL)
        case renameFailed(Error)

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
            }
        }
    }

    private let supportedAudioExtensions: Set<String> = ["mp3", "wav", "m4a", "aiff", "aif", "flac"]

    func scanLibrary(at folderURL: URL, scansSubfolders: Bool) throws -> [SoundClip] {
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
                makeClip(from: url, fallbackOrder: offset)
            }
    }

    func importFiles(_ fileURLs: [URL], into clips: [SoundClip]) -> [SoundClip] {
        let importedClips = fileURLs
            .filter(isSupportedAudioFile)
            .sorted { lhs, rhs in lhs.lastPathComponent.localizedStandardCompare(rhs.lastPathComponent) == .orderedAscending }
            .enumerated()
            .map { offset, url in
                makeClip(from: url, fallbackOrder: clips.count + offset)
            }

        return clips + importedClips.filter { importedClip in
            !clips.contains { $0.fileURL == importedClip.fileURL }
        }
    }

    func loadMockLibrary() -> [SoundClip] {
        PreviewLibrary.sampleClips
    }

    func importMockSounds(into clips: [SoundClip]) -> [SoundClip] {
        var updatedClips = clips
        updatedClips.append(
            SoundClip(
                name: "Victory Sting",
                filename: "victory-sting.wav",
                category: .music,
                duration: 4,
                shortcut: SoundShortcut(keyCode: 25, characters: "9", modifiers: [.option]),
                waveform: [0.18, 0.42, 0.68, 0.92, 0.74, 0.38, 0.24, 0.56, 0.84, 0.61],
                isFavorite: true
            )
        )
        return updatedClips
    }

    func rescanMockLibrary(currentClips: [SoundClip]) -> [SoundClip] {
        currentClips.map { clip in
            var rescannedClip = clip
            rescannedClip.lastPlayedAt = clip.lastPlayedAt
            return rescannedClip
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

        let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .isReadableKey])
        return values?.isRegularFile == true && values?.isReadable != false
    }

    private func isValidFileBaseName(_ name: String) -> Bool {
        !name.isEmpty
            && !name.contains("/")
            && !name.contains(":")
            && name != "."
            && name != ".."
    }

    private func makeClip(from url: URL, fallbackOrder: Int) -> SoundClip {
        return SoundClip(
            id: UUID(uuidString: stableUUIDString(for: url)) ?? UUID(),
            name: url.deletingPathExtension().lastPathComponent,
            filename: url.lastPathComponent,
            category: .uncategorized,
            duration: duration(for: url),
            waveform: defaultWaveform(for: url.lastPathComponent),
            addedAt: addedAt(for: url, fallbackOrder: fallbackOrder),
            fileURL: url
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

    private func stableUUIDString(for url: URL) -> String {
        let bytes = Array(url.path.utf8)
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
}

enum PreviewLibrary {
    static let sampleClips: [SoundClip] = [
        SoundClip(
            name: "Rain on Window",
            filename: "rain-window.wav",
            category: .ambience,
            duration: 72,
            shortcut: SoundShortcut(keyCode: 18, characters: "1", modifiers: [.option]),
            waveform: [0.24, 0.38, 0.31, 0.44, 0.36, 0.51, 0.42, 0.34, 0.47, 0.39, 0.29, 0.41],
            isFavorite: true
        ),
        SoundClip(
            name: "Door Knock",
            filename: "door-knock.aiff",
            category: .effects,
            duration: 3,
            shortcut: SoundShortcut(keyCode: 19, characters: "2", modifiers: [.option]),
            waveform: [0.12, 0.9, 0.2, 0.75, 0.18, 0.36, 0.14, 0.28]
        ),
        SoundClip(
            name: "Soft Room Tone",
            filename: "soft-room-tone.flac",
            category: .ambience,
            duration: 96,
            shortcut: SoundShortcut(keyCode: 20, characters: "3", modifiers: [.option]),
            waveform: [0.28, 0.32, 0.27, 0.34, 0.31, 0.29, 0.35, 0.3, 0.33, 0.26]
        ),
        SoundClip(
            name: "Message Pop",
            filename: "message-pop.wav",
            category: .alerts,
            duration: 1,
            shortcut: SoundShortcut(keyCode: 21, characters: "4", modifiers: [.option]),
            waveform: [0.14, 0.52, 0.86, 0.48, 0.2, 0.08],
            isFavorite: true
        ),
        SoundClip(
            name: "Tension Bed",
            filename: "tension-bed.m4a",
            category: .music,
            duration: 124,
            shortcut: SoundShortcut(keyCode: 23, characters: "5", modifiers: [.option]),
            waveform: [0.18, 0.25, 0.37, 0.45, 0.62, 0.58, 0.66, 0.72, 0.64, 0.7, 0.55, 0.5]
        ),
        SoundClip(
            name: "Applause Short",
            filename: "applause-short.mp3",
            category: .effects,
            duration: 8,
            shortcut: SoundShortcut(keyCode: 22, characters: "6", modifiers: [.option]),
            waveform: [0.38, 0.72, 0.68, 0.8, 0.76, 0.71, 0.63, 0.42]
        ),
        SoundClip(
            name: "Host Intro",
            filename: "host-intro.wav",
            category: .voice,
            duration: 13,
            shortcut: SoundShortcut(keyCode: 26, characters: "7", modifiers: [.option]),
            waveform: [0.2, 0.52, 0.31, 0.74, 0.27, 0.61, 0.35, 0.56, 0.18]
        ),
        SoundClip(
            name: "Stop Cue",
            filename: "stop-cue.wav",
            category: .alerts,
            duration: 2,
            shortcut: SoundShortcut(keyCode: 28, characters: "8", modifiers: [.option]),
            waveform: [0.84, 0.64, 0.38, 0.18, 0.08]
        )
    ]
}
