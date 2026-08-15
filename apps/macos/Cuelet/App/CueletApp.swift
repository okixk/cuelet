import SwiftUI

@main
struct CueletApp: App {
    @StateObject private var appState = AppState()
    @NSApplicationDelegateAdaptor(CueletApplicationDelegate.self) private var applicationDelegate

    var body: some Scene {
        WindowGroup(id: "main") {
            RootView()
                .environmentObject(appState)
                .frame(minWidth: 980, minHeight: 680)
                .onAppear {
                    applicationDelegate.appState = appState
                    appState.applyStartupVisibilityIfNeeded()
                }
        }
        .windowToolbarStyle(.unified(showsTitle: true))
        .commands {
            CueletAboutCommands()
            CueletHelpCommands()
            ToolbarCommands()

            CommandGroup(after: .textEditing) {
                Button("Find") {
                    appState.requestSearchFocus()
                }
                .keyboardShortcut("f", modifiers: [.command])

                Button("Focus Search") {
                    appState.requestSearchFocus()
                }
                .keyboardShortcut("f", modifiers: [.control])
            }

            CommandMenu("Library") {
                Button("Choose Library…") {
                    appState.chooseLibrary()
                }
                .keyboardShortcut("o", modifiers: [.command])

                Button("Import Sounds…") {
                    appState.importSounds()
                }
                .keyboardShortcut("i", modifiers: [.command])

                Button("Rescan Library") {
                    appState.rescanLibrary()
                }
                .keyboardShortcut("r", modifiers: [.command])
            }

            CommandMenu("Playback") {
                Button("Play Selected") {
                    appState.playSelectedVisibleSound()
                }
                .disabled(appState.selectedClip == nil)

                Button("Clear Selection or Stop") {
                    _ = appState.handleEscapeFromKeyboard()
                }
                .keyboardShortcut(.escape, modifiers: [])

                Button("Stop All") {
                    appState.stopAllPlayback()
                }
                .keyboardShortcut(".", modifiers: [.command])
            }
        }

        Settings {
            SettingsView()
                .environmentObject(appState)
        }
        .keyboardShortcut(",", modifiers: [.command])

        Window("About Cuelet", id: CueletWindowID.about) {
            CueletAboutView()
        }
        .defaultPosition(.center)
        .windowResizability(.contentSize)

        Window("Cuelet License", id: CueletWindowID.license) {
            CueletLicenseView()
        }
        .defaultPosition(.center)
        .defaultSize(width: 720, height: 560)

        Window("Cuelet Help", id: CueletHelpWindowID.help) {
            CueletHelpView()
        }
        .defaultPosition(.center)
        .defaultSize(width: 720, height: 680)

        MenuBarExtra(
            "Cuelet",
            systemImage: "waveform",
            isInserted: Binding(
                get: { appState.settings.showsMenuBarItem },
                set: { isInserted in
                    // macOS may write the current menu-bar visibility back while
                    // reconciling the scene. Avoid publishing and persisting an
                    // unchanged settings value from inside that view update.
                    guard appState.settings.showsMenuBarItem != isInserted else { return }
                    appState.settings.showsMenuBarItem = isInserted
                }
            )
        ) {
            Button("Open Cuelet") {
                appState.showMainWindow()
            }

            Button("Stop All Sounds") {
                appState.stopAllPlayback()
            }
            .disabled(!appState.playbackState.isPlaying)

            Divider()

            Text(appState.globalShortcutStatusMessage)

            Divider()

            Button("Quit Cuelet") {
                appState.prepareForTermination()
                NSApp.terminate(nil)
            }
            .keyboardShortcut("q")
        }
    }
}

@MainActor
final class CueletApplicationDelegate: NSObject, NSApplicationDelegate {
    weak var appState: AppState?

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        !(appState?.settings.keepsRunningAfterWindowClose ?? false)
    }

    func applicationWillTerminate(_ notification: Notification) {
        appState?.prepareForTermination()
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag {
            appState?.showMainWindow()
        }
        return true
    }
}
