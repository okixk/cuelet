import Darwin
import Foundation

struct SettingsStore {
    enum LoadResult {
        case missing(CueletSettings)
        case loaded(CueletSettings)
        case recovered(CueletSettings, primaryError: String)
        case failure(String)
    }

    let url: URL

    init(url: URL = SettingsStore.defaultURL) {
        self.url = url
    }

    var backupURL: URL { url.appendingPathExtension("backup") }
    var legacyMetadataBackupURL: URL { url.appendingPathExtension("pre-library-migration.bak") }

    func loadResult() -> LoadResult {
        guard FileManager.default.fileExists(atPath: url.path) else {
            return .missing(CueletSettings())
        }
        do {
            return .loaded(try decode(at: url))
        } catch {
            let primaryError = error.localizedDescription
            guard FileManager.default.fileExists(atPath: backupURL.path) else {
                return .failure("Cuelet settings could not be decoded: \(primaryError). The file was left untouched.")
            }
            do {
                return .recovered(try decode(at: backupURL), primaryError: primaryError)
            } catch {
                return .failure(
                    "Cuelet settings and their recovery copy could not be decoded. Primary: \(primaryError). Recovery: \(error.localizedDescription). Both files were left untouched."
                )
            }
        }
    }

    func load() -> CueletSettings {
        switch loadResult() {
        case .missing(let settings), .loaded(let settings), .recovered(let settings, _):
            return settings
        case .failure(let message):
            NSLog("%@", message)
            return CueletSettings()
        }
    }

    @discardableResult
    func save(_ settings: CueletSettings, preservePrimaryAsBackup: Bool = true) -> Bool {
        do {
            try FileManager.default.createDirectory(
                at: url.deletingLastPathComponent(),
                withIntermediateDirectories: true,
                attributes: [.posixPermissions: 0o700]
            )
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
            let data = try encoder.encode(settings)
            if preservePrimaryAsBackup, FileManager.default.fileExists(atPath: url.path) {
                try atomicWrite(Data(contentsOf: url), to: backupURL)
            }
            try atomicWrite(data, to: url)
            return true
        } catch {
            NSLog("Could not save Cuelet settings: %@", error.localizedDescription)
            return false
        }
    }

    @discardableResult
    func backupLegacyMetadataIfNeeded() -> Bool {
        guard FileManager.default.fileExists(atPath: url.path) else { return true }
        guard !FileManager.default.fileExists(atPath: legacyMetadataBackupURL.path) else { return true }
        do {
            try atomicWrite(Data(contentsOf: url), to: legacyMetadataBackupURL)
            return true
        } catch {
            NSLog("Could not back up legacy Cuelet metadata: %@", error.localizedDescription)
            return false
        }
    }

    private func decode(at url: URL) throws -> CueletSettings {
        try JSONDecoder().decode(CueletSettings.self, from: Data(contentsOf: url))
    }

    private func atomicWrite(_ data: Data, to destination: URL) throws {
        let temporary = destination.deletingLastPathComponent().appendingPathComponent(
            ".\(destination.lastPathComponent).tmp-\(UUID().uuidString)"
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

    static var applicationSupportURL: URL {
#if DEBUG
        let environment = ProcessInfo.processInfo.environment
        if let override = environment["CUELET_APP_SUPPORT_DIR"], !override.isEmpty {
            return URL(fileURLWithPath: NSString(string: override).expandingTildeInPath, isDirectory: true)
        }
#endif
        let baseURL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        return baseURL.appendingPathComponent("Cuelet", isDirectory: true)
    }

    static var defaultURL: URL {
        applicationSupportURL.appendingPathComponent("settings.json")
    }
}
