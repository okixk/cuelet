import Foundation

struct SettingsStore {
    let url: URL

    init(url: URL = SettingsStore.defaultURL) {
        self.url = url
    }

    func load() -> CueletSettings {
        guard let data = try? Data(contentsOf: url),
              let settings = try? JSONDecoder().decode(CueletSettings.self, from: data) else {
            return CueletSettings()
        }

        return settings
    }

    @discardableResult
    func save(_ settings: CueletSettings) -> Bool {
        do {
            try FileManager.default.createDirectory(
                at: url.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            let data = try JSONEncoder().encode(settings)
            try data.write(to: url, options: [.atomic])
            return true
        } catch {
            NSLog("Could not save Cuelet settings: %@", error.localizedDescription)
            return false
        }
    }

    private static var defaultURL: URL {
        let baseURL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        return baseURL
            .appendingPathComponent("Cuelet", isDirectory: true)
            .appendingPathComponent("settings.json")
    }
}
