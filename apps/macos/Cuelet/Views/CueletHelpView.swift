import AppKit
import SwiftUI

enum CueletHelpWindowID {
    static let help = "help"
}

struct CueletHelpStep: Identifiable, Equatable {
    let id: Int
    let title: String
    let detail: String
}

struct CueletHelpTopic: Identifiable, Equatable {
    let id: String
    let title: String
    let detail: String
}

enum CueletHelpContent {
    static let introduction = "Cuelet is a cross-platform soundboard and virtual microphone."

    static let sectionTitles = [
        "Getting Started",
        "Cuelet Virtual Microphone",
        "Global Shortcuts",
        "Troubleshooting",
        "Support & Online"
    ]

    static let gettingStarted = [
        CueletHelpStep(
            id: 1,
            title: "Choose a sound library",
            detail: "Choose Library… and select the folder Cuelet should use."
        ),
        CueletHelpStep(
            id: 2,
            title: "Import sounds",
            detail: "Use Import Sounds… and choose Copy into Cuelet Library or Link External Files."
        ),
        CueletHelpStep(
            id: 3,
            title: "Organize your library",
            detail: "Use Add to Favorites and Assign Category from a sound’s context menu."
        ),
        CueletHelpStep(
            id: 4,
            title: "Play a sound",
            detail: "Use the play button, or select a sound and press Space or Return."
        ),
        CueletHelpStep(
            id: 5,
            title: "Assign shortcuts",
            detail: "Choose Set Shortcut… from a sound’s context menu when you want keyboard playback."
        )
    ]

    static let virtualMicrophone = [
        "The full Cuelet package installs Cuelet Virtual Microphone.",
        "After installing or updating it, restart your Mac so Core Audio can load the driver. Cuelet itself remains usable before the restart.",
        "After restarting, open Settings → Audio & Microphone and choose Cuelet Virtual Microphone under Cuelet Output, or click Use as Cuelet Output.",
        "In the receiving application, select Cuelet Virtual Microphone as its input device."
    ]

    static let globalShortcuts = [
        "Control-click or right-click a sound and choose Set Shortcut… (or Change Shortcut… if it already has one).",
        "Set Scope to Global, press the key combination, and choose Save.",
        "Global shortcuts work while Cuelet is in the background and do not require Accessibility permission."
    ]

    static let troubleshooting = [
        CueletHelpTopic(
            id: "virtual-microphone-unavailable",
            title: "Virtual Microphone unavailable",
            detail: "Open Settings → Audio & Microphone and check Cuelet Virtual Microphone status. If it says Restart required, restart macOS and reopen Cuelet."
        ),
        CueletHelpTopic(
            id: "missing-sound-file",
            title: "Missing sound file",
            detail: "If a source file was moved or deleted, Cuelet keeps its library metadata and marks the sound missing. Right-click it and choose Relink… for a linked sound or Locate Replacement… for a managed sound."
        ),
        CueletHelpTopic(
            id: "no-audio",
            title: "No audio",
            detail: "Open Settings → Audio & Microphone. Under Cuelet Output, verify Output Device and check Selected, Active, and the routing status. Use Refresh Outputs if needed."
        )
    ]

    static let projectURL = URL(string: "https://github.com/okixk/cuelet")!
    static let issueTrackerURL = URL(string: "https://github.com/okixk/cuelet/issues")!

    static var offlineText: String {
        ([introduction] +
            gettingStarted.flatMap { [$0.title, $0.detail] } +
            virtualMicrophone +
            globalShortcuts +
            troubleshooting.flatMap { [$0.title, $0.detail] })
            .joined(separator: "\n")
    }
}

struct CueletHelpCommands: Commands {
    @Environment(\.openWindow) private var openWindow

    var body: some Commands {
        CommandGroup(replacing: .help) {
            Button("Cuelet Help") {
                NSApp.activate(ignoringOtherApps: true)
                openWindow(id: CueletHelpWindowID.help)
            }
            .keyboardShortcut("?", modifiers: [.command])
        }
    }
}

struct CueletHelpView: View {
    @Environment(\.dismissWindow) private var dismissWindow

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 26) {
                header

                Divider()

                helpSection("Getting Started", systemImage: "sparkles") {
                    VStack(alignment: .leading, spacing: 14) {
                        ForEach(CueletHelpContent.gettingStarted) { step in
                            HStack(alignment: .top, spacing: 12) {
                                Text("\(step.id)")
                                    .font(.callout.weight(.semibold))
                                    .foregroundStyle(.white)
                                    .frame(width: 24, height: 24)
                                    .background(.tint, in: Circle())
                                    .accessibilityHidden(true)

                                VStack(alignment: .leading, spacing: 2) {
                                    Text(step.title)
                                        .font(.headline)
                                    Text(step.detail)
                                        .foregroundStyle(.secondary)
                                        .fixedSize(horizontal: false, vertical: true)
                                }
                            }
                            .accessibilityElement(children: .combine)
                            .accessibilityLabel("Step \(step.id): \(step.title). \(step.detail)")
                        }
                    }
                }

                helpSection("Cuelet Virtual Microphone", systemImage: "mic") {
                    bulletList(CueletHelpContent.virtualMicrophone)
                }

                helpSection("Global Shortcuts", systemImage: "keyboard") {
                    bulletList(CueletHelpContent.globalShortcuts)
                }

                helpSection("Troubleshooting", systemImage: "wrench.and.screwdriver") {
                    VStack(alignment: .leading, spacing: 16) {
                        ForEach(CueletHelpContent.troubleshooting) { topic in
                            VStack(alignment: .leading, spacing: 3) {
                                Text(topic.title)
                                    .font(.headline)
                                Text(topic.detail)
                                    .foregroundStyle(.secondary)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                        }
                    }
                }

                helpSection("Support & Online", systemImage: "lifepreserver") {
                    VStack(alignment: .leading, spacing: 8) {
                        Link("Project — github.com/okixk/cuelet", destination: CueletHelpContent.projectURL)
                        Link("Report an issue — github.com/okixk/cuelet/issues", destination: CueletHelpContent.issueTrackerURL)
                    }
                }
            }
            .padding(28)
            .frame(maxWidth: 760, alignment: .leading)
        }
        .frame(minWidth: 560, minHeight: 460)
        .background {
            Button("") {
                dismissWindow(id: CueletHelpWindowID.help)
            }
            .keyboardShortcut(.cancelAction)
            .frame(width: 0, height: 0)
            .opacity(0)
            .accessibilityHidden(true)
        }
        .onKeyPress(.escape) {
            dismissWindow(id: CueletHelpWindowID.help)
            return .handled
        }
        .onExitCommand {
            dismissWindow(id: CueletHelpWindowID.help)
        }
    }

    private var header: some View {
        HStack(alignment: .center, spacing: 18) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .scaledToFit()
                .frame(width: 72, height: 72)
                .accessibilityLabel("Cuelet app icon")

            VStack(alignment: .leading, spacing: 5) {
                Text("Cuelet Help")
                    .font(.largeTitle.weight(.semibold))
                    .accessibilityAddTraits(.isHeader)
                Text(CueletHelpContent.introduction)
                    .font(.title3)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    private func helpSection<Content: View>(
        _ title: String,
        systemImage: String,
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Label(title, systemImage: systemImage)
                .font(.title2.weight(.semibold))
                .accessibilityAddTraits(.isHeader)

            content()
                .padding(.leading, 2)
        }
    }

    private func bulletList(_ items: [String]) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(items, id: \.self) { item in
                HStack(alignment: .firstTextBaseline, spacing: 10) {
                    Image(systemName: "circle.fill")
                        .font(.system(size: 5))
                        .foregroundStyle(.secondary)
                        .accessibilityHidden(true)
                    Text(item)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .accessibilityElement(children: .combine)
            }
        }
    }
}
