import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var appState: AppState

    var body: some View {
        TabView {
            Form {
                Section("Library") {
                    TextField("Library", text: $appState.settings.libraryPath)
                    Toggle("Scan subfolders", isOn: Binding(
                        get: { appState.settings.scansSubfolders },
                        set: { appState.setScansSubfolders($0) }
                    ))
                    Toggle("Stop playback when changing library", isOn: $appState.settings.stopOnLibraryChange)
                }

                Section("Import Behavior") {
                    Toggle("Copy imported files into the library", isOn: $appState.settings.copiesImportedFiles)
                    Toggle("Preserve folder structure", isOn: $appState.settings.preservesFolderStructure)
                }

                Section("Demo Library") {
                    Toggle("Show demo library", isOn: Binding(
                        get: { appState.showsMockLibrary },
                        set: { isOn in
                            if isOn {
                                appState.loadDemoLibrary()
                            } else {
                                appState.hideDemoLibrary()
                            }
                        }
                    ))
                    Text("Real chosen libraries take priority on launch. Run Cuelet with --demo to force the demo library for development.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(20)
            .tabItem { Label("Library", systemImage: "folder") }

            Form {
                Section("Playback") {
                    LabeledContent("Output", value: "macOS System Output")
                    Slider(value: $appState.settings.soundboardVolume, in: 0...1) {
                        Text("Soundboard Volume")
                    }
                    Toggle("Allow simultaneous playback", isOn: $appState.settings.allowsSimultaneousPlayback)
                }

                Section("Shortcuts") {
                    LabeledContent("Global Shortcuts", value: appState.globalShortcutStatusMessage)
                    Text("Assign Local or Global shortcuts from a sound's context menu.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(20)
            .tabItem { Label("Playback", systemImage: "speaker.wave.2") }

            AudioRoutingSettingsView(appState: appState)
                .padding(20)
                .tabItem { Label("Audio & Microphone", systemImage: "mic") }

            Form {
                Section("Background") {
                    Toggle("Keep Cuelet running after closing its main window", isOn: $appState.settings.keepsRunningAfterWindowClose)
                    Toggle("Show Cuelet in the menu bar", isOn: $appState.settings.showsMenuBarItem)
                    Toggle("Start hidden", isOn: $appState.settings.startsHidden)
                        .disabled(!appState.settings.keepsRunningAfterWindowClose)
                }

                Section("Login") {
                    Toggle("Launch at login", isOn: Binding(
                        get: { appState.settings.launchesAtLogin },
                        set: { appState.setLaunchesAtLogin($0) }
                    ))
                    Text("Launch at Login requires the Finder-launchable Cuelet app bundle.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(20)
            .tabItem { Label("Background", systemImage: "menubar.rectangle") }

            Form {
                Section("Appearance") {
                    Picker("Appearance", selection: $appState.settings.appearanceMode) {
                        Text("System").tag("System")
                        Text("Light").tag("Light")
                        Text("Dark").tag("Dark")
                    }
                }
            }
            .padding(20)
            .tabItem { Label("Display", systemImage: "rectangle.on.rectangle") }

            Form {
                Section("Advanced") {
                    Toggle("Show diagnostics", isOn: $appState.settings.showAdvancedDiagnostics)
                    LabeledContent("Settings domain", value: "com.cuelet.Cuelet")
                }
            }
            .padding(20)
            .tabItem { Label("Advanced", systemImage: "gearshape.2") }
        }
        .frame(width: 640, height: 500)
        .task {
            appState.refreshAudioRouting()
        }
    }
}

private struct AudioRoutingSettingsView: View {
    @ObservedObject var appState: AppState

    var body: some View {
        Form {
            Section("Speaker Output") {
                LabeledContent("Active Output", value: "macOS System Output")

                Picker("Routing Mode", selection: $appState.settings.audioRoutingMode) {
                    ForEach(AudioRoutingMode.allCases) { mode in
                        Text(mode.title)
                            .tag(mode)
                            .disabled(!mode.isImplemented)
                    }
                }

                Text("Cuelet currently follows the output selected in macOS. The detected devices below are informational until Cuelet's playback engine supports explicit device routing.")
                    .font(.callout)
                    .foregroundStyle(.secondary)

                ForEach(appState.outputDevices.filter { !$0.isDefault }) { device in
                    LabeledContent(device.name, value: device.isVirtual ? "Virtual" : "Detected")
                }
            }

            Section("Microphone Input") {
                LabeledContent("Metering Input", value: "macOS Default Microphone")

                ForEach(appState.inputDevices) { device in
                    LabeledContent(device.name, value: device.isVirtual ? "Virtual" : "Detected")
                }

                LabeledContent("Permission", value: appState.microphonePermissionState.title)

                HStack {
                    Button("Request Microphone Access") {
                        Task { await appState.requestMicrophonePermission() }
                    }
                    .disabled(appState.microphonePermissionState == .authorized)

                    Button("Refresh Devices") {
                        appState.refreshAudioRouting()
                    }
                }

                Toggle("Monitor input level", isOn: Binding(
                    get: { appState.settings.isInputMonitoringEnabled },
                    set: { appState.setInputMonitoringEnabled($0) }
                ))

                VStack(alignment: .leading, spacing: 6) {
                    ProgressView(value: appState.inputLevelState.averagePower)
                    Text(inputLevelLabel)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section("Virtual Device Routing") {
                LabeledContent("Virtual Device", value: virtualRoutingStatus)
                Text("Cuelet does not create a virtual microphone or mix microphone input into a virtual device yet. Use macOS or vendor software such as BlackHole or Loopback to configure routing externally.")
                    .font(.callout)
                    .foregroundStyle(.secondary)

                if !appState.audioStatusMessage.isEmpty {
                    Text(appState.audioStatusMessage)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
        }
    }

    private var inputLevelLabel: String {
        guard appState.inputLevelState.isMonitoring else { return "Input monitoring is off." }
        let level = Int(appState.inputLevelState.averagePower * 100)
        return "Input level: \(level)%"
    }

    private var virtualRoutingStatus: String {
        let devices = appState.outputDevices + appState.inputDevices
        if devices.contains(where: { $0.name.localizedCaseInsensitiveContains("BlackHole") }) {
            return "BlackHole detected"
        }
        if devices.contains(where: { $0.name.localizedCaseInsensitiveContains("Loopback") }) {
            return "Loopback device detected"
        }
        return devices.contains(where: \.isVirtual) ? "Virtual device detected" : "No virtual device detected"
    }
}
