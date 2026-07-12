import XCTest
@testable import Cuelet

final class LibraryServiceTests: XCTestCase {
    func testScansSupportedAudioFilesFromFolderAndSubfolders() throws {
        let root = try makeTemporaryDirectory()
        let nested = root.appendingPathComponent("nested", isDirectory: true)
        try FileManager.default.createDirectory(at: nested, withIntermediateDirectories: true)
        try Data().write(to: root.appendingPathComponent("Kick.wav"))
        try Data().write(to: nested.appendingPathComponent("Door Knock.MP3"))
        try Data().write(to: root.appendingPathComponent("notes.txt"))

        let clips = try LibraryService().scanLibrary(at: root, scansSubfolders: true)

        XCTAssertEqual(clips.map(\.filename), ["Door Knock.MP3", "Kick.wav"])
        XCTAssertTrue(clips.allSatisfy { $0.fileURL != nil })
    }

    func testDoesNotScanSubfoldersWhenDisabled() throws {
        let root = try makeTemporaryDirectory()
        let nested = root.appendingPathComponent("nested", isDirectory: true)
        try FileManager.default.createDirectory(at: nested, withIntermediateDirectories: true)
        try Data().write(to: root.appendingPathComponent("Kick.wav"))
        try Data().write(to: nested.appendingPathComponent("Door Knock.mp3"))

        let clips = try LibraryService().scanLibrary(at: root, scansSubfolders: false)

        XCTAssertEqual(clips.map(\.filename), ["Kick.wav"])
    }

    func testRenameClipFileRenamesFileOnDiskAndPreservesExtension() throws {
        let root = try makeTemporaryDirectory()
        let originalURL = root.appendingPathComponent("du-bist-gut-genug-meme.mp3")
        try Data("sound".utf8).write(to: originalURL)

        let service = LibraryService()
        let clip = try XCTUnwrap(try service.scanLibrary(at: root, scansSubfolders: false).first)

        let renamedClip = try service.renameClipFile(clip, to: "du bist gut genug")

        XCTAssertFalse(FileManager.default.fileExists(atPath: originalURL.path))
        XCTAssertEqual(renamedClip.filename, "du bist gut genug.mp3")
        XCTAssertEqual(renamedClip.name, "du bist gut genug")
        XCTAssertEqual(renamedClip.fileURL?.lastPathComponent, "du bist gut genug.mp3")
        XCTAssertTrue(FileManager.default.fileExists(atPath: renamedClip.fileURL?.path ?? ""))
    }

    func testRenameClipFileRefusesToOverwriteExistingFile() throws {
        let root = try makeTemporaryDirectory()
        let originalURL = root.appendingPathComponent("old.mp3")
        let existingURL = root.appendingPathComponent("new.mp3")
        try Data("old".utf8).write(to: originalURL)
        try Data("existing".utf8).write(to: existingURL)

        let service = LibraryService()
        let clip = try XCTUnwrap(try service.scanLibrary(at: root, scansSubfolders: false).first { $0.filename == "old.mp3" })

        XCTAssertThrowsError(try service.renameClipFile(clip, to: "new"))
        XCTAssertTrue(FileManager.default.fileExists(atPath: originalURL.path))
        XCTAssertEqual(try String(contentsOf: existingURL), "existing")
    }

    func testScansAndRenamesUnicodeFilenamesWithoutLoss() throws {
        let root = try makeTemporaryDirectory()
        let originalURL = root.appendingPathComponent("jönu 日本語.wav")
        try Data("audio".utf8).write(to: originalURL)
        let service = LibraryService()

        let clip = try XCTUnwrap(service.scanLibrary(at: root, scansSubfolders: true).first)
        XCTAssertEqual(clip.filename, "jönu 日本語.wav")
        XCTAssertEqual(clip.displayName, "jönu 日本語")

        let renamed = try service.renameClipFile(clip, to: "Geräusche música файл")
        XCTAssertEqual(renamed.filename, "Geräusche música файл.wav")
        XCTAssertTrue(FileManager.default.fileExists(atPath: try XCTUnwrap(renamed.fileURL).path))
    }

    private func makeTemporaryDirectory() throws -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }
}
