import AppKit
import SwiftUI

enum CueletWindowID {
    static let about = "about"
    static let license = "license"
}

enum CueletAboutContent {
    static let contributors = "Cuelet contributors"
    static let summary = "A cross-platform soundboard and virtual microphone."
    static let licenseStatement = "Cuelet is free and open-source software licensed under the GNU Affero General Public License version 3 only."
    static let projectURL = URL(string: "https://github.com/okixk/cuelet")!
    static let issueTrackerURL = URL(string: "https://github.com/okixk/cuelet/issues")!
}

struct CueletAboutMetadata: Equatable {
    let applicationName: String
    let shortVersion: String
    let buildVersion: String

    init(infoDictionary: [String: Any], fallbackApplicationName: String = "Cuelet") {
        applicationName = Self.nonemptyString(
            infoDictionary["CFBundleDisplayName"] ?? infoDictionary["CFBundleName"]
        ) ?? fallbackApplicationName
        shortVersion = Self.nonemptyString(infoDictionary["CFBundleShortVersionString"]) ?? "Unknown"
        buildVersion = Self.nonemptyString(infoDictionary["CFBundleVersion"]) ?? "Unknown"
    }

    var versionText: String {
        "Version \(shortVersion)"
    }

    static var current: CueletAboutMetadata {
        CueletAboutMetadata(infoDictionary: Bundle.main.infoDictionary ?? [:])
    }

    private static func nonemptyString(_ value: Any?) -> String? {
        guard let string = value as? String else { return nil }
        let trimmed = string.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }
}

struct CueletAboutCommands: Commands {
    @Environment(\.openWindow) private var openWindow

    var body: some Commands {
        CommandGroup(replacing: .appInfo) {
            Button("About Cuelet") {
                NSApp.activate(ignoringOtherApps: true)
                openWindow(id: CueletWindowID.about)
            }
        }
    }
}

struct CueletAboutView: View {
    @Environment(\.dismissWindow) private var dismissWindow
    @Environment(\.openWindow) private var openWindow

    private let metadata = CueletAboutMetadata.current

    var body: some View {
        VStack(spacing: 0) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .scaledToFit()
                .frame(width: 112, height: 112)
                .accessibilityLabel("Cuelet app icon")

            Text(metadata.applicationName)
                .font(.system(size: 28, weight: .semibold))
                .foregroundStyle(Color(red: 106.0 / 255.0, green: 0, blue: 1))
                .padding(.top, 10)

            Text(CueletAboutContent.contributors)
                .font(.headline)
                .padding(.top, 4)

            Text(metadata.versionText)
                .foregroundStyle(.secondary)
                .padding(.top, 2)

            Text(CueletAboutContent.summary)
                .multilineTextAlignment(.center)
                .padding(.top, 18)

            Text(CueletAboutContent.licenseStatement)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.top, 10)

            VStack(spacing: 5) {
                Link("github.com/okixk/cuelet", destination: CueletAboutContent.projectURL)
                Link("github.com/okixk/cuelet/issues", destination: CueletAboutContent.issueTrackerURL)
            }
            .padding(.top, 14)

            Button("View License…") {
                openWindow(id: CueletWindowID.license)
            }
            .padding(.top, 16)
        }
        .padding(.horizontal, 32)
        .padding(.top, 26)
        .padding(.bottom, 24)
        .frame(width: 500)
        .background {
            Button("") {
                dismissWindow(id: CueletWindowID.about)
            }
            .keyboardShortcut(.cancelAction)
            .frame(width: 0, height: 0)
            .opacity(0)
            .accessibilityHidden(true)
        }
        .onKeyPress(.escape) {
            dismissWindow(id: CueletWindowID.about)
            return .handled
        }
        .onExitCommand {
            dismissWindow(id: CueletWindowID.about)
        }
    }
}

struct CueletLicenseView: View {
    @Environment(\.dismissWindow) private var dismissWindow

    private let licenseText = CueletLicenseText.load()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("GNU Affero General Public License v3.0 only")
                .font(.title2.weight(.semibold))

            ScrollView {
                Text(licenseText)
                    .font(.system(.body, design: .monospaced))
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(12)
            }
            .background(.background.secondary, in: RoundedRectangle(cornerRadius: 8))
        }
        .padding(20)
        .frame(minWidth: 560, minHeight: 400)
        .background {
            Button("") {
                dismissWindow(id: CueletWindowID.license)
            }
            .keyboardShortcut(.cancelAction)
            .frame(width: 0, height: 0)
            .opacity(0)
            .accessibilityHidden(true)
        }
        .onKeyPress(.escape) {
            dismissWindow(id: CueletWindowID.license)
            return .handled
        }
        .onExitCommand {
            dismissWindow(id: CueletWindowID.license)
        }
    }
}

enum CueletLicenseText {
    static func load(bundle: Bundle = .main) -> String {
        guard let url = bundle.url(forResource: "LICENSE", withExtension: "txt"),
              let text = try? String(contentsOf: url, encoding: .utf8) else {
            return "The full license text is unavailable in this development build. Visit github.com/okixk/cuelet/blob/main/LICENSE."
        }
        return text
    }
}
